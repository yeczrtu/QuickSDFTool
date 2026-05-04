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

float MeasureTextureMirrorOccupancyScore(const TArray<int32>& PixelChartIDs, int32 Width, int32 Height)
{
	if (Width <= 0 || Height <= 0 || PixelChartIDs.Num() != Width * Height)
	{
		return 0.0f;
	}

	int32 OccupiedUnionPixels = 0;
	int32 MatchedPixels = 0;
	for (int32 Y = 0; Y < Height; ++Y)
	{
		for (int32 X = 0; X < Width; ++X)
		{
			const int32 LeftIndex = Y * Width + X;
			const int32 RightIndex = Y * Width + (Width - 1 - X);
			const bool bLeftOccupied = PixelChartIDs[LeftIndex] != INDEX_NONE;
			const bool bRightOccupied = PixelChartIDs[RightIndex] != INDEX_NONE;
			if (!bLeftOccupied && !bRightOccupied)
			{
				continue;
			}

			++OccupiedUnionPixels;
			if (bLeftOccupied == bRightOccupied)
			{
				++MatchedPixels;
			}
		}
	}

	return OccupiedUnionPixels > 0
		? static_cast<float>(MatchedPixels) / static_cast<float>(OccupiedUnionPixels)
		: 0.0f;
}

EQuickSDFSymmetryMode ResolveAutoSymmetryModeFromAnalysis(bool bHasValidUVData, float TextureMirrorScore, int32 AmbiguousPixelCount, int32 OutOfRangeIslandCount)
{
	if (!bHasValidUVData)
	{
		return EQuickSDFSymmetryMode::WholeTextureFlip90;
	}

	if (AmbiguousPixelCount > 0 || OutOfRangeIslandCount > 0)
	{
		return EQuickSDFSymmetryMode::UVIslandChannelFlip90;
	}

	constexpr float TextureMirrorThreshold = 0.97f;
	return TextureMirrorScore >= TextureMirrorThreshold
		? EQuickSDFSymmetryMode::WholeTextureFlip90
		: EQuickSDFSymmetryMode::UVIslandChannelFlip90;
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

FVector2f TransformIslandMirrorLocalUV(const FVector2f& LocalUV, EQuickSDFIslandMirrorTransform Transform)
{
	switch (Transform)
	{
	case EQuickSDFIslandMirrorTransform::FlipV:
		return FVector2f(LocalUV.X, 1.0f - LocalUV.Y);
	case EQuickSDFIslandMirrorTransform::Rotate180:
		return FVector2f(1.0f - LocalUV.X, 1.0f - LocalUV.Y);
	case EQuickSDFIslandMirrorTransform::SwapUVFlipU:
		return FVector2f(1.0f - LocalUV.Y, LocalUV.X);
	case EQuickSDFIslandMirrorTransform::SwapUVFlipV:
		return FVector2f(LocalUV.Y, 1.0f - LocalUV.X);
	case EQuickSDFIslandMirrorTransform::FlipU:
	default:
		return FVector2f(1.0f - LocalUV.X, LocalUV.Y);
	}
}

FVector4f SampleCombinedFieldBilinear(const TArray<FVector4f>& CombinedField, int32 Width, int32 Height, const FVector2f& UV)
{
	if (Width <= 0 || Height <= 0 || CombinedField.Num() != Width * Height)
	{
		return FVector4f(1.0f, 1.0f, 1.0f, 1.0f);
	}

	const float PixelX = FMath::Clamp(UV.X * Width - 0.5f, 0.0f, static_cast<float>(Width - 1));
	const float PixelY = FMath::Clamp(UV.Y * Height - 0.5f, 0.0f, static_cast<float>(Height - 1));
	const int32 X0 = FMath::Clamp(FMath::FloorToInt(PixelX), 0, Width - 1);
	const int32 Y0 = FMath::Clamp(FMath::FloorToInt(PixelY), 0, Height - 1);
	const int32 X1 = FMath::Clamp(X0 + 1, 0, Width - 1);
	const int32 Y1 = FMath::Clamp(Y0 + 1, 0, Height - 1);
	const float Tx = PixelX - static_cast<float>(X0);
	const float Ty = PixelY - static_cast<float>(Y0);

	const FVector4f C00 = CombinedField[Y0 * Width + X0];
	const FVector4f C10 = CombinedField[Y0 * Width + X1];
	const FVector4f C01 = CombinedField[Y1 * Width + X0];
	const FVector4f C11 = CombinedField[Y1 * Width + X1];
	return FMath::Lerp(FMath::Lerp(C00, C10, Tx), FMath::Lerp(C01, C11, Tx), Ty);
}

FQuickSDFIslandMirrorApplyResult ApplyIslandMirrorToCombinedField(
	TArray<FVector4f>& CombinedField,
	int32 Width,
	int32 Height,
	bool bBipolar,
	const TArray<FQuickSDFIslandMirrorChart>& Charts,
	const TArray<int32>& PixelChartIDs,
	const TArray<uint8>& AmbiguousPixelFlags,
	const TArray<FQuickSDFIslandMirrorPair>& Pairs)
{
	FQuickSDFIslandMirrorApplyResult Result;
	if (Width <= 0 || Height <= 0 || CombinedField.Num() != Width * Height || PixelChartIDs.Num() != Width * Height)
	{
		return Result;
	}

	TMap<int32, const FQuickSDFIslandMirrorChart*> ChartByID;
	TMap<FString, const FQuickSDFIslandMirrorChart*> ChartByKey;
	for (const FQuickSDFIslandMirrorChart& Chart : Charts)
	{
		ChartByID.Add(Chart.ChartID, &Chart);
		ChartByKey.Add(Chart.Key, &Chart);
	}

	TMap<FString, const FQuickSDFIslandMirrorPair*> PairByTargetKey;
	for (const FQuickSDFIslandMirrorPair& Pair : Pairs)
	{
		if (Pair.bEnabled && !Pair.TargetIslandKey.IsEmpty())
		{
			PairByTargetKey.Add(Pair.TargetIslandKey, &Pair);
		}
	}

	for (int32 PixelIndex = 0; PixelIndex < CombinedField.Num(); ++PixelIndex)
	{
		FVector4f& Pixel = CombinedField[PixelIndex];
		if (!bBipolar)
		{
			Pixel.Y = 1.0f;
			Pixel.Z = 1.0f;
		}

		if (AmbiguousPixelFlags.IsValidIndex(PixelIndex) && AmbiguousPixelFlags[PixelIndex] != 0)
		{
			++Result.AmbiguousPixels;
		}

		const FQuickSDFIslandMirrorChart* TargetChart = ChartByID.FindRef(PixelChartIDs[PixelIndex]);
		if (!TargetChart)
		{
			Pixel.Y = bBipolar ? Pixel.Z : 1.0f;
			Pixel.W = Pixel.X;
			++Result.FallbackPixels;
			continue;
		}

		const FQuickSDFIslandMirrorPair* Pair = PairByTargetKey.FindRef(TargetChart->Key);
		if (!Pair)
		{
			Pixel.Y = bBipolar ? Pixel.Z : 1.0f;
			Pixel.W = Pixel.X;
			++Result.MissingPairPixels;
			++Result.FallbackPixels;
			continue;
		}

		const FQuickSDFIslandMirrorChart* SourceChart = ChartByKey.FindRef(Pair->SourceIslandKey);
		if (!SourceChart)
		{
			Pixel.Y = bBipolar ? Pixel.Z : 1.0f;
			Pixel.W = Pixel.X;
			++Result.MissingSourcePixels;
			++Result.FallbackPixels;
			continue;
		}

		const int32 X = PixelIndex % Width;
		const int32 Y = PixelIndex / Width;
		const FVector2f TargetUV(
			(static_cast<float>(X) + 0.5f) / static_cast<float>(Width),
			(static_cast<float>(Y) + 0.5f) / static_cast<float>(Height));

		const FVector2f TargetSize(
			FMath::Max(TargetChart->UVMax.X - TargetChart->UVMin.X, KINDA_SMALL_NUMBER),
			FMath::Max(TargetChart->UVMax.Y - TargetChart->UVMin.Y, KINDA_SMALL_NUMBER));
		const FVector2f SourceSize(
			FMath::Max(SourceChart->UVMax.X - SourceChart->UVMin.X, KINDA_SMALL_NUMBER),
			FMath::Max(SourceChart->UVMax.Y - SourceChart->UVMin.Y, KINDA_SMALL_NUMBER));
		const FVector2f TargetLocal(
			(TargetUV.X - TargetChart->UVMin.X) / TargetSize.X,
			(TargetUV.Y - TargetChart->UVMin.Y) / TargetSize.Y);
		const FVector2f SourceLocal = TransformIslandMirrorLocalUV(TargetLocal, Pair->Transform);
		const FVector2f SourceUV(
			SourceChart->UVMin.X + SourceLocal.X * SourceSize.X,
			SourceChart->UVMin.Y + SourceLocal.Y * SourceSize.Y);

		if (SourceUV.X < 0.0f || SourceUV.X > 1.0f || SourceUV.Y < 0.0f || SourceUV.Y > 1.0f)
		{
			Pixel.Y = bBipolar ? Pixel.Z : 1.0f;
			Pixel.W = Pixel.X;
			++Result.FallbackPixels;
			continue;
		}

		const FVector4f Mirrored = SampleCombinedFieldBilinear(CombinedField, Width, Height, SourceUV);
		// DownscaleAndConvert exports G from internal W and A from internal Y.
		Pixel.Y = bBipolar ? Mirrored.Z : 1.0f;
		Pixel.W = Mirrored.X;
		++Result.MirroredPixels;
	}

	return Result;
}
}
