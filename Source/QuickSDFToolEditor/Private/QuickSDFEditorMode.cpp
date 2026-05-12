#include "QuickSDFEditorMode.h"
#include "Editor.h"
#include "QuickSDFEditorModeToolkit.h"
#include "EngineUtils.h"
#include "Engine/DirectionalLight.h"
#include "Components/DirectionalLightComponent.h"
#include "InteractiveToolManager.h"
#include "QuickSDFEditorModeCommands.h"
#include "QuickSDFPaintToolBuilder.h"
#include "QuickSDFPaintTool.h"
#include "QuickSDFMeshComponentAdapter.h"
#include "QuickSDFToolSubsystem.h"
#include "QuickSDFToolStyle.h"
#include "Tools/EdModeInteractiveToolsContext.h"
#include "QuickSDFSelectTool.h"
#include "SQuickSDFTimeline.h"
#include "CameraController.h"
#include "EditorViewportClient.h"
#include "LevelEditor.h"
#include "SLevelViewport.h"
#include "Framework/Application/IInputProcessor.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Commands/UICommandList.h"
#include "HAL/PlatformTime.h"
#include "Input/Events.h"
#include "SQuickSDFPaintCanvas.h"
#include "Widgets/SViewport.h"

#if PLATFORM_WINDOWS
#include "Windows/WindowsApplication.h"
#include "Windows/AllowWindowsPlatformTypes.h"
#include <windows.h>
#include "Windows/HideWindowsPlatformTypes.h"
#endif

const FEditorModeID UQuickSDFEditorMode::EM_QuickSDFEditorModeId = TEXT("EM_QuickSDFEditorMode");

namespace
{
constexpr double QuickSDFPenPointerApplyFreshSeconds = 0.75;
constexpr float QuickSDFWindowsMaxPenPressure = 1024.0f;

class FQuickSDFBrushResizeInputPreProcessor final : public IInputProcessor
#if PLATFORM_WINDOWS
	, public IWindowsMessageHandler
#endif
{
public:
	explicit FQuickSDFBrushResizeInputPreProcessor(UQuickSDFEditorMode* InMode)
		: Mode(InMode)
	{
#if PLATFORM_WINDOWS
		RegisterWindowsMessageHandler();
#endif
	}

	virtual ~FQuickSDFBrushResizeInputPreProcessor() override
	{
#if PLATFORM_WINDOWS
		UnregisterWindowsMessageHandler();
#endif
	}

	virtual void Tick(const float DeltaTime, FSlateApplication& SlateApp, TSharedRef<ICursor> Cursor) override
	{
#if PLATFORM_WINDOWS
		if (LastPenPointerSerial != LastAppliedPenPointerSerial)
		{
			if (FPlatformTime::Seconds() - LastPenPointerUpdateTime <= QuickSDFPenPointerApplyFreshSeconds)
			{
				if (UQuickSDFPaintTool* PaintTool = GetActivePaintTool())
				{
					PaintTool->UpdateExternalViewportPointerHover(LastPenPointerAbsolutePosition);
				}
			}
			LastAppliedPenPointerSerial = LastPenPointerSerial;
		}
#endif
	}

#if PLATFORM_WINDOWS
	virtual bool ProcessMessage(HWND hwnd, uint32 msg, WPARAM wParam, LPARAM lParam, int32& OutResult) override
	{
		if (msg != WM_POINTERUPDATE && msg != WM_POINTERDOWN && msg != WM_POINTERUP)
		{
			return false;
		}

		const uint32 PointerId = static_cast<uint32>(wParam & 0xFFFF);
		POINTER_PEN_INFO PenInfo;
		FMemory::Memzero(PenInfo);
		if (::GetPointerPenInfo(PointerId, &PenInfo) == 0)
		{
			return false;
		}

		LastPenPointerAbsolutePosition = FVector2D(
			static_cast<double>(PenInfo.pointerInfo.ptPixelLocation.x),
			static_cast<double>(PenInfo.pointerInfo.ptPixelLocation.y));
		bLastPenPointerInContact = (PenInfo.pointerInfo.pointerFlags & POINTER_FLAG_INCONTACT) != 0;
		const bool bHasPressure = (PenInfo.penMask & PEN_MASK_PRESSURE) != 0;
		LastPenPointerPressure = (bLastPenPointerInContact && bHasPressure)
			? FMath::Clamp(static_cast<float>(PenInfo.pressure) / QuickSDFWindowsMaxPenPressure, 0.0f, 1.0f)
			: 1.0f;
		LastPenPointerUpdateTime = FPlatformTime::Seconds();
		++LastPenPointerSerial;
		if (UQuickSDFPaintTool* PaintTool = GetActivePaintTool())
		{
			PaintTool->UpdateExternalPenPointerState(LastPenPointerAbsolutePosition, LastPenPointerPressure, bLastPenPointerInContact);
		}
		if (QuickSDFPaintCanvas::UpdateExternalPenPointerState(LastPenPointerAbsolutePosition, bLastPenPointerInContact))
		{
			OutResult = 0;
			return true;
		}
		if (UQuickSDFPaintTool* PaintTool = GetActivePaintTool())
		{
			if (PaintTool->HandleExternalViewportPenPointer(LastPenPointerAbsolutePosition, LastPenPointerPressure, bLastPenPointerInContact))
			{
				OutResult = 0;
				return true;
			}
		}
		return false;
	}
#endif

	virtual bool HandleKeyDownEvent(FSlateApplication& SlateApp, const FKeyEvent& InKeyEvent) override
	{
		if (UQuickSDFPaintTool* PaintTool = GetActivePaintTool())
		{
			if (PaintTool->IsBrushResizeModeActive())
			{
				if (InKeyEvent.GetKey() == EKeys::Escape)
				{
					PaintTool->CancelBrushResizeMode();
					return true;
				}
				if (InKeyEvent.GetKey() == EKeys::F)
				{
					return true;
				}
			}
		}

		if (InKeyEvent.GetKey() != EKeys::F || !InKeyEvent.IsControlDown())
		{
			return false;
		}

		FVector2D BrushResizeAbsolutePosition = SlateApp.GetCursorPos();
		bool bBrushResizeFromExternalPen = false;
#if PLATFORM_WINDOWS
		bBrushResizeFromExternalPen = TryGetFreshPenPointerAbsolutePosition(BrushResizeAbsolutePosition);
#endif
		if (QuickSDFPaintCanvas::RequestBrushResizeFromHoveredCanvas(BrushResizeAbsolutePosition, bBrushResizeFromExternalPen))
		{
			return true;
		}

		UQuickSDFEditorMode* ModePtr = Mode.Get();
		return ModePtr ? ModePtr->RequestBrushResizeFromHoveredViewport(BrushResizeAbsolutePosition, bBrushResizeFromExternalPen) : false;
	}

	virtual bool HandleMouseButtonDownEvent(FSlateApplication& SlateApp, const FPointerEvent& MouseEvent) override
	{
		UQuickSDFPaintTool* PaintTool = GetActivePaintTool();
		if (!PaintTool || !PaintTool->IsBrushResizeModeActive())
		{
			return false;
		}

		if (MouseEvent.GetEffectingButton() == EKeys::RightMouseButton)
		{
			PaintTool->CancelBrushResizeMode();
			bSuppressNextMouseButtonUp = true;
			return true;
		}

		if (MouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
		{
			PaintTool->ConfirmBrushResizeMode();
			bSuppressNextMouseButtonUp = true;
			return true;
		}

		return true;
	}

	virtual bool HandleMouseButtonUpEvent(FSlateApplication& SlateApp, const FPointerEvent& MouseEvent) override
	{
		if (bSuppressNextMouseButtonUp &&
			(MouseEvent.GetEffectingButton() == EKeys::RightMouseButton ||
				MouseEvent.GetEffectingButton() == EKeys::LeftMouseButton))
		{
			bSuppressNextMouseButtonUp = false;
			return true;
		}

		return false;
	}

	virtual const TCHAR* GetDebugName() const override
	{
		return TEXT("QuickSDFBrushResizeInputPreProcessor");
	}

private:
	UQuickSDFPaintTool* GetActivePaintTool() const
	{
		UQuickSDFEditorMode* ModePtr = Mode.Get();
		return ModePtr && ModePtr->GetToolManager()
			? Cast<UQuickSDFPaintTool>(ModePtr->GetToolManager()->GetActiveTool(EToolSide::Left))
			: nullptr;
	}

#if PLATFORM_WINDOWS
	bool TryGetFreshPenPointerAbsolutePosition(FVector2D& OutAbsolutePosition) const
	{
		if (FPlatformTime::Seconds() - LastPenPointerUpdateTime > QuickSDFPenPointerApplyFreshSeconds)
		{
			return false;
		}

		OutAbsolutePosition = LastPenPointerAbsolutePosition;
		return true;
	}

	void RegisterWindowsMessageHandler()
	{
		if (bWindowsMessageHandlerRegistered || !FSlateApplication::IsInitialized())
		{
			return;
		}

		const TSharedPtr<GenericApplication> PlatformApplication = FSlateApplication::Get().GetPlatformApplication();
		if (!PlatformApplication.IsValid())
		{
			return;
		}

		WindowsApplication = static_cast<FWindowsApplication*>(PlatformApplication.Get());
		if (WindowsApplication)
		{
			WindowsApplication->AddMessageHandler(*this);
			bWindowsMessageHandlerRegistered = true;
		}
	}

	void UnregisterWindowsMessageHandler()
	{
		if (bWindowsMessageHandlerRegistered && WindowsApplication)
		{
			WindowsApplication->RemoveMessageHandler(*this);
		}
		WindowsApplication = nullptr;
		bWindowsMessageHandlerRegistered = false;
	}
#endif

	TWeakObjectPtr<UQuickSDFEditorMode> Mode;
	bool bSuppressNextMouseButtonUp = false;
#if PLATFORM_WINDOWS
	FWindowsApplication* WindowsApplication = nullptr;
	FVector2D LastPenPointerAbsolutePosition = FVector2D::ZeroVector;
	float LastPenPointerPressure = 1.0f;
	double LastPenPointerUpdateTime = -1000.0;
	uint64 LastPenPointerSerial = 0;
	uint64 LastAppliedPenPointerSerial = 0;
	bool bLastPenPointerInContact = false;
	bool bWindowsMessageHandlerRegistered = false;
#endif
};
}

UQuickSDFEditorMode::UQuickSDFEditorMode()
{
	Info = FEditorModeInfo(EM_QuickSDFEditorModeId, 
		INVTEXT("Quick SDF"),
		FSlateIcon(FQuickSDFToolStyle::GetStyleSetName(), "LevelEditor.QuickSDFToolMode", "LevelEditor.QuickSDFToolMode.Small"), 
		true);
}

void UQuickSDFEditorMode::Enter()
{
	Super::Enter();
	
	FQuickSDFEditorModeCommands ToolManagerCommands = FQuickSDFEditorModeCommands::Get();
	
	RegisterTool(ToolManagerCommands.SelectTextureAsset, TEXT("QuickSDFSelectTool"), NewObject<UQuickSDFSelectToolBuilder>(this));
	RegisterTool(ToolManagerCommands.PaintTextureColor, TEXT("QuickSDFPaintTool"), NewObject<UQuickSDFPaintToolBuilder>(this));
	
	ActorSelectionChangeNotify();
	StartQuickSDFSelectTool();
	GetToolManager()->ConfigureChangeTrackingMode(EToolChangeTrackingMode::NoChangeTracking);
	Toolkit->SetCurrentPalette(FName(TEXT("Default")));

	if (!BrushResizeInputPreProcessor.IsValid())
	{
		BrushResizeInputPreProcessor = MakeShared<FQuickSDFBrushResizeInputPreProcessor>(this);
		FSlateApplication::Get().RegisterInputPreProcessor(BrushResizeInputPreProcessor);
	}

	// Register save delegates to prevent permanent lighting changes
	FEditorDelegates::PreSaveWorldWithContext.AddUObject(this, &UQuickSDFEditorMode::OnPreSaveWorld);
	FEditorDelegates::PostSaveWorldWithContext.AddUObject(this, &UQuickSDFEditorMode::OnPostSaveWorld);
}

void UQuickSDFEditorMode::Exit()
{
	if (BrushResizeInputPreProcessor.IsValid())
	{
		FSlateApplication::Get().UnregisterInputPreProcessor(BrushResizeInputPreProcessor);
		BrushResizeInputPreProcessor.Reset();
	}

	DetachTimelineFromViewport();
	TimelineWidget.Reset();
	EndViewportNavigationSuppression();

	RestoreLights();

	// Unregister save delegates
	FEditorDelegates::PreSaveWorldWithContext.RemoveAll(this);
	FEditorDelegates::PostSaveWorldWithContext.RemoveAll(this);

	// Destroy preview light
	if (PreviewLight)
	{
		PreviewLight->Destroy();
		PreviewLight = nullptr;
	}

	Super::Exit();
	// Clean up tools
}

void UQuickSDFEditorMode::AttachTimelineToActiveViewport()
{
	if (!FModuleManager::Get().IsModuleLoaded("LevelEditor"))
	{
		return;
	}

	FLevelEditorModule& LevelEditorModule = FModuleManager::GetModuleChecked<FLevelEditorModule>("LevelEditor");
	TSharedPtr<ILevelEditor> LevelEditor = LevelEditorModule.GetFirstLevelEditor();
	if (!LevelEditor.IsValid())
	{
		return;
	}

	TSharedPtr<SLevelViewport> ActiveViewport = LevelEditor->GetActiveViewportInterface();
	if (!ActiveViewport.IsValid())
	{
		return;
	}

	if (!TimelineWidget.IsValid())
	{
		TimelineWidget = SNew(SQuickSDFTimeline);
	}

	if (TimelineViewport.Pin() == ActiveViewport)
	{
		return;
	}

	DetachTimelineFromViewport();
	ActiveViewport->AddOverlayWidget(TimelineWidget.ToSharedRef());
	TimelineViewport = ActiveViewport;
}

void UQuickSDFEditorMode::DetachTimelineFromViewport()
{
	TSharedPtr<SLevelViewport> AttachedViewport = TimelineViewport.Pin();
	if (AttachedViewport.IsValid() && TimelineWidget.IsValid())
	{
		AttachedViewport->RemoveOverlayWidget(TimelineWidget.ToSharedRef());
	}
	TimelineViewport.Reset();
}

bool UQuickSDFEditorMode::RequestBrushResizeFromHoveredViewport()
{
	return RequestBrushResizeFromHoveredViewport(FSlateApplication::Get().GetCursorPos(), false);
}

bool UQuickSDFEditorMode::RequestBrushResizeFromHoveredViewport(const FVector2D& AbsoluteScreenPosition, bool bFromExternalPen)
{
	TSharedPtr<SLevelViewport> AttachedViewport = TimelineViewport.Pin();
	if (!AttachedViewport.IsValid())
	{
		AttachTimelineToActiveViewport();
		AttachedViewport = TimelineViewport.Pin();
	}

	bool bCursorOverViewport = false;
	if (AttachedViewport.IsValid())
	{
		if (TSharedPtr<SViewport> ViewportWidget = AttachedViewport->GetViewportWidget().Pin())
		{
			bCursorOverViewport = ViewportWidget->GetTickSpaceGeometry().IsUnderLocation(AbsoluteScreenPosition);
		}
	}

	if (!bCursorOverViewport)
	{
		return false;
	}

	UInteractiveToolManager* ToolManager = GetToolManager();
	if (UQuickSDFPaintTool* PaintTool = ToolManager ? Cast<UQuickSDFPaintTool>(ToolManager->GetActiveTool(EToolSide::Left)) : nullptr)
	{
		return PaintTool->RequestBrushResizeMode(AbsoluteScreenPosition, bFromExternalPen);
	}

	return false;
}

void UQuickSDFEditorMode::StartQuickSDFPaintTool()
{
	GetInteractiveToolsContext()->StartTool(TEXT("QuickSDFPaintTool"));
	UpdatePaintToolEnvironment();
}

void UQuickSDFEditorMode::StartQuickSDFSelectTool()
{
	GetInteractiveToolsContext()->StartTool(TEXT("QuickSDFSelectTool"));
	UpdatePaintToolEnvironment();
}

void UQuickSDFEditorMode::MuteLights()
{
	if (OriginalLightStates.Num() > 0) return;

	if (UWorld* World = GetWorld())
	{
		for (TActorIterator<ADirectionalLight> It(World); It; ++It)
		{
			if (ADirectionalLight* Light = *It)
			{
				// Skip the preview light itself
				if (Light == PreviewLight) continue;

				FQuickSDFLightState State;
				State.Light = Light;
				State.Intensity = Light->GetLightComponent()->Intensity;
				OriginalLightStates.Add(State);
				
				Light->GetLightComponent()->SetIntensity(0.0f);
			}
		}

		// Spawn Preview Light if it doesn't exist yet
		if (!PreviewLight)
		{
			FActorSpawnParameters SpawnParams;
			SpawnParams.ObjectFlags |= RF_Transient;
			PreviewLight = World->SpawnActor<ADirectionalLight>(ADirectionalLight::StaticClass(), SpawnParams);
			if (PreviewLight)
			{
				PreviewLight->SetActorLabel(TEXT("QuickSDF_PreviewLight"));
				PreviewLight->GetLightComponent()->SetIntensity(10.0f);
				PreviewLight->GetLightComponent()->SetMobility(EComponentMobility::Movable);
				PreviewLight->SetActorRotation(FRotator(-45.0f, 0.0f, 0.0f));
			}
		}
	}
}

void UQuickSDFEditorMode::RestoreLights()
{
	for (const FQuickSDFLightState& State : OriginalLightStates)
	{
		if (State.Light.IsValid())
		{
			State.Light->GetLightComponent()->SetIntensity(State.Intensity);
		}
	}
	OriginalLightStates.Empty();

	if (PreviewLight)
	{
		PreviewLight->Destroy();
		PreviewLight = nullptr;
	}
}

void UQuickSDFEditorMode::OnPreSaveWorld(UWorld* InWorld, FObjectPreSaveContext InContext)
{
	if (InWorld == GetWorld())
	{
		RestoreLights();
	}
}

void UQuickSDFEditorMode::OnPostSaveWorld(UWorld* InWorld, FObjectPostSaveContext InContext)
{
	if (InWorld == GetWorld())
	{
		UpdatePaintToolEnvironment();
	}
}

void UQuickSDFEditorMode::Tick(FEditorViewportClient* ViewportClient, float DeltaTime)
{
	if (CachedViewportViewState.bValid && ViewportClient == SuppressedViewportClient)
	{
		const bool bAnyArrowKeyDown = SuppressedViewport &&
			(SuppressedViewport->KeyState(EKeys::Left) ||
			 SuppressedViewport->KeyState(EKeys::Right) ||
			 SuppressedViewport->KeyState(EKeys::Up) ||
			 SuppressedViewport->KeyState(EKeys::Down));

		RestoreViewportViewState(ViewportClient);

		if (!bAnyArrowKeyDown)
		{
			EndViewportNavigationSuppression();
		}
	}

	UpdatePaintToolEnvironment();
}

TMap<FName, TArray<TSharedPtr<FUICommandInfo>>> UQuickSDFEditorMode::GetModeCommands() const
{
	return FQuickSDFEditorModeCommands::GetCommands();
}

void UQuickSDFEditorMode::BindCommands()
{
	Super::BindCommands();

	if (!Toolkit.IsValid())
	{
		return;
	}

	const FQuickSDFEditorModeCommands& Commands = FQuickSDFEditorModeCommands::Get();
	const TSharedRef<FUICommandList>& CommandList = Toolkit->GetToolkitCommands();
	CommandList->MapAction(
		Commands.PreviousFrame,
		FExecuteAction::CreateUObject(this, &UQuickSDFEditorMode::SelectRelativeFrame, -1),
		FCanExecuteAction::CreateUObject(this, &UQuickSDFEditorMode::CanSelectRelativeFrame));
	CommandList->MapAction(
		Commands.NextFrame,
		FExecuteAction::CreateUObject(this, &UQuickSDFEditorMode::SelectRelativeFrame, 1),
		FCanExecuteAction::CreateUObject(this, &UQuickSDFEditorMode::CanSelectRelativeFrame));
	CommandList->MapAction(
		Commands.Open2DCanvas,
		FExecuteAction::CreateStatic(&QuickSDFPaintCanvas::OpenTab));
}

bool UQuickSDFEditorMode::InputKey(FEditorViewportClient* ViewportClient, FViewport* Viewport, FKey Key, EInputEvent Event)
{
	if (bSuppressBrushResizeMouseButtonRelease &&
		Event == IE_Released &&
		(Key == EKeys::RightMouseButton || Key == EKeys::LeftMouseButton))
	{
		bSuppressBrushResizeMouseButtonRelease = false;
		return true;
	}

	UQuickSDFPaintTool* PaintTool = Cast<UQuickSDFPaintTool>(GetToolManager()->GetActiveTool(EToolSide::Left));
	if (PaintTool)
	{
		if (PaintTool->IsBrushResizeModeActive())
		{
			if (Event == IE_Pressed)
			{
				if (Key == EKeys::RightMouseButton || Key == EKeys::Escape)
				{
					PaintTool->CancelBrushResizeMode();
					if (Key == EKeys::RightMouseButton)
					{
						bSuppressBrushResizeMouseButtonRelease = true;
					}
					return true;
				}
				if (Key == EKeys::LeftMouseButton)
				{
					PaintTool->ConfirmBrushResizeMode();
					bSuppressBrushResizeMouseButtonRelease = true;
					return true;
				}
			}

			if (Key == EKeys::RightMouseButton ||
				Key == EKeys::LeftMouseButton ||
				Key == EKeys::Escape ||
				Key == EKeys::F)
			{
				return true;
			}
		}
	}

	if (Key == EKeys::F && (Event == IE_Pressed || Event == IE_Repeat))
	{
		const FModifierKeysState ModifierKeys = FSlateApplication::Get().GetModifierKeys();
		const bool bCtrlDown =
			ModifierKeys.IsControlDown() ||
			(Viewport && (Viewport->KeyState(EKeys::LeftControl) || Viewport->KeyState(EKeys::RightControl)));
		if (bCtrlDown && RequestBrushResizeFromHoveredViewport())
		{
			return true;
		}
		if (!bCtrlDown && FocusActiveBrush(ViewportClient, PaintTool))
		{
			return true;
		}
	}

	if (!IsArrowNavigationKey(Key))
	{
		return false;
	}

	if (Event == IE_Pressed || Event == IE_Repeat)
	{
		CacheViewportViewState(ViewportClient, Viewport);
	}

	if (Event == IE_Pressed || Event == IE_Repeat)
	{
		if (IsFrameNavigationKey(Key))
		{
			SelectRelativeFrame(Key == EKeys::Right ? 1 : -1);
		}
	}
	else if (Event == IE_Released)
	{
		RestoreViewportViewState(ViewportClient);
		EndViewportNavigationSuppression();
	}

	return true;
}

bool UQuickSDFEditorMode::IsArrowNavigationKey(FKey Key)
{
	return Key == EKeys::Left || Key == EKeys::Right || Key == EKeys::Up || Key == EKeys::Down;
}

bool UQuickSDFEditorMode::IsFrameNavigationKey(FKey Key)
{
	return Key == EKeys::Left || Key == EKeys::Right;
}

bool UQuickSDFEditorMode::FocusActiveBrush(FEditorViewportClient* ViewportClient, const UQuickSDFPaintTool* PaintTool) const
{
	if (!ViewportClient || !PaintTool)
	{
		return false;
	}

	FVector BrushFocusPosition = FVector::ZeroVector;
	float BrushFocusRadius = 0.0f;
	if (!PaintTool->TryGetBrushFocusTarget(BrushFocusPosition, BrushFocusRadius))
	{
		return false;
	}

	const FVector Extent(BrushFocusRadius);
	ViewportClient->FocusViewportOnBox(FBox(BrushFocusPosition - Extent, BrushFocusPosition + Extent));
	return true;
}

void UQuickSDFEditorMode::CacheViewportViewState(FEditorViewportClient* ViewportClient, FViewport* Viewport)
{
	if (!ViewportClient)
	{
		return;
	}

	if (CachedViewportViewState.bValid && SuppressedViewportClient == ViewportClient)
	{
		return;
	}

	SuppressedViewportClient = ViewportClient;
	SuppressedViewport = Viewport;
	CachedViewportViewState.Location = ViewportClient->GetViewLocation();
	CachedViewportViewState.Rotation = ViewportClient->GetViewRotation();
	CachedViewportViewState.LookAtLocation = ViewportClient->GetLookAtLocation();
	CachedViewportViewState.OrthoZoom = ViewportClient->GetOrthoZoom();
	CachedViewportViewState.bPerspective = ViewportClient->IsPerspective();
	CachedViewportViewState.bValid = true;
}

void UQuickSDFEditorMode::RestoreViewportViewState(FEditorViewportClient* ViewportClient)
{
	if (!CachedViewportViewState.bValid || ViewportClient != SuppressedViewportClient || !ViewportClient)
	{
		return;
	}

	ViewportClient->SetViewLocation(CachedViewportViewState.Location);
	ViewportClient->SetViewRotation(CachedViewportViewState.Rotation);
	ViewportClient->SetLookAtLocation(CachedViewportViewState.LookAtLocation, false);
	if (!CachedViewportViewState.bPerspective && !FMath::IsNearlyZero(CachedViewportViewState.OrthoZoom))
	{
		ViewportClient->SetOrthoZoom(CachedViewportViewState.OrthoZoom);
	}
	if (FEditorCameraController* CameraController = ViewportClient->GetCameraController())
	{
		CameraController->ResetVelocity();
	}
	ViewportClient->Invalidate(false, false);
}

void UQuickSDFEditorMode::EndViewportNavigationSuppression()
{
	CachedViewportViewState = FViewportViewState();
	SuppressedViewportClient = nullptr;
	SuppressedViewport = nullptr;
}

bool UQuickSDFEditorMode::IsPaintToolActive() const
{
	UInteractiveToolManager* ToolManager = GetToolManager();
	return ToolManager && Cast<UQuickSDFPaintTool>(ToolManager->GetActiveTool(EToolSide::Left)) != nullptr;
}

void UQuickSDFEditorMode::UpdatePaintToolEnvironment()
{
	if (IsPaintToolActive())
	{
		MuteLights();
		if (!TimelineViewport.IsValid())
		{
			AttachTimelineToActiveViewport();
		}
		return;
	}

	DetachTimelineFromViewport();
	EndViewportNavigationSuppression();
	RestoreLights();
}

bool UQuickSDFEditorMode::CanSelectRelativeFrame() const
{
	UInteractiveToolManager* ToolManager = GetToolManager();
	const UQuickSDFPaintTool* PaintTool = ToolManager ? Cast<UQuickSDFPaintTool>(ToolManager->GetActiveTool(EToolSide::Left)) : nullptr;
	return PaintTool && PaintTool->Properties && PaintTool->Properties->TargetAngles.Num() > 1;
}

void UQuickSDFEditorMode::SelectRelativeFrame(int32 Direction)
{
	if (Direction == 0)
	{
		return;
	}

	UInteractiveToolManager* ToolManager = GetToolManager();
	UQuickSDFPaintTool* PaintTool = ToolManager ? Cast<UQuickSDFPaintTool>(ToolManager->GetActiveTool(EToolSide::Left)) : nullptr;
	UQuickSDFToolProperties* Properties = PaintTool ? PaintTool->Properties : nullptr;
	if (!Properties || Properties->TargetAngles.Num() == 0)
	{
		return;
	}

	const bool bFrontHalfAngles = Properties->UsesFrontHalfAngles();
	const float MaxAngle = bFrontHalfAngles ? 90.0f : 180.0f;
	TArray<int32> TimelineIndices;
	for (int32 Index = 0; Index < Properties->TargetAngles.Num(); ++Index)
	{
		if (!bFrontHalfAngles || Properties->TargetAngles[Index] <= MaxAngle)
		{
			TimelineIndices.Add(Index);
		}
	}

	if (TimelineIndices.Num() == 0)
	{
		return;
	}

	TimelineIndices.Sort([Properties](int32 A, int32 B)
	{
		return Properties->TargetAngles[A] < Properties->TargetAngles[B];
	});

	const int32 CurrentIndex = FMath::Clamp(Properties->EditAngleIndex, 0, Properties->TargetAngles.Num() - 1);
	const int32 CurrentPosition = TimelineIndices.IndexOfByKey(CurrentIndex);
	int32 NextPosition = INDEX_NONE;

	if (CurrentPosition != INDEX_NONE)
	{
		NextPosition = (CurrentPosition + (Direction > 0 ? 1 : -1) + TimelineIndices.Num()) % TimelineIndices.Num();
	}
	else
	{
		const float CurrentAngle = Properties->TargetAngles.IsValidIndex(CurrentIndex) ? Properties->TargetAngles[CurrentIndex] : 0.0f;
		if (Direction > 0)
		{
			NextPosition = 0;
			for (int32 Index = 0; Index < TimelineIndices.Num(); ++Index)
			{
				if (Properties->TargetAngles[TimelineIndices[Index]] > CurrentAngle)
				{
					NextPosition = Index;
					break;
				}
			}
		}
		else
		{
			NextPosition = TimelineIndices.Num() - 1;
			for (int32 Index = TimelineIndices.Num() - 1; Index >= 0; --Index)
			{
				if (Properties->TargetAngles[TimelineIndices[Index]] < CurrentAngle)
				{
					NextPosition = Index;
					break;
				}
			}
		}
	}

	if (!TimelineIndices.IsValidIndex(NextPosition))
	{
		return;
	}

	const int32 NextIndex = TimelineIndices[NextPosition];
	if (NextIndex == Properties->EditAngleIndex)
	{
		return;
	}

	Properties->EditAngleIndex = NextIndex;
	FProperty* Prop = Properties->GetClass()->FindPropertyByName(GET_MEMBER_NAME_CHECKED(UQuickSDFToolProperties, EditAngleIndex));
	PaintTool->OnPropertyModified(Properties, Prop);

	if (Properties->bAutoSyncLight && Properties->TargetAngles.IsValidIndex(NextIndex))
	{
		SetTimelineSeekAngle(Properties->TargetAngles[NextIndex]);
		SetPreviewLightAngle(Properties->GetMaterialAngleForKey(NextIndex), false);
	}

	if (GEditor)
	{
		GEditor->RedrawAllViewports(false);
	}
}

void UQuickSDFEditorMode::ActorSelectionChangeNotify()
{
	if (UQuickSDFToolSubsystem* QuickSDFToolSubsystem = GEditor->GetEditorSubsystem<UQuickSDFToolSubsystem>())
	{
		FToolBuilderState SelectionState;
		GetToolManager()->GetContextQueriesAPI()->GetCurrentSelectionState(SelectionState);

		UMeshComponent* TargetComp = nullptr;
		
		if (SelectionState.SelectedComponents.Num() > 0)
		{
			TargetComp = Cast<UMeshComponent>(SelectionState.SelectedComponents[0]);
		}
		
		if (TargetComp == nullptr && SelectionState.SelectedActors.Num() > 0)
		{
			if (AActor* SelectedActor = Cast<AActor>(SelectionState.SelectedActors[0]))
			{
				TargetComp = SelectedActor->FindComponentByClass<UMeshComponent>();
			}
		}
		
		if (TargetComp)
		{
			QuickSDFToolSubsystem->SetTargetComponent(TargetComp);
		}
		else
		{
			QuickSDFToolSubsystem->SetTargetComponent(nullptr);
		}
	}
}

bool UQuickSDFEditorMode::IsSelectionAllowed(AActor* InActor, bool bInSelection) const
{
	if (UInteractiveTool* ActiveTool = GetToolManager()->GetActiveTool(EToolSide::Left))
	{
		if (ActiveTool->IsA<UQuickSDFSelectTool>())
		{
			return true;
		}
	}
	return false;
}

void UQuickSDFEditorMode::CreateToolkit()
{
	Toolkit = MakeShared<FQuickSDFEditorModeToolkit>();
}

void UQuickSDFEditorMode::SetTimelineSeekAngle(float AzimuthAngle)
{
	if (TimelineWidget.IsValid())
	{
		TimelineWidget->SetSeekAngle(AzimuthAngle);
	}
}

void UQuickSDFEditorMode::SetPreviewLightAngle(float AzimuthAngle, bool bUpdateTimelineSeek)
{
	if (bUpdateTimelineSeek)
	{
		SetTimelineSeekAngle(AzimuthAngle);
	}

	if (!PreviewLight) return;

	UQuickSDFToolSubsystem* Subsystem = GEditor->GetEditorSubsystem<UQuickSDFToolSubsystem>();
	if (!Subsystem) return;

	UMeshComponent* MeshComp = Subsystem->GetTargetMeshComponent();
	if (!MeshComp)
	{
		// Fallback to absolute yaw if no mesh
		FRotator NewRotation(-45.0f, AzimuthAngle, 0.0f);
		PreviewLight->SetActorRotation(NewRotation);
		return;
	}

	// Map AzimuthAngle (0-180) back to direction relative to mesh
	// 0 = Left (ProjY=1), 90 = Front (ProjX=-1), 180 = Right (ProjY=-1)
	float Alpha = FMath::DegreesToRadians(AzimuthAngle - 90.0f);
	float ProjX = -FMath::Cos(Alpha);
	float ProjY = -FMath::Sin(Alpha);

	const FQuickSDFMeshBakeBasis BakeBasis = FQuickSDFMeshComponentAdapter::GetBakeBasisForComponent(MeshComp);
	const FVector MeshForward = BakeBasis.Forward;
	const FVector MeshRight = BakeBasis.Right;
	FVector HorizontalDir = ProjX * MeshForward + ProjY * MeshRight;

	// Preserve current pitch
	FRotator CurrentRot = PreviewLight->GetActorRotation();
	float PitchRad = FMath::DegreesToRadians(CurrentRot.Pitch);

	FVector FinalDir = HorizontalDir * FMath::Cos(PitchRad);
	FinalDir.Z = FMath::Sin(PitchRad);

	FRotator NewRot = FinalDir.Rotation();
	NewRot.Roll = 0.0f;
	PreviewLight->SetActorRotation(NewRot);
}
