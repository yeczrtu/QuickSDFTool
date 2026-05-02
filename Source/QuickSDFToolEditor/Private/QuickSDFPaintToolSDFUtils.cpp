#include "QuickSDFPaintToolMaskUtils.h"

#include "QuickSDFPaintTool.h"

namespace QuickSDFPaintToolPrivate
{
bool ShouldProcessMaskAngle(float RawAngle, bool bSymmetryMode)
{
	return !bSymmetryMode || (RawAngle >= -0.01f && RawAngle <= 90.01f);
}

TArray<int32> CollectProcessableMaskIndices(const UQuickSDFAsset& Asset, bool bSymmetryMode)
{
	TArray<int32> Indices;
	const TArray<FQuickSDFAngleData>& ActiveAngles = Asset.GetActiveAngleDataList();
	for (int32 Index = 0; Index < ActiveAngles.Num(); ++Index)
	{
		const FQuickSDFAngleData& AngleData = ActiveAngles[Index];
		if (AngleData.PaintRenderTarget && ShouldProcessMaskAngle(AngleData.Angle, bSymmetryMode))
		{
			Indices.Add(Index);
		}
	}
	return Indices;
}

bool TryBuildMaskData(
	const UQuickSDFPaintTool& Tool,
	UTextureRenderTarget2D* RenderTarget,
	float RawAngle,
	float MaxAngle,
	int32 OrigW,
	int32 OrigH,
	int32 Upscale,
	FMaskData& OutData)
{
	TArray<FColor> Pixels;
	if (!RenderTarget || !Tool.CaptureRenderTargetPixels(RenderTarget, Pixels))
	{
		return false;
	}

	const int32 HighW = OrigW * Upscale;
	const int32 HighH = OrigH * Upscale;
	TArray<uint8> GrayPixels = FSDFProcessor::ConvertToGrayscale(Pixels);
	TArray<uint8> UpscaledPixels = FSDFProcessor::UpscaleImage(GrayPixels, OrigW, OrigH, Upscale);

	OutData.SDF = FSDFProcessor::GenerateSDF(UpscaledPixels, HighW, HighH);
	OutData.TargetT = FMath::Clamp(FMath::Abs(RawAngle) / FMath::Max(MaxAngle, KINDA_SMALL_NUMBER), 0.0f, 1.0f);
	OutData.bIsOpposite = RawAngle < -0.01f;
	return true;
}

void SortMaskData(TArray<FMaskData>& MaskData)
{
	MaskData.Sort([](const FMaskData& A, const FMaskData& B)
	{
		if (A.bIsOpposite != B.bIsOpposite)
		{
			return !A.bIsOpposite;
		}
		return A.TargetT < B.TargetT;
	});
}

bool NeedsBipolarOutput(const TArray<FMaskData>& MaskData, int32 PixelCount)
{
	for (int32 Index = 0; Index < MaskData.Num(); ++Index)
	{
		if (MaskData[Index].bIsOpposite)
		{
			return true;
		}

		if (Index >= MaskData.Num() - 1)
		{
			continue;
		}

		const FMaskData& Current = MaskData[Index];
		const FMaskData& Next = MaskData[Index + 1];
		const int32 NumComparablePixels = FMath::Min(PixelCount, FMath::Min(Current.SDF.Num(), Next.SDF.Num()));
		for (int32 PixelIndex = 0; PixelIndex < NumComparablePixels; PixelIndex += QuickSDFBipolarDetectionStride)
		{
			if (Current.SDF[PixelIndex] > 0.0 && Next.SDF[PixelIndex] <= 0.0)
			{
				return true;
			}
		}
	}
	return false;
}

int32 GetQuickSDFPresetSize(EQuickSDFQualityPreset Preset)
{
	switch (Preset)
	{
	case EQuickSDFQualityPreset::Draft512:
		return 512;
	case EQuickSDFQualityPreset::High2048:
		return 2048;
	case EQuickSDFQualityPreset::Ultra4096:
		return 4096;
	case EQuickSDFQualityPreset::Standard1024:
	default:
		return 1024;
	}
}
}
