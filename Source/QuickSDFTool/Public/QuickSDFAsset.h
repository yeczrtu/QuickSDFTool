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

UENUM(BlueprintType)
enum class EQuickSDFAutoSymmetryResolvedMode : uint8
{
	Texture UMETA(DisplayName = "Texture"),
	Island UMETA(DisplayName = "Island")
};

UENUM(BlueprintType)
enum class EQuickSDFIntermediatePolarity : uint8
{
	Monopolar UMETA(DisplayName = "Monopolar"),
	Bipolar UMETA(DisplayName = "Bipolar")
};

UENUM(BlueprintType)
enum class EQuickSDFIntermediateSymmetryMode : uint8
{
	None180 UMETA(DisplayName = "Off"),
	WholeTextureFlip90 UMETA(DisplayName = "Texture Flip"),
	UVIslandChannelFlip90 UMETA(DisplayName = "UV Island Channel Flip")
};

UENUM(BlueprintType)
enum class EQuickSDFIntermediateLilToonLeftSource : uint8
{
	InternalY UMETA(DisplayName = "Internal Y"),
	InternalW UMETA(DisplayName = "Internal W"),
	MirroredX UMETA(DisplayName = "Mirrored X")
};

USTRUCT(BlueprintType)
struct FQuickSDFIntermediateMetadata
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Intermediate SDF")
	int32 SchemaVersion = 1;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Intermediate SDF")
	FIntPoint Resolution = FIntPoint::ZeroValue;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Intermediate SDF")
	int32 UVChannel = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Intermediate SDF")
	int32 UpscaleFactor = 1;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Intermediate SDF")
	EQuickSDFIntermediatePolarity Polarity = EQuickSDFIntermediatePolarity::Monopolar;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Intermediate SDF")
	EQuickSDFIntermediateSymmetryMode SymmetryMode = EQuickSDFIntermediateSymmetryMode::WholeTextureFlip90;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Intermediate SDF")
	EQuickSDFIntermediateLilToonLeftSource LilToonLeftSource = EQuickSDFIntermediateLilToonLeftSource::InternalY;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Intermediate SDF")
	bool bForceRGBA16F = false;
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

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Intermediate SDF")
	class UTexture2D* IntermediateSDFTexture = nullptr;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Intermediate SDF")
	FQuickSDFIntermediateMetadata IntermediateMetadata;

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

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Auto Symmetry")
	bool bHasLastAutoSymmetryResult = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Auto Symmetry")
	EQuickSDFAutoSymmetryResolvedMode LastAutoSymmetryResolvedMode = EQuickSDFAutoSymmetryResolvedMode::Texture;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Auto Symmetry")
	float LastAutoSymmetryConfidence = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Auto Symmetry", meta = (MultiLine = true))
	FText LastAutoSymmetryStatus;
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
