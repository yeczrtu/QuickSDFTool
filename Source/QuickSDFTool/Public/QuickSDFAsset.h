#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "UObject/NoExportTypes.h"
#include "Engine/Texture2D.h"
#include "QuickSDFAsset.generated.h"

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

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Transient, Category = "SDF")
	class UTextureRenderTarget2D* PaintRenderTarget = nullptr;
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

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SDF settings")
	FIntPoint Resolution;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SDF settings")
	int32 UVChannel;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SDF Data")
	TArray<FQuickSDFAngleData> AngleDataList;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "SDF Result")
	class UTexture2D* FinalSDFTexture;

	// Sets up render targets for painting
	void InitializeRenderTargets(class UWorld* InWorld);
	// Merges render targets back to standard textures
	void BakeToTextures();
};
