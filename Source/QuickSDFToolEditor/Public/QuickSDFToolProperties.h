#pragma once

#include "CoreMinimal.h"
#include "InteractiveTool.h"
#include "QuickSDFToolTypes.h"
#include "QuickSDFToolProperties.generated.h"

class UQuickSDFAsset;
class UTexture2D;

UENUM(BlueprintType)
enum class EQuickSDFQualityPreset : uint8
{
	Draft512 UMETA(DisplayName = "Draft 512"),
	Standard1024 UMETA(DisplayName = "Standard 1024"),
	High2048 UMETA(DisplayName = "High 2048"),
	Ultra4096 UMETA(DisplayName = "Ultra 4096")
};

UENUM(BlueprintType)
enum class EQuickSDFPaintTargetMode : uint8
{
	CurrentOnly UMETA(DisplayName = "Current Only"),
	All UMETA(DisplayName = "All Textures"),
	BeforeCurrent UMETA(DisplayName = "Before Current"),
	AfterCurrent UMETA(DisplayName = "After Current")
};

UCLASS()
class UQuickSDFToolProperties : public UInteractiveToolPropertySet
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, Category = "Quick Start", meta = (DisplayName = "Quality"))
	EQuickSDFQualityPreset QualityPreset = EQuickSDFQualityPreset::Standard1024;

	UPROPERTY(EditAnywhere, Category = "Quick Start", meta = (DisplayName = "Refine Masks", HideInDetailPanel))
	bool bRefineMasks = false;

	UPROPERTY(EditAnywhere, Category = "Asset Settings")
	UQuickSDFAsset* TargetAsset = nullptr;

	UPROPERTY(EditAnywhere, Category = "Asset Settings", meta = (DisplayName = "QuickSDF Asset Folder", ContentDir))
	FString QuickSDFAssetFolder = TEXT("/Game/QuickSDF_Assets");

	UPROPERTY(EditAnywhere, Category = "Asset Settings", meta = (DisplayName = "QuickSDF Asset Name"))
	FString QuickSDFAssetName = TEXT("DA_QuickSDF");

	UPROPERTY(EditAnywhere, Category = "Asset Settings", meta = (DisplayName = "Save Mask Textures With Asset"))
	bool bSaveMaskTexturesWithAsset = true;

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

	UPROPERTY(EditAnywhere, Category = "Paint Settings", meta = (DisplayName = "Paint Target Mode"))
	EQuickSDFPaintTargetMode PaintTargetMode = EQuickSDFPaintTargetMode::CurrentOnly;

	UPROPERTY(EditAnywhere, Category = "Paint Settings", meta = (DisplayName = "Paint All Textures", HideInDetailPanel))
	bool bPaintAllAngles = false;

	UPROPERTY(EditAnywhere, Category = "Paint Settings", meta = (DisplayName = "Enable Quick Stroke"))
	bool bEnableQuickLine = true;

	UPROPERTY(EditAnywhere, Category = "Paint Settings", meta = (DisplayName = "Quick Stroke Hold Time", ClampMin = "0.1", UIMin = "0.1", UIMax = "2.0"))
	float QuickLineHoldTime = 0.45f;

	UPROPERTY(EditAnywhere, Category = "Paint Settings", meta = (DisplayName = "Quick Stroke Move Tolerance", ClampMin = "1.0", UIMin = "1.0", UIMax = "32.0"))
	float QuickLineMoveTolerance = 6.0f;

	UPROPERTY(EditAnywhere, Category = "Paint Settings", meta = (DisplayName = "Enable Stroke Stabilizer"))
	bool bEnableStrokeStabilizer = true;

	UPROPERTY(EditAnywhere, Category = "Paint Settings", meta = (DisplayName = "Stroke Stabilizer Radius", ClampMin = "0.0", UIMin = "0.0", UIMax = "48.0"))
	float StrokeStabilizerRadius = 12.0f;

	UPROPERTY(EditAnywhere, Category = "Paint Settings", meta = (DisplayName = "Stroke Spacing", ClampMin = "0.02", UIMin = "0.02", UIMax = "0.25"))
	float StrokeSpacingRatio = 0.08f;

	UPROPERTY(EditAnywhere, Category = "Paint Settings", meta = (DisplayName = "Brush Edge Antialiasing"))
	bool bEnableBrushAntialiasing = true;

	UPROPERTY(EditAnywhere, Category = "Paint Settings", meta = (DisplayName = "Brush Edge AA Width", ClampMin = "0.25", UIMin = "0.25", UIMax = "3.0", EditCondition = "bEnableBrushAntialiasing"))
	float BrushAntialiasingWidth = 1.25f;

	UPROPERTY(EditAnywhere, Category = "Paint Settings")
	bool bEnableOnionSkin = false;

	UPROPERTY(EditAnywhere, Category = "Paint Settings")
	bool bSymmetryMode = true;

	UPROPERTY(EditAnywhere, Category = "Paint Settings", meta = (DisplayName = "Enable Monotonic Guard"))
	bool bEnableMonotonicGuard = false;

	UPROPERTY(EditAnywhere, Category = "Paint Settings", meta = (DisplayName = "Clip Direction", EditCondition = "bEnableMonotonicGuard"))
	EQuickSDFClipDirection ClipDirection = EQuickSDFClipDirection::Auto;

	UPROPERTY(EditAnywhere, Category = "Paint Settings", meta = (UIMin = "0.0", UIMax = "180.0"))
	TArray<float> TargetAngles;

	UPROPERTY(EditAnywhere, Category = "Paint Settings")
	TArray<UTexture2D*> TargetTextures;

	UPROPERTY(VisibleAnywhere, Category = "Texture Sets", meta = (DisplayName = "Active Texture Set"))
	int32 ActiveTextureSetIndex = 0;

	UPROPERTY(EditAnywhere, Category = "Target Settings")
	FIntPoint Resolution = FIntPoint(1024, 1024);

	UPROPERTY(EditAnywhere, Category = "Target Settings", meta = (DisplayName = "Target Material Slot", ClampMin = "-1", UIMin = "-1"))
	int32 TargetMaterialSlot = -1;

	UPROPERTY(EditAnywhere, Category = "Target Settings", meta = (DisplayName = "Isolate Target Material Slot", EditCondition = "TargetMaterialSlot >= 0"))
	bool bIsolateTargetMaterialSlot = true;

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

	UFUNCTION(CallInEditor, Category = "Actions", meta = (DisplayName = "Create Threshold Map"))
	void CreateQuickThresholdMap();

	UFUNCTION(CallInEditor, Category = "Actions", meta = (DisplayName = "Bake Selected Texture Set"))
	void BakeSelectedTextureSet();

	UFUNCTION(CallInEditor, Category = "Actions", meta = (DisplayName = "Bake Missing Texture Sets"))
	void BakeMissingTextureSets();

	UFUNCTION(CallInEditor, Category = "Actions", meta = (DisplayName = "Import Mask Assets"))
	void ImportEditedMasks();

	UFUNCTION(CallInEditor, Category = "Actions", meta = (DisplayName = "Overwrite Source Textures"))
	void OverwriteSourceTextures();

	UFUNCTION(CallInEditor, Category = "Actions", meta = (DisplayName = "Save QuickSDF Asset"))
	void SaveQuickSDFAsset();

	UFUNCTION(CallInEditor, Category = "Actions", meta = (DisplayName = "Fill Current White"))
	void FillCurrentMaskWhite();

	UFUNCTION(CallInEditor, Category = "Actions", meta = (DisplayName = "Fill Current Black"))
	void FillCurrentMaskBlack();

	UFUNCTION(CallInEditor, Category = "Actions", meta = (DisplayName = "Fill All White"))
	void FillAllMasksWhite();

	UFUNCTION(CallInEditor, Category = "Actions", meta = (DisplayName = "Fill All Black"))
	void FillAllMasksBlack();

	UFUNCTION(CallInEditor, Category = "Actions", meta = (DisplayName = "Complete Default Masks"))
	void CompleteToEightMasks();

	UFUNCTION(CallInEditor, Category = "Actions", meta = (DisplayName = "Redistribute Angles Evenly"))
	void RedistributeAnglesEvenly();

	UFUNCTION(CallInEditor, Category = "Actions", meta = (DisplayName = "Rebake Current"))
	void FillOriginalShadingToCurrentAngle();

	UFUNCTION(CallInEditor, Category = "Actions", meta = (DisplayName = "Rebake All"))
	void FillOriginalShadingToAllAngles();

	UFUNCTION(CallInEditor, Category = "Actions")
	void GenerateSDFThresholdMap();

	UFUNCTION(CallInEditor, Category = "Actions", meta = (DisplayName = "Generate All Baked Texture Sets"))
	void GenerateAllBakedTextureSets();

	UFUNCTION(CallInEditor, Category = "Actions", meta = (DisplayName = "Validate Monotonic Guard"))
	void ValidateMonotonicGuard();
};
