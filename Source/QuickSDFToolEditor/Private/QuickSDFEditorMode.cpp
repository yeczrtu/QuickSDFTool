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
#include "Widgets/SViewport.h"

const FEditorModeID UQuickSDFEditorMode::EM_QuickSDFEditorModeId = TEXT("EM_QuickSDFEditorMode");

namespace
{
class FQuickSDFBrushResizeInputPreProcessor final : public IInputProcessor
{
public:
	explicit FQuickSDFBrushResizeInputPreProcessor(UQuickSDFEditorMode* InMode)
		: Mode(InMode)
	{
	}

	virtual void Tick(const float DeltaTime, FSlateApplication& SlateApp, TSharedRef<ICursor> Cursor) override
	{
	}

	virtual bool HandleKeyDownEvent(FSlateApplication& SlateApp, const FKeyEvent& InKeyEvent) override
	{
		if (InKeyEvent.GetKey() != EKeys::F || !InKeyEvent.IsControlDown())
		{
			return false;
		}

		UQuickSDFEditorMode* ModePtr = Mode.Get();
		return ModePtr ? ModePtr->RequestBrushResizeFromHoveredViewport() : false;
	}

	virtual const TCHAR* GetDebugName() const override
	{
		return TEXT("QuickSDFBrushResizeInputPreProcessor");
	}

private:
	TWeakObjectPtr<UQuickSDFEditorMode> Mode;
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
	
	GetInteractiveToolsContext()->StartTool(TEXT("QuickSDFPaintTool"));
	GetToolManager()->ConfigureChangeTrackingMode(EToolChangeTrackingMode::NoChangeTracking);
	Toolkit->SetCurrentPalette(FName(TEXT("Default")));

	AttachTimelineToActiveViewport();
	if (!BrushResizeInputPreProcessor.IsValid())
	{
		BrushResizeInputPreProcessor = MakeShared<FQuickSDFBrushResizeInputPreProcessor>(this);
		FSlateApplication::Get().RegisterInputPreProcessor(BrushResizeInputPreProcessor);
	}

	MuteLights();

	// Register save delegates to prevent permanent lighting changes
	FEditorDelegates::PreSaveWorldWithContext.AddUObject(this, &UQuickSDFEditorMode::OnPreSaveWorld);
	FEditorDelegates::PostSaveWorldWithContext.AddUObject(this, &UQuickSDFEditorMode::OnPostSaveWorld);

	// Auto-select target if something is already selected
	ActorSelectionChangeNotify();
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
			const FVector2D CursorPosition = FSlateApplication::Get().GetCursorPos();
			bCursorOverViewport = ViewportWidget->GetTickSpaceGeometry().IsUnderLocation(CursorPosition);
		}
	}

	if (!bCursorOverViewport)
	{
		return false;
	}

	UInteractiveToolManager* ToolManager = GetToolManager();
	if (UQuickSDFPaintTool* PaintTool = ToolManager ? Cast<UQuickSDFPaintTool>(ToolManager->GetActiveTool(EToolSide::Mouse)) : nullptr)
	{
		PaintTool->RequestBrushResizeMode();
		return true;
	}

	return false;
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
		MuteLights();
		DetachTimelineFromViewport();
		AttachTimelineToActiveViewport();
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

	if (!TimelineViewport.IsValid())
	{
		AttachTimelineToActiveViewport();
	}
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
}

bool UQuickSDFEditorMode::InputKey(FEditorViewportClient* ViewportClient, FViewport* Viewport, FKey Key, EInputEvent Event)
{
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

bool UQuickSDFEditorMode::CanSelectRelativeFrame() const
{
	UInteractiveToolManager* ToolManager = GetToolManager();
	const UQuickSDFPaintTool* PaintTool = ToolManager ? Cast<UQuickSDFPaintTool>(ToolManager->GetActiveTool(EToolSide::Mouse)) : nullptr;
	return PaintTool && PaintTool->Properties && PaintTool->Properties->TargetAngles.Num() > 1;
}

void UQuickSDFEditorMode::SelectRelativeFrame(int32 Direction)
{
	if (Direction == 0)
	{
		return;
	}

	UInteractiveToolManager* ToolManager = GetToolManager();
	UQuickSDFPaintTool* PaintTool = ToolManager ? Cast<UQuickSDFPaintTool>(ToolManager->GetActiveTool(EToolSide::Mouse)) : nullptr;
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
		SetPreviewLightAngle(Properties->TargetAngles[NextIndex]);
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
	}
}

bool UQuickSDFEditorMode::IsSelectionAllowed(AActor* InActor, bool bInSelection) const
{
	if (UInteractiveTool* ActiveTool = GetToolManager()->GetActiveTool(EToolSide::Mouse))
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

void UQuickSDFEditorMode::SetPreviewLightAngle(float AzimuthAngle)
{
	SetTimelineSeekAngle(AzimuthAngle);

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
