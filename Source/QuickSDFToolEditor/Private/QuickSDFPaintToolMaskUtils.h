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
}
