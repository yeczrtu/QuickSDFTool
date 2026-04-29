#pragma once

#include "CoreMinimal.h"
#include "InteractiveTool.h"
#include "QuickSDFToolProperties.generated.h"

class UQuickSDFAsset;
class UTexture2D;

UCLASS()
class UQuickSDFToolProperties : public UInteractiveToolPropertySet
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, Category = "Asset Settings")
	UQuickSDFAsset* TargetAsset = nullptr;

	UPROPERTY(EditAnywhere, Category = "Paint Settings")
	int32 EditAngleIndex = 0;

	UPROPERTY(EditAnywhere, Category = "Paint Settings", meta = (ClampMin = "1", UIMin = "1"))
	int32 NumAngles = 8;

	UPROPERTY(EditAnywhere, Category = "Paint Settings")
	int32 UVChannel = 0;

	UPROPERTY(EditAnywhere, Category = "Paint Settings")
	bool bShowPreview = true;

	UPROPERTY(EditAnywhere, Category = "Paint Settings")
	bool bOverlayOriginalShadow = false;

	UPROPERTY(EditAnywhere, Category = "Paint Settings")
	bool bOverlayUV = true;

	UPROPERTY(EditAnywhere, Category = "Paint Settings")
	bool bAutoSyncLight = true;

	UPROPERTY(EditAnywhere, Category = "Paint Settings", meta = (DisplayName = "Paint All Textures"))
	bool bPaintAllAngles = false;

	UPROPERTY(EditAnywhere, Category = "Paint Settings", meta = (DisplayName = "Enable Quick Stroke"))
	bool bEnableQuickLine = true;

	UPROPERTY(EditAnywhere, Category = "Paint Settings", meta = (DisplayName = "Quick Stroke Hold Time", ClampMin = "0.1", UIMin = "0.1", UIMax = "2.0"))
	float QuickLineHoldTime = 0.45f;

	UPROPERTY(EditAnywhere, Category = "Paint Settings", meta = (DisplayName = "Quick Stroke Move Tolerance", ClampMin = "1.0", UIMin = "1.0", UIMax = "32.0"))
	float QuickLineMoveTolerance = 6.0f;

	UPROPERTY(EditAnywhere, Category = "Paint Settings")
	bool bEnableOnionSkin = false;

	UPROPERTY(EditAnywhere, Category = "Paint Settings")
	bool bSymmetryMode = true;

	UPROPERTY(EditAnywhere, Category = "Paint Settings", meta = (UIMin = "0.0", UIMax = "180.0"))
	TArray<float> TargetAngles;

	UPROPERTY(EditAnywhere, Category = "Paint Settings")
	TArray<UTexture2D*> TargetTextures;

	UPROPERTY(EditAnywhere, Category = "Target Settings")
	FIntPoint Resolution = FIntPoint(1024, 1024);

	UPROPERTY(EditAnywhere, Category = "Target Settings", meta = (DisplayName = "Target Material Slot", ClampMin = "-1", UIMin = "-1"))
	int32 TargetMaterialSlot = -1;

	UPROPERTY(EditAnywhere, Category = "Target Settings", meta = (DisplayName = "Isolate Target Material Slot", EditCondition = "TargetMaterialSlot >= 0"))
	bool bIsolateTargetMaterialSlot = false;

	UPROPERTY(EditAnywhere, Category = "Export Settings", meta = (ClampMin = "1", UIMin = "1", ClampMax = "8", UIMax = "8"))
	int32 UpscaleFactor = 1;

	UPROPERTY(EditAnywhere, Category = "Export Settings", meta = (DisplayName = "SDF Output Folder", ContentDir))
	FString SDFOutputFolder = TEXT("/Game/QuickSDF_GENERATED");

	UPROPERTY(EditAnywhere, Category = "Export Settings", meta = (DisplayName = "SDF Texture Name"))
	FString SDFTextureName = TEXT("T_QuickSDF_ThresholdMap");

	UPROPERTY(EditAnywhere, Category = "Export Settings", meta = (DisplayName = "Overwrite Existing SDF Texture"))
	bool bOverwriteExistingSDF = false;

	UPROPERTY(EditAnywhere, Category = "Export Settings", meta = (DisplayName = "Mask Export Folder", ContentDir))
	FString MaskExportFolder = TEXT("/Game/QuickSDF_Exports");

	UPROPERTY(EditAnywhere, Category = "Export Settings", meta = (DisplayName = "Create Mask Folder Per Export"))
	bool bCreateMaskFolderPerExport = true;

	UPROPERTY(EditAnywhere, Category = "Export Settings", meta = (DisplayName = "Mask Export Folder Prefix"))
	FString MaskExportFolderPrefix = TEXT("Masks");

	UPROPERTY(EditAnywhere, Category = "Export Settings", meta = (DisplayName = "Mask Texture Prefix"))
	FString MaskTextureNamePrefix = TEXT("T_QuickSDF_Angle");

	UPROPERTY(EditAnywhere, Category = "Export Settings", meta = (DisplayName = "Overwrite Existing Mask Textures"))
	bool bOverwriteExistingMasks = false;

	UFUNCTION(CallInEditor, Category = "Actions")
	void ExportToTexture();

	UFUNCTION(CallInEditor, Category = "Actions", meta = (DisplayName = "Fill Original Shading (Current)"))
	void FillOriginalShadingToCurrentAngle();

	UFUNCTION(CallInEditor, Category = "Actions", meta = (DisplayName = "Fill Original Shading (All)"))
	void FillOriginalShadingToAllAngles();

	UFUNCTION(CallInEditor, Category = "Actions")
	void GenerateSDFThresholdMap();
};
