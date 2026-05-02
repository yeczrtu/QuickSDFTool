#include "QuickSDFPaintTool.h"
#include "QuickSDFPaintToolPrivate.h"
#include "QuickSDFMeshComponentAdapter.h"
#include "QuickSDFToolSubsystem.h"
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

void UQuickSDFPaintTool::FillOriginalShading(int32 AngleIndex)
{
	if (!Properties || !CurrentComponent.IsValid()) return;
	
	UQuickSDFToolSubsystem* Subsystem = GEditor->GetEditorSubsystem<UQuickSDFToolSubsystem>();
	if (!Subsystem || !Subsystem->GetActiveSDFAsset()) return;

	UQuickSDFAsset* Asset = Subsystem->GetActiveSDFAsset();
	if (!Asset->GetActiveAngleDataList().IsValidIndex(AngleIndex)) return;

	UMaterialInterface* BaseMat = LoadObject<UMaterialInterface>(nullptr, TEXT("/QuickSDFTool/Materials/M_OriginalShading.M_OriginalShading"));
	if (!BaseMat) return;

	UMaterialInstanceDynamic* MID = UMaterialInstanceDynamic::Create(BaseMat, this);
	MID->SetScalarParameterValue(TEXT("Angle"), Properties->TargetAngles[AngleIndex]);

	FMaterialData MatData;
	MatData.Material = MID;
	MatData.PropertySizes.Add(MP_EmissiveColor, Properties->Resolution);
	MatData.PropertySizes.Add(MP_BaseColor, Properties->Resolution);
	MatData.BackgroundColor = FColor::Black;
	
	MatData.bPerformShrinking = false;
	MatData.bPerformBorderSmear = false;

	UE_LOG(LogTemp, Log, TEXT("Starting bake for angle %d at resolution %dx%d"), AngleIndex, Properties->Resolution.X, Properties->Resolution.Y);

	FMeshData MeshData;
	TUniquePtr<FQuickSDFMeshComponentAdapter> MeshAdapter = FQuickSDFMeshComponentAdapter::Make(CurrentComponent.Get());
	if (!MeshAdapter.IsValid())
	{
		UE_LOG(LogTemp, Warning, TEXT("FillOriginalShading: Unsupported mesh component type"));
		return;
	}

	if (UStaticMeshComponent* SMC = Cast<UStaticMeshComponent>(CurrentComponent.Get()))
	{
		MeshData.Mesh = SMC->GetStaticMesh();
		if (MeshData.Mesh)
		{
			MeshData.MeshDescription = MeshData.Mesh->GetMeshDescription(0);
		}
		
		MeshData.PrimitiveData = FPrimitiveData(SMC);
	}
	else if (USkeletalMeshComponent* SkMC = Cast<USkeletalMeshComponent>(CurrentComponent.Get()))
	{
		if (USkeletalMesh* SkeletalMesh = SkMC->GetSkeletalMeshAsset())
		{
			MeshData.MeshDescription = SkeletalMesh->HasMeshDescription(0) ? SkeletalMesh->GetMeshDescription(0) : nullptr;
		}

		MeshData.PrimitiveData = FPrimitiveData(SkMC);
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("FillOriginalShading: Unsupported mesh component type"));
		return;
	}

	if (!MeshData.MeshDescription)
	{
		UE_LOG(LogTemp, Warning, TEXT("FillOriginalShading: Target mesh has no LOD0 mesh description"));
		return;
	}

	MeshAdapter->GetMaterialSlots(MeshData.MaterialIndices, Properties->TargetMaterialSlot);
	if (MeshData.MaterialIndices.Num() == 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("FillOriginalShading: Target material slot %d is not valid for this component"), Properties->TargetMaterialSlot);
		return;
	}
	
	MeshData.TextureCoordinateIndex = Properties->UVChannel;
	MeshData.TextureCoordinateBox = FBox2D(FVector2D(0, 0), FVector2D(1, 1));

	TArray<FBakeOutput> BakeOutputs;
	IMaterialBakingModule& Module = FModuleManager::GetModuleChecked<IMaterialBakingModule>("MaterialBaking");
	
	FScopedSlowTask SlowTask(1.0f, LOCTEXT("BakingShading", "Baking Original Shading..."));
	SlowTask.MakeDialog();

	TArray<FMaterialData*> MaterialSettings;
	MaterialSettings.Add(&MatData);
	TArray<FMeshData*> MeshSettings;
	MeshSettings.Add(&MeshData);

	// 繝吶う繧ｯ螳溯｡・
	Module.BakeMaterials(MaterialSettings, MeshSettings, BakeOutputs);

	if (BakeOutputs.Num() > 0)
	{
		TArray<FColor> FinalPixels;
		bool bGotPixels = false;

		// Emissive 繧偵メ繧ｧ繝・け (LDR)
		if (BakeOutputs[0].PropertyData.Contains(MP_EmissiveColor) && BakeOutputs[0].PropertyData[MP_EmissiveColor].Num() > 1)
		{
			FinalPixels = BakeOutputs[0].PropertyData[MP_EmissiveColor];
			bGotPixels = true;
		}
		// BaseColor 繧偵メ繧ｧ繝・け (LDR)
		else if (BakeOutputs[0].PropertyData.Contains(MP_BaseColor) && BakeOutputs[0].PropertyData[MP_BaseColor].Num() > 1)
		{
			FinalPixels = BakeOutputs[0].PropertyData[MP_BaseColor];
			bGotPixels = true;
		}

		if (bGotPixels)
		{
			UE_LOG(LogTemp, Log, TEXT("Bake successful for angle %d, pixels: %d"), AngleIndex, FinalPixels.Num());
			ApplyPixelsWithUndo(AngleIndex, FinalPixels, LOCTEXT("RebakeQuickSDFMask", "Rebake Quick SDF Mask"));
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("Bake produced no pixels (or 1x1) for angle %d"), AngleIndex);
		}
	}
}

void UQuickSDFPaintTool::FillOriginalShadingAll()
{
	if (!Properties) return;
	
	UQuickSDFToolSubsystem* Subsystem = GEditor->GetEditorSubsystem<UQuickSDFToolSubsystem>();
	if (!Subsystem || !Subsystem->GetActiveSDFAsset()) return;

	UQuickSDFAsset* Asset = Subsystem->GetActiveSDFAsset();
	
	FScopedSlowTask SlowTask(Asset->GetActiveAngleDataList().Num(), LOCTEXT("BakingAllShading", "Baking All Original Shading..."));
	SlowTask.MakeDialog();

	for (int32 i = 0; i < Asset->GetActiveAngleDataList().Num(); ++i)
	{
		SlowTask.EnterProgressFrame(1.0f);
		FillOriginalShading(i);
	}
}
#undef LOCTEXT_NAMESPACE
