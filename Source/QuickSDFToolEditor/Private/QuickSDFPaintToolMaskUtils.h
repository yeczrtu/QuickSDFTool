#pragma once

#include "CoreMinimal.h"
#include "QuickSDFPaintToolConstants.h"
#include "QuickSDFAsset.h"
#include "QuickSDFToolProperties.h"
#include "SDFProcessor.h"

class UQuickSDFPaintTool;
class UTexture2D;
class UTextureRenderTarget2D;

namespace QuickSDFPaintToolPrivate
{
struct FQuickSDFIslandMirrorChart
{
	FString Key;
	int32 ChartID = INDEX_NONE;
	FVector2f UVMin = FVector2f::ZeroVector;
	FVector2f UVMax = FVector2f::ZeroVector;
	float Area = 0.0f;
	float AspectRatio = 1.0f;
	bool bOutOfRange = false;
	TArray<uint8> ShapeMask;
};

struct FQuickSDFIslandMirrorApplyResult
{
	int32 MirroredPixels = 0;
	int32 FallbackPixels = 0;
	int32 MissingPairPixels = 0;
	int32 MissingSourcePixels = 0;
	int32 AmbiguousPixels = 0;
};

int32 GetQuickSDFPresetSize(EQuickSDFQualityPreset Preset);
bool ShouldProcessMaskAngle(float RawAngle, bool bSymmetryMode);
TArray<int32> CollectProcessableMaskIndices(const UQuickSDFAsset& Asset, bool bSymmetryMode);
bool TryBuildMaskData(
	const UQuickSDFPaintTool& Tool,
	UTextureRenderTarget2D* RenderTarget,
	float RawAngle,
	float MaxAngle,
	int32 OrigW,
	int32 OrigH,
	int32 Upscale,
	FMaskData& OutData);
void SortMaskData(TArray<FMaskData>& MaskData);
bool NeedsBipolarOutput(const TArray<FMaskData>& MaskData, int32 PixelCount);
bool TryExtractAngleFromName(const FString& Name, float& OutAngle);
bool HasImportedSourceMasks(const UQuickSDFAsset* Asset);
bool HasNonWhitePaintMasks(const UQuickSDFPaintTool& Tool, const UQuickSDFAsset* Asset);
bool IsPersistentQuickSDFAsset(const UQuickSDFAsset* Asset);
TArray<FColor> MakeSolidPixels(int32 Width, int32 Height, const FLinearColor& FillColor);
bool EnsureMaskGuids(UQuickSDFAsset* Asset);
int32 FindAngleIndexByGuid(const UQuickSDFAsset* Asset, const FGuid& MaskGuid);
void CaptureMaskState(
	UQuickSDFPaintTool& Tool,
	UQuickSDFAsset* Asset,
	TArray<FGuid>& OutGuids,
	TArray<float>& OutAngles,
	TArray<UTexture2D*>& OutTextures,
	TArray<bool>& OutAllowSourceTextureOverwrites,
	TArray<TArray<FColor>>& OutPixelsByMask);
void RestoreMaskStateOnNextTick(
	UQuickSDFPaintTool* Tool,
	const TArray<FGuid>& MaskGuids,
	const TArray<float>& Angles,
	const TArray<UTexture2D*>& Textures,
	const TArray<bool>& AllowSourceTextureOverwrites,
	const TArray<TArray<FColor>>& PixelsByMask);
FVector2f TransformIslandMirrorLocalUV(const FVector2f& LocalUV, EQuickSDFIslandMirrorTransform Transform);
FVector4f SampleCombinedFieldBilinear(const TArray<FVector4f>& CombinedField, int32 Width, int32 Height, const FVector2f& UV);
FQuickSDFIslandMirrorApplyResult ApplyIslandMirrorToCombinedField(
	TArray<FVector4f>& CombinedField,
	int32 Width,
	int32 Height,
	bool bBipolar,
	const TArray<FQuickSDFIslandMirrorChart>& Charts,
	const TArray<int32>& PixelChartIDs,
	const TArray<uint8>& AmbiguousPixelFlags,
	const TArray<FQuickSDFIslandMirrorPair>& Pairs);
}
