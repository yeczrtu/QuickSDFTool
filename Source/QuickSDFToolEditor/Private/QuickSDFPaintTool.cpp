#include "QuickSDFPaintTool.h"
#include "QuickSDFPaintToolPrivate.h"
#include "QuickSDFMeshComponentAdapter.h"
#include "QuickSDFToolSubsystem.h"
#include "QuickSDFToolUI.h"
#include "QuickSDFAsset.h"
#include "SDFProcessor.h"
#include "BaseGizmos/BrushStampIndicator.h"
#include "InteractiveToolManager.h"
#include "ToolBuilderUtil.h"
#include "EngineUtils.h"
#include "Engine/DirectionalLight.h"
#include "CollisionQueryParams.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/Texture2D.h"
#include "BaseBehaviors/ClickDragBehavior.h"
#include "TargetInterfaces/PrimitiveComponentBackedTarget.h"
#include "TargetInterfaces/MeshDescriptionProvider.h"
#include "DynamicMesh/MeshTransforms.h"
#include "DynamicMesh/DynamicMeshAttributeSet.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Components/PrimitiveComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/SkinnedMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Kismet/KismetRenderingLibrary.h"
#include "Intersection/IntrRay3Triangle3.h"
#include "Spatial/SpatialInterfaces.h"
#include "IndexTypes.h"
#include "Engine/Canvas.h"
#include "CanvasItem.h"
#include "CanvasTypes.h"
#include "TextureResource.h"
#include "RenderResource.h"
#include "Math/UnrealMathUtility.h"
#include "InputCoreTypes.h"
#include "HAL/PlatformApplicationMisc.h"
#include "HAL/PlatformTime.h"
#include "InteractiveToolChange.h"
#include "Misc/ScopedSlowTask.h"
#include "Misc/MessageDialog.h"
#include "DesktopPlatformModule.h"
#include "Engine/Selection.h"
#include "Framework/Application/SlateApplication.h"
#include "IDesktopPlatform.h"
#include "Misc/DefaultValueHelper.h"
#include "Containers/Ticker.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetToolsModule.h"
#include "IAssetTools.h"
#include "Misc/PackageName.h"
#include "ObjectTools.h"

#if WITH_EDITOR
#include "Editor.h"
#include "IMaterialBakingModule.h"
#include "MaterialBakingStructures.h"
#endif

#define LOCTEXT_NAMESPACE "QuickSDFPaintTool"

using namespace QuickSDFPaintToolPrivate;

void UQuickSDFBrushResizeInputBehavior::Initialize(UQuickSDFPaintTool* InTool)
{
	BrushTool = InTool;
}

EInputDevices UQuickSDFBrushResizeInputBehavior::GetSupportedDevices()
{
	return EInputDevices::Keyboard;
}

bool UQuickSDFBrushResizeInputBehavior::IsPressed(const FInputDeviceState& Input)
{
	if (!Input.IsFromDevice(EInputDevices::Keyboard))
	{
		return false;
	}

	return Input.Keyboard.ActiveKey.Button == EKeys::F && Input.Keyboard.ActiveKey.bDown && FInputDeviceState::IsCtrlKeyDown(Input);
}

bool UQuickSDFBrushResizeInputBehavior::IsReleased(const FInputDeviceState& Input)
{
	return false;
}

FInputCaptureRequest UQuickSDFBrushResizeInputBehavior::WantsCapture(const FInputDeviceState& Input)
{
	return IsPressed(Input) ? FInputCaptureRequest::Begin(this, EInputCaptureSide::Any, 0.0f) : FInputCaptureRequest::Ignore();
}

FInputCaptureUpdate UQuickSDFBrushResizeInputBehavior::BeginCapture(const FInputDeviceState& Input, EInputCaptureSide Side)
{
	if (BrushTool)
	{
		BrushTool->BeginBrushResizeMode();
	}

	return FInputCaptureUpdate::End();
}

FInputCaptureUpdate UQuickSDFBrushResizeInputBehavior::UpdateCapture(const FInputDeviceState& Input, const FInputCaptureData& Data)
{
	return FInputCaptureUpdate::End();
}

void UQuickSDFBrushResizeInputBehavior::ForceEndCapture(const FInputCaptureData& Data)
{
}

UQuickSDFPaintTool::UQuickSDFPaintTool()
{
}

void UQuickSDFPaintTool::RegisterActions(FInteractiveToolActionSet& ActionSet)
{
	Super::RegisterActions(ActionSet);

	ActionSet.RegisterAction(
		this,
		QuickSDFPreviewActionIncreaseBrush,
		TEXT("QuickSDFIncreaseBrushRadius"),
		NSLOCTEXT("QuickSDFPaintTool", "IncreaseBrushShortcut", "Increase Brush"),
		NSLOCTEXT("QuickSDFPaintTool", "IncreaseBrushShortcutDesc", "Increase brush radius."),
		EModifierKey::Control,
		EKeys::F,
		[this]()
		{
			BeginBrushResizeMode();
		});

	ActionSet.RegisterAction(
		this,
		QuickSDFActionOpenToggleMenu,
		TEXT("QuickSDFOpenToggleMenu"),
		NSLOCTEXT("QuickSDFPaintTool", "OpenToggleMenuShortcut", "Quick Toggles"),
		NSLOCTEXT("QuickSDFPaintTool", "OpenToggleMenuShortcutDesc", "Open the Quick SDF paint toggle menu."),
		EModifierKey::Alt,
		EKeys::T,
		[this]()
		{
			TSharedPtr<SWindow> ParentWindow = FSlateApplication::Get().GetActiveTopLevelWindow();
			if (ParentWindow.IsValid())
			{
				QuickSDFToolUI::ShowQuickToggleMenu(
					ParentWindow.ToSharedRef(),
					FSlateApplication::Get().GetCursorPos(),
					[this]()
					{
						return this;
					});
			}
		});

	const TArray<FKey> ToggleKeys = {
		EKeys::Two,
		EKeys::Three,
		EKeys::Four,
		EKeys::Five,
		EKeys::Six,
		EKeys::Seven,
		EKeys::Eight,
	};

	ActionSet.RegisterAction(
		this,
		QuickSDFActionToggleBase,
		TEXT("QuickSDFCyclePaintTargetMode"),
		NSLOCTEXT("QuickSDFPaintTool", "CyclePaintTargetModeShortcut", "Paint Target Mode"),
		NSLOCTEXT("QuickSDFPaintTool", "CyclePaintTargetModeShortcutDesc", "Cycle the Quick SDF paint target mode."),
		EModifierKey::Alt,
		EKeys::One,
		[this]()
		{
			QuickSDFToolUI::CyclePaintTargetMode(this, Properties);
		});

	const TArray<EQuickSDFPaintToggle>& Toggles = QuickSDFToolUI::GetPaintToggles();
	for (int32 Index = 0; Index < Toggles.Num() && Index < ToggleKeys.Num(); ++Index)
	{
		const EQuickSDFPaintToggle Toggle = Toggles[Index];
		ActionSet.RegisterAction(
			this,
			QuickSDFActionToggleBase + Index + 1,
			FString::Printf(TEXT("QuickSDFToggle%d"), Index + 2),
			QuickSDFToolUI::GetToggleLabel(Toggle),
			QuickSDFToolUI::GetToggleDescription(Toggle),
			EModifierKey::Alt,
			ToggleKeys[Index],
			[this, Toggle]()
			{
				QuickSDFToolUI::ToggleValue(this, Properties, Toggle);
			});
	}
}

void UQuickSDFPaintTool::InitializeRenderTargets()
{

}

void UQuickSDFPaintTool::Setup()
{
	Super::Setup();

	if (BrushProperties)
	{
		BrushProperties->bSpecifyRadius = true;
		BrushProperties->bToolSupportsPressureSensitivity = true;
		RecalculateBrushRadius();
	}

	Properties = NewObject<UQuickSDFToolProperties>(this);
	Properties->SetFlags(RF_Transactional);
	AddToolPropertySource(Properties);

	if (UQuickSDFToolSubsystem* Subsystem = GEditor->GetEditorSubsystem<UQuickSDFToolSubsystem>())
	{
		if (UMeshComponent* TargetComponent = Subsystem->GetTargetMeshComponent())
		{
			Subsystem->SetTargetComponent(TargetComponent);
		}

		// サブシステムにアセットがない場合は仮で新規作成
		if (!Subsystem->GetActiveSDFAsset())
		{
			UQuickSDFAsset* NewAsset = NewObject<UQuickSDFAsset>(Subsystem);
			NewAsset->SetFlags(RF_Transactional);
			Subsystem->SetActiveSDFAsset(NewAsset);
		}

		UQuickSDFAsset* ActiveAsset = Subsystem->GetActiveSDFAsset();
		ActiveAsset->SetFlags(RF_Transactional);
		if (ActiveAsset->AngleDataList.Num() == 0)
		{
			ActiveAsset->Resolution = FIntPoint(1024, 1024);
			ActiveAsset->UVChannel = 0;
			float InitialMaxAngle = Properties->bSymmetryMode ? 90.0f : 180.0f;
			for (int32 i = 0; i < 8; ++i)
			{
				FQuickSDFAngleData Data;
				Data.Angle = (i / 7.0f) * InitialMaxAngle;
				Data.MaskGuid = FGuid::NewGuid();
				ActiveAsset->AngleDataList.Add(Data);
			}
		}
		EnsureMaskGuids(ActiveAsset);
		ActiveAsset->InitializeRenderTargets(GetToolManager()->GetContextQueriesAPI()->GetCurrentEditingWorld());

		// アセットのデータをツールのプロパティ(UI)に同期させる
		Properties->TargetAsset = ActiveAsset;
		Properties->Resolution = ActiveAsset->Resolution;
		Properties->UVChannel = ActiveAsset->UVChannel;
		Properties->NumAngles = ActiveAsset->AngleDataList.Num();
		
		Properties->TargetAngles.SetNum(Properties->NumAngles);
		Properties->TargetTextures.SetNum(Properties->NumAngles);
		for (int32 i = 0; i < Properties->NumAngles; ++i)
		{
			Properties->TargetAngles[i] = ActiveAsset->AngleDataList[i].Angle;
			Properties->TargetTextures[i] = ActiveAsset->AngleDataList[i].TextureMask;
		}
		ChangeTargetComponent(Subsystem->GetTargetMeshComponent());
	}

	BuildBrushMaskTexture();
	ResetStrokeState();

	BrushResizeBehavior = NewObject<UQuickSDFBrushResizeInputBehavior>(this);
	BrushResizeBehavior->Initialize(this);
	AddInputBehavior(BrushResizeBehavior);
}

void UQuickSDFPaintTool::OnTick(float DeltaTime)
{
	Super::OnTick(DeltaTime);
	
	if (UQuickSDFToolSubsystem* Subsystem = GEditor->GetEditorSubsystem<UQuickSDFToolSubsystem>())
	{
		if (CurrentComponent.Get() != Subsystem->GetTargetMeshComponent())
		{
			ChangeTargetComponent(Subsystem->GetTargetMeshComponent());
		}

		// Ensure render targets are initialized (critical after Undo/Redo since RTs are Transient)
		if (UQuickSDFAsset* ActiveAsset = Subsystem->GetActiveSDFAsset())
		{
			bool bAnyMissingRT = false;
			for (const FQuickSDFAngleData& Data : ActiveAsset->AngleDataList)
			{
				if (!Data.PaintRenderTarget)
				{
					bAnyMissingRT = true;
					break;
				}
			}
			if (bAnyMissingRT)
			{
				ActiveAsset->InitializeRenderTargets(GetToolManager()->GetContextQueriesAPI()->GetCurrentEditingWorld());
				MarkMasksChanged();
			}
		}
	}

	TryActivateQuickLine();
}

void UQuickSDFPaintTool::ChangeTargetComponent(UMeshComponent* NewComponent)
{
	if (CurrentComponent.Get() == NewComponent)
	{
		return;
	}

	// 以前のコンポーネントのマテリアルを復元
	if (CurrentComponent.IsValid())
	{
		if (UStaticMeshComponent* StaticMeshComponent = Cast<UStaticMeshComponent>(CurrentComponent.Get()))
		{
			StaticMeshComponent->SetMaterialPreview(INDEX_NONE);
		}
		else if (USkinnedMeshComponent* SkinnedMeshComponent = Cast<USkinnedMeshComponent>(CurrentComponent.Get()))
		{
			SkinnedMeshComponent->SetMaterialPreview(INDEX_NONE);
		}

		for (int32 i = 0; i < OriginalMaterials.Num(); ++i)
		{
			if (CurrentComponent->GetNumMaterials() > i)
			{
				CurrentComponent->SetMaterial(i, OriginalMaterials[i]);
			}
		}
	}

	if (PreviewBaseMaterial)
	{
		PreviewBaseMaterial->GetOutermost()->SetDirtyFlag(false);
	}
	PreviewMaterial = nullptr;
	PreviewBaseMaterial = nullptr;
	OriginalMaterials.Empty();
	CurrentComponent = NewComponent;
	if (UQuickSDFToolSubsystem* Subsystem = GEditor->GetEditorSubsystem<UQuickSDFToolSubsystem>())
	{
		Subsystem->SetTargetComponent(NewComponent);
		SyncPropertiesFromActiveAsset();
	}
	TargetMeshSpatial.Reset();
	TargetMesh.Reset();
	TargetTriangleMaterialSlots.Reset();
	InvalidateUVOverlayCache();
	ResetStrokeState();

	if (!CurrentComponent.IsValid())
	{
		return;
	}

	TSharedPtr<UE::Geometry::FDynamicMesh3> TempMesh = MakeShared<UE::Geometry::FDynamicMesh3>();
	TUniquePtr<FQuickSDFMeshComponentAdapter> MeshAdapter = FQuickSDFMeshComponentAdapter::Make(CurrentComponent.Get());
	const bool bValidMeshLoaded = MeshAdapter.IsValid() && MeshAdapter->BuildDynamicMesh(*TempMesh, TargetTriangleMaterialSlots);

	if (!bValidMeshLoaded || TempMesh->TriangleCount() <= 0)
	{
		CurrentComponent.Reset();
		return;
	}

	TargetMesh = TempMesh;
	TargetMeshSpatial = MakeShared<UE::Geometry::FDynamicMeshAABBTree3>();
	TargetMeshSpatial->SetMesh(TargetMesh.Get(), true);

	PreviewBaseMaterial = LoadObject<UMaterialInterface>(nullptr, TEXT("/QuickSDFTool/Materials/M_PreviewMat.M_PreviewMat"));
	PreviewMaterial = PreviewBaseMaterial
		? UMaterialInstanceDynamic::Create(PreviewBaseMaterial, GetTransientPackage())
		: nullptr;

	if (!PreviewMaterial)
	{
		return;
	}
	PreviewMaterial->SetFlags(RF_Transient);
	if (PreviewBaseMaterial)
	{
		PreviewBaseMaterial->GetOutermost()->SetDirtyFlag(false);
	}

	// 新しいコンポーネントのマテリアルをプレビュー用に差し替え
	for (int32 i = 0; i < CurrentComponent->GetNumMaterials(); ++i)
	{
		OriginalMaterials.Add(CurrentComponent->GetMaterial(i));
		CurrentComponent->SetMaterial(i, PreviewMaterial);
	}

	ApplyTargetMaterialSlotIsolation();
	RefreshPreviewMaterial();
	EnsureInitialMasksReady();
}

void UQuickSDFPaintTool::Shutdown(EToolShutdownType ShutdownType)
{
	if (bAdjustingBrushRadius)
	{
		EndBrushResizeMode();
	}
	else if (bBrushResizeTransactionOpen)
	{
		GetToolManager()->EndUndoTransaction();
		bBrushResizeTransactionOpen = false;
	}
	
	ChangeTargetComponent(nullptr);
	
	Super::Shutdown(ShutdownType); // ツール終了処理
}

bool UQuickSDFPaintTool::IsTriangleInTargetMaterialSlot(int32 TriangleID) const
{
	if (!Properties || Properties->TargetMaterialSlot < 0)
	{
		return true;
	}

	const int32* TriangleMaterialSlot = TargetTriangleMaterialSlots.Find(TriangleID);
	return TriangleMaterialSlot && *TriangleMaterialSlot == Properties->TargetMaterialSlot;
}

void UQuickSDFPaintTool::ApplyTargetMaterialSlotIsolation()
{
	if (!CurrentComponent.IsValid())
	{
		return;
	}

	const int32 MaterialPreviewIndex =
		(Properties && Properties->bIsolateTargetMaterialSlot &&
			Properties->TargetMaterialSlot >= 0 && CurrentComponent->GetNumMaterials() > Properties->TargetMaterialSlot)
			? Properties->TargetMaterialSlot
			: INDEX_NONE;

	if (UStaticMeshComponent* StaticMeshComponent = Cast<UStaticMeshComponent>(CurrentComponent.Get()))
	{
		StaticMeshComponent->SetMaterialPreview(MaterialPreviewIndex);
	}
	else if (USkinnedMeshComponent* SkinnedMeshComponent = Cast<USkinnedMeshComponent>(CurrentComponent.Get()))
	{
		SkinnedMeshComponent->SetMaterialPreview(MaterialPreviewIndex);
	}
}
#undef LOCTEXT_NAMESPACE
