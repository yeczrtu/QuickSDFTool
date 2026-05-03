#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "UObject/NoExportTypes.h"
#include "Engine/Texture2D.h"
#include "QuickSDFAsset.generated.h"

UENUM(BlueprintType)
enum class EQuickSDFIslandMirrorTransform : uint8
{
	FlipU UMETA(DisplayName = "Flip U"),
	FlipV UMETA(DisplayName = "Flip V"),
	Rotate180 UMETA(DisplayName = "Rotate 180"),
	SwapUVFlipU UMETA(DisplayName = "Swap UV + Flip U"),
	SwapUVFlipV UMETA(DisplayName = "Swap UV + Flip V")
};

USTRUCT(BlueprintType)
struct FQuickSDFIslandMirrorPair
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Island Mirror")
	FString SourceIslandKey;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Island Mirror")
	FString TargetIslandKey;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Island Mirror")
	EQuickSDFIslandMirrorTransform Transform = EQuickSDFIslandMirrorTransform::FlipU;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Island Mirror")
	bool bEnabled = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Island Mirror")
	bool bUserLocked = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Island Mirror")
	float Confidence = 0.0f;
};

USTRUCT(BlueprintType)
struct FQuickSDFAngleData
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SDF")
	float Angle = 0.0f;

	UPROPERTY()
	FGuid MaskGuid;

	// Binarized shadow mask texture for this angle
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SDF")
	class UTexture2D* TextureMask = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SDF")
	bool bAllowSourceTextureOverwrite = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Transient, Category = "SDF")
	class UTextureRenderTarget2D* PaintRenderTarget = nullptr;
};

USTRUCT(BlueprintType)
struct FQuickSDFTextureSetData
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Texture Set")
	int32 MaterialSlotIndex = INDEX_NONE;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Texture Set")
	FName SlotName = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Texture Set")
	FString MaterialName;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Texture Set")
	int32 UVChannel = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Texture Set")
	FIntPoint Resolution = FIntPoint(1024, 1024);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SDF Data")
	TArray<FQuickSDFAngleData> AngleDataList;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "SDF Result")
	class UTexture2D* FinalSDFTexture = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Island Mirror")
	TArray<FQuickSDFIslandMirrorPair> IslandMirrorPairs;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Texture Set")
	bool bDirty = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Texture Set")
	bool bInitialBakeComplete = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Texture Set")
	bool bHasWarning = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Texture Set")
	FText WarningMessage;
};

/**
 * Data Asset to hold non-destructive data for QuickSDFTool
 */
UCLASS(BlueprintType, meta=(DisplayThumbnail="true"))
class QUICKSDFTOOL_API UQuickSDFAsset : public UDataAsset
{
	GENERATED_BODY()

public:
	UQuickSDFAsset();
	virtual void PostLoad() override;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SDF settings")
	FIntPoint Resolution;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SDF settings")
	int32 UVChannel;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Texture Sets")
	TArray<FQuickSDFTextureSetData> TextureSets;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Texture Sets")
	int32 ActiveTextureSetIndex = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SDF Data")
	TArray<FQuickSDFAngleData> AngleDataList;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "SDF Result")
	class UTexture2D* FinalSDFTexture;

	// Sets up render targets for painting
	void InitializeRenderTargets(class UWorld* InWorld);
	// Merges render targets back to standard textures
	void BakeToTextures();

	FQuickSDFTextureSetData* GetActiveTextureSet();
	const FQuickSDFTextureSetData* GetActiveTextureSet() const;
	FQuickSDFTextureSetData& EnsureActiveTextureSet();
	TArray<FQuickSDFAngleData>& GetActiveAngleDataList();
	const TArray<FQuickSDFAngleData>& GetActiveAngleDataList() const;
	FIntPoint& GetActiveResolution();
	const FIntPoint& GetActiveResolution() const;
	int32& GetActiveUVChannel();
	const int32& GetActiveUVChannel() const;
	UTexture2D*& GetActiveFinalSDFTexture();
	UTexture2D* GetActiveFinalSDFTexture() const;
	bool SetActiveTextureSetIndex(int32 NewIndex);
	void MigrateLegacyDataToTextureSetsIfNeeded();
	void SyncActiveTextureSetFromLegacy();
	void SyncLegacyFromActiveTextureSet();
};
