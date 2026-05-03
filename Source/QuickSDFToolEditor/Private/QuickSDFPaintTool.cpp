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
#include "UObject/Package.h"

#if WITH_EDITOR
#include "Editor.h"
#include "IMaterialBakingModule.h"
#include "MaterialBakingStructures.h"
#endif

#define LOCTEXT_NAMESPACE "QuickSDFPaintTool"

using namespace QuickSDFPaintToolPrivate;

namespace
{
const TCHAR* QuickSDFPreviewMaterialObjectPath = TEXT("/QuickSDFTool/Materials/M_PreviewMat.M_PreviewMat");
const TCHAR* QuickSDFPreviewMaterialPackagePath = TEXT("/QuickSDFTool/Materials/M_PreviewMat");
const TCHAR* QuickSDFPreviewOverlayMaterialObjectPath = TEXT("/QuickSDFTool/Materials/M_PreviewSceneColorOverlay.M_PreviewSceneColorOverlay");
const TCHAR* QuickSDFPreviewOverlayMaterialPackagePath = TEXT("/QuickSDFTool/Materials/M_PreviewSceneColorOverlay");
const TCHAR* QuickSDFSDFToonMaterialObjectPath = TEXT("/QuickSDFTool/Materials/M_SDFToon.M_SDFToon");
const TCHAR* QuickSDFSDFToonMaterialPackagePath = TEXT("/QuickSDFTool/Materials/M_SDFToon");

void ClearMaterialPackageDirtyState(UPackage* Package)
{
	if (!Package)
	{
		return;
	}

	if (Package->IsDirty())
	{
		Package->SetDirtyFlag(false);
	}
	Package->ClearDirtyFlag();
}
}


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

void UQuickSDFPaintTool::RequestBrushResizeMode()
{
	BeginBrushResizeMode();
}

void UQuickSDFPaintTool::ClearPreviewMaterialDirtyState() const
{
	UPackage* PreviewPackage = PreviewBaseMaterial ? PreviewBaseMaterial->GetOutermost() : FindPackage(nullptr, QuickSDFPreviewMaterialPackagePath);
	ClearMaterialPackageDirtyState(PreviewPackage);

	UPackage* PreviewOverlayPackage = PreviewOverlayBaseMaterial ? PreviewOverlayBaseMaterial->GetOutermost() : FindPackage(nullptr, QuickSDFPreviewOverlayMaterialPackagePath);
	ClearMaterialPackageDirtyState(PreviewOverlayPackage);

	UPackage* SDFToonPackage = SDFToonBaseMaterial ? SDFToonBaseMaterial->GetOutermost() : FindPackage(nullptr, QuickSDFSDFToonMaterialPackagePath);
	ClearMaterialPackageDirtyState(SDFToonPackage);
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
			RequestBrushResizeMode();
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
		EKeys::Nine,
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
		BrushProperties->BrushRadius = QuickSDFInitialBrushRadius;
		const float RangeSize = BrushRelativeSizeRange.Max - BrushRelativeSizeRange.Min;
		if (RangeSize > KINDA_SMALL_NUMBER)
		{
			BrushProperties->BrushSize = FMath::Clamp((QuickSDFInitialBrushRadius - BrushRelativeSizeRange.Min) / RangeSize, 0.0f, 1.0f);
		}
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

		// 繧ｵ繝悶す繧ｹ繝・Β縺ｫ繧｢繧ｻ繝・ヨ縺後↑縺・ｴ蜷医・莉ｮ縺ｧ譁ｰ隕丈ｽ懈・
		if (!Subsystem->GetActiveSDFAsset())
		{
			UQuickSDFAsset* NewAsset = NewObject<UQuickSDFAsset>(Subsystem);
			NewAsset->SetFlags(RF_Transactional);
			Subsystem->SetActiveSDFAsset(NewAsset);
		}

		UQuickSDFAsset* ActiveAsset = Subsystem->GetActiveSDFAsset();
		ActiveAsset->SetFlags(RF_Transactional);
		if (ActiveAsset->GetActiveAngleDataList().Num() == 0)
		{
			ActiveAsset->GetActiveResolution() = FIntPoint(1024, 1024);
			ActiveAsset->GetActiveUVChannel() = 0;
			const float InitialMaxAngle = Properties->bSymmetryMode ? 90.0f : 180.0f;
			const int32 InitialAngleCount = GetQuickSDFDefaultAngleCount(Properties->bSymmetryMode);
			for (int32 i = 0; i < InitialAngleCount; ++i)
			{
				FQuickSDFAngleData Data;
				Data.Angle = InitialAngleCount > 1
					? (static_cast<float>(i) / static_cast<float>(InitialAngleCount - 1)) * InitialMaxAngle
					: 0.0f;
				Data.MaskGuid = FGuid::NewGuid();
				ActiveAsset->GetActiveAngleDataList().Add(Data);
			}
		}
		EnsureMaskGuids(ActiveAsset);
		ActiveAsset->InitializeRenderTargets(GetToolManager()->GetContextQueriesAPI()->GetCurrentEditingWorld());

		// 繧｢繧ｻ繝・ヨ縺ｮ繝・・繧ｿ繧偵ヤ繝ｼ繝ｫ縺ｮ繝励Ο繝代ユ繧｣(UI)縺ｫ蜷梧悄縺輔○繧・
		Properties->TargetAsset = ActiveAsset;
		Properties->Resolution = ActiveAsset->GetActiveResolution();
		Properties->UVChannel = ActiveAsset->GetActiveUVChannel();
		Properties->NumAngles = ActiveAsset->GetActiveAngleDataList().Num();
		
		Properties->TargetAngles.SetNum(Properties->NumAngles);
		Properties->TargetTextures.SetNum(Properties->NumAngles);
		for (int32 i = 0; i < Properties->NumAngles; ++i)
		{
			Properties->TargetAngles[i] = ActiveAsset->GetActiveAngleDataList()[i].Angle;
			Properties->TargetTextures[i] = ActiveAsset->GetActiveAngleDataList()[i].TextureMask;
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

	if (bAdjustingBrushRadius)
	{
		UpdateBrushResizeFromCursor();
	}
	
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
			for (const FQuickSDFAngleData& Data : ActiveAsset->GetActiveAngleDataList())
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

	if (PreviewBaseMaterial)
	{
		ClearPreviewMaterialDirtyState();
	}

	TryActivateQuickLine();
}

void UQuickSDFPaintTool::RestoreOriginalComponentMaterialSlots()
{
	if (!CurrentComponent.IsValid())
	{
		return;
	}

	for (int32 i = 0; i < OriginalMaterials.Num(); ++i)
	{
		if (CurrentComponent->GetNumMaterials() > i)
		{
			CurrentComponent->SetMaterial(i, OriginalMaterials[i]);
		}
	}
}

void UQuickSDFPaintTool::RestoreOriginalComponentOverlayMaterial()
{
	if (!CurrentComponent.IsValid() || !bHasOriginalOverlayMaterialState)
	{
		return;
	}

	CurrentComponent->SetOverlayMaterial(OriginalOverlayMaterial);
	CurrentComponent->SetOverlayMaterialMaxDrawDistance(OriginalOverlayMaterialMaxDrawDistance);
}

void UQuickSDFPaintTool::RestoreOriginalComponentMaterials()
{
	if (!CurrentComponent.IsValid())
	{
		return;
	}

	if (UStaticMeshComponent* StaticMeshComponent = Cast<UStaticMeshComponent>(CurrentComponent.Get()))
	{
		StaticMeshComponent->SetMaterialPreview(INDEX_NONE);
	}
	else if (USkinnedMeshComponent* SkinnedMeshComponent = Cast<USkinnedMeshComponent>(CurrentComponent.Get()))
	{
		SkinnedMeshComponent->SetMaterialPreview(INDEX_NONE);
	}

	RestoreOriginalComponentMaterialSlots();
	RestoreOriginalComponentOverlayMaterial();
}

void UQuickSDFPaintTool::ApplyMaterialPreviewMode()
{
	if (!CurrentComponent.IsValid())
	{
		return;
	}

	const EQuickSDFMaterialPreviewMode PreviewMode = Properties
		? Properties->MaterialPreviewMode
		: EQuickSDFMaterialPreviewMode::OriginalMaterial;

	switch (PreviewMode)
	{
	case EQuickSDFMaterialPreviewMode::OriginalMaterial:
		RestoreOriginalComponentMaterialSlots();
		if (PreviewOverlayMaterial)
		{
			CurrentComponent->SetOverlayMaterial(PreviewOverlayMaterial);
			CurrentComponent->SetOverlayMaterialMaxDrawDistance(0.0f);
		}
		else
		{
			CurrentComponent->SetOverlayMaterial(nullptr);
			CurrentComponent->SetOverlayMaterialMaxDrawDistance(0.0f);
		}
		break;

	case EQuickSDFMaterialPreviewMode::Mask:
	case EQuickSDFMaterialPreviewMode::UV:
	case EQuickSDFMaterialPreviewMode::OriginalShadow:
		CurrentComponent->SetOverlayMaterial(nullptr);
		CurrentComponent->SetOverlayMaterialMaxDrawDistance(0.0f);
		if (PreviewMaterial)
		{
			for (int32 i = 0; i < CurrentComponent->GetNumMaterials(); ++i)
			{
				CurrentComponent->SetMaterial(i, PreviewMaterial);
			}
		}
		else
		{
			RestoreOriginalComponentMaterialSlots();
		}
		break;

	case EQuickSDFMaterialPreviewMode::GeneratedSDF:
		CurrentComponent->SetOverlayMaterial(nullptr);
		CurrentComponent->SetOverlayMaterialMaxDrawDistance(0.0f);
		RestoreOriginalComponentMaterialSlots();
		if (SDFToonPreviewMaterial && CanUseGeneratedSDFPreview())
		{
			const int32 TargetMaterialSlot = Properties ? Properties->TargetMaterialSlot : INDEX_NONE;
			if (TargetMaterialSlot >= 0 && TargetMaterialSlot < CurrentComponent->GetNumMaterials())
			{
				CurrentComponent->SetMaterial(TargetMaterialSlot, SDFToonPreviewMaterial);
			}
			else
			{
				for (int32 i = 0; i < CurrentComponent->GetNumMaterials(); ++i)
				{
					CurrentComponent->SetMaterial(i, SDFToonPreviewMaterial);
				}
			}
		}
		break;

	default:
		RestoreOriginalComponentMaterialSlots();
		RestoreOriginalComponentOverlayMaterial();
		break;
	}

	ApplyTargetMaterialSlotIsolation();
	ClearPreviewMaterialDirtyState();
}

void UQuickSDFPaintTool::ChangeTargetComponent(UMeshComponent* NewComponent)
{
	if (CurrentComponent.Get() == NewComponent)
	{
		return;
	}

	// 莉･蜑阪・繧ｳ繝ｳ繝昴・繝阪Φ繝医・繝槭ユ繝ｪ繧｢繝ｫ繧貞ｾｩ蜈・
	if (CurrentComponent.IsValid())
	{
		RestoreOriginalComponentMaterials();
	}

	ClearPreviewMaterialDirtyState();
	PreviewMaterial = nullptr;
	PreviewHUDMaterial = nullptr;
	PreviewBaseMaterial = nullptr;
	PreviewOverlayMaterial = nullptr;
	PreviewOverlayBaseMaterial = nullptr;
	SDFToonPreviewMaterial = nullptr;
	SDFToonBaseMaterial = nullptr;
	OriginalMaterials.Empty();
	OriginalOverlayMaterial = nullptr;
	OriginalOverlayMaterialMaxDrawDistance = 0.0f;
	bHasOriginalOverlayMaterialState = false;
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

	PreviewBaseMaterial = LoadObject<UMaterialInterface>(nullptr, QuickSDFPreviewMaterialObjectPath);
	PreviewMaterial = PreviewBaseMaterial
		? UMaterialInstanceDynamic::Create(PreviewBaseMaterial, GetTransientPackage())
		: nullptr;
	PreviewHUDMaterial = PreviewBaseMaterial
		? UMaterialInstanceDynamic::Create(PreviewBaseMaterial, GetTransientPackage())
		: nullptr;
	PreviewOverlayBaseMaterial = LoadObject<UMaterialInterface>(nullptr, QuickSDFPreviewOverlayMaterialObjectPath);
	PreviewOverlayMaterial = PreviewOverlayBaseMaterial
		? UMaterialInstanceDynamic::Create(PreviewOverlayBaseMaterial, GetTransientPackage())
		: nullptr;
	SDFToonBaseMaterial = LoadObject<UMaterialInterface>(nullptr, QuickSDFSDFToonMaterialObjectPath);
	SDFToonPreviewMaterial = SDFToonBaseMaterial
		? UMaterialInstanceDynamic::Create(SDFToonBaseMaterial, GetTransientPackage())
		: nullptr;

	if (PreviewMaterial)
	{
		PreviewMaterial->SetFlags(RF_Transient);
	}
	if (PreviewHUDMaterial)
	{
		PreviewHUDMaterial->SetFlags(RF_Transient);
	}
	if (PreviewOverlayMaterial)
	{
		PreviewOverlayMaterial->SetFlags(RF_Transient);
	}
	if (SDFToonPreviewMaterial)
	{
		SDFToonPreviewMaterial->SetFlags(RF_Transient);
	}
	ClearPreviewMaterialDirtyState();

	// 譁ｰ縺励＞繧ｳ繝ｳ繝昴・繝阪Φ繝医・繝槭ユ繝ｪ繧｢繝ｫ繧偵・繝ｬ繝薙Η繝ｼ逕ｨ縺ｫ蟾ｮ縺玲崛縺・
	for (int32 i = 0; i < CurrentComponent->GetNumMaterials(); ++i)
	{
		OriginalMaterials.Add(CurrentComponent->GetMaterial(i));
	}
	OriginalOverlayMaterial = CurrentComponent->GetOverlayMaterial();
	OriginalOverlayMaterialMaxDrawDistance = CurrentComponent->GetOverlayMaterialMaxDrawDistance();
	bHasOriginalOverlayMaterialState = true;
	ClearPreviewMaterialDirtyState();

	RefreshTextureSetsForCurrentComponent();
	EnsureInitialMasksReady();
	RefreshPreviewMaterial();
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
	ClearPreviewMaterialDirtyState();
	
	Super::Shutdown(ShutdownType); // 繝・・繝ｫ邨ゆｺ・・逅・
	ClearPreviewMaterialDirtyState();
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
