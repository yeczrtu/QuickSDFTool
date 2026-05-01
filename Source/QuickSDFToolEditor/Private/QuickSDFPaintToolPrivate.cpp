#include "QuickSDFPaintToolPrivate.h"

#include "Containers/Ticker.h"
#include "Editor.h"
#include "Engine/Selection.h"
#include "Misc/DefaultValueHelper.h"
#include "Misc/PackageName.h"

namespace QuickSDFPaintToolPrivate
{
bool ShouldProcessMaskAngle(float RawAngle, bool bSymmetryMode)
{
	return !bSymmetryMode || (RawAngle >= -0.01f && RawAngle <= 90.01f);
}

TArray<int32> CollectProcessableMaskIndices(const UQuickSDFAsset& Asset, bool bSymmetryMode)
{
	TArray<int32> Indices;
	for (int32 Index = 0; Index < Asset.AngleDataList.Num(); ++Index)
	{
		const FQuickSDFAngleData& AngleData = Asset.AngleDataList[Index];
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

bool TryExtractAngleFromName(const FString& Name, float& OutAngle)
{
	TArray<float> Candidates;
	FString Token;

	auto FlushToken = [&]()
	{
		if (!Token.IsEmpty())
		{
			float ParsedValue = 0.0f;
			if (FDefaultValueHelper::ParseFloat(Token, ParsedValue) && ParsedValue >= 0.0f && ParsedValue <= 180.0f)
			{
				Candidates.Add(ParsedValue);
			}
			Token.Reset();
		}
	};

	for (int32 CharIndex = 0; CharIndex < Name.Len(); ++CharIndex)
	{
		const TCHAR Char = Name[CharIndex];
		const bool bCanStartNegative = Char == TEXT('-') && Token.IsEmpty() && CharIndex + 1 < Name.Len() && FChar::IsDigit(Name[CharIndex + 1]);
		if (FChar::IsDigit(Char) || Char == TEXT('.') || bCanStartNegative)
		{
			Token.AppendChar(Char);
		}
		else
		{
			FlushToken();
		}
	}
	FlushToken();

	if (Candidates.Num() == 0)
	{
		return false;
	}

	OutAngle = Candidates.Last();
	return true;
}

TArray<UTexture2D*> CollectSelectedTextureAssets()
{
	TArray<UTexture2D*> SelectedTextures;
	if (!GEditor)
	{
		return SelectedTextures;
	}

	if (USelection* SelectedObjects = GEditor->GetSelectedObjects())
	{
		for (FSelectionIterator It(*SelectedObjects); It; ++It)
		{
			if (UTexture2D* Texture = Cast<UTexture2D>(*It))
			{
				SelectedTextures.Add(Texture);
			}
		}
	}

	SelectedTextures.Sort([](const UTexture2D& A, const UTexture2D& B)
	{
		return A.GetName() < B.GetName();
	});
	return SelectedTextures;
}

bool HasImportedSourceMasks(const UQuickSDFAsset* Asset)
{
	if (!Asset)
	{
		return false;
	}

	for (const FQuickSDFAngleData& AngleData : Asset->AngleDataList)
	{
		if (AngleData.TextureMask)
		{
			return true;
		}
	}

	return false;
}

bool HasNonWhitePaintMasks(const UQuickSDFPaintTool& Tool, const UQuickSDFAsset* Asset)
{
	if (!Asset)
	{
		return false;
	}

	TArray<FColor> Pixels;
	for (const FQuickSDFAngleData& AngleData : Asset->AngleDataList)
	{
		if (!AngleData.PaintRenderTarget || !Tool.CaptureRenderTargetPixels(AngleData.PaintRenderTarget, Pixels))
		{
			continue;
		}

		for (const FColor& Pixel : Pixels)
		{
			if (Pixel.R < 250 || Pixel.G < 250 || Pixel.B < 250)
			{
				return true;
			}
		}
	}

	return false;
}

bool IsPersistentQuickSDFAsset(const UQuickSDFAsset* Asset)
{
	return Asset &&
		Asset->GetOutermost() != GetTransientPackage() &&
		FPackageName::IsValidLongPackageName(Asset->GetOutermost()->GetName());
}

TArray<FColor> MakeSolidPixels(int32 Width, int32 Height, const FLinearColor& FillColor)
{
	TArray<FColor> Pixels;
	if (Width <= 0 || Height <= 0)
	{
		return Pixels;
	}

	Pixels.Init(FillColor.ToFColor(false), Width * Height);
	return Pixels;
}

bool EnsureMaskGuids(UQuickSDFAsset* Asset)
{
	if (!Asset)
	{
		return false;
	}

	bool bChanged = false;
	for (FQuickSDFAngleData& AngleData : Asset->AngleDataList)
	{
		if (!AngleData.MaskGuid.IsValid())
		{
			AngleData.MaskGuid = FGuid::NewGuid();
			bChanged = true;
		}
	}
	return bChanged;
}

int32 FindAngleIndexByGuid(const UQuickSDFAsset* Asset, const FGuid& MaskGuid)
{
	if (!Asset || !MaskGuid.IsValid())
	{
		return INDEX_NONE;
	}

	for (int32 Index = 0; Index < Asset->AngleDataList.Num(); ++Index)
	{
		if (Asset->AngleDataList[Index].MaskGuid == MaskGuid)
		{
			return Index;
		}
	}
	return INDEX_NONE;
}

void CaptureMaskState(
	UQuickSDFPaintTool& Tool,
	UQuickSDFAsset* Asset,
	TArray<FGuid>& OutGuids,
	TArray<float>& OutAngles,
	TArray<UTexture2D*>& OutTextures,
	TArray<bool>& OutAllowSourceTextureOverwrites,
	TArray<TArray<FColor>>& OutPixelsByMask)
{
	OutGuids.Reset();
	OutAngles.Reset();
	OutTextures.Reset();
	OutAllowSourceTextureOverwrites.Reset();
	OutPixelsByMask.Reset();
	if (!Asset)
	{
		return;
	}

	EnsureMaskGuids(Asset);
	for (const FQuickSDFAngleData& AngleData : Asset->AngleDataList)
	{
		OutGuids.Add(AngleData.MaskGuid);
		OutAngles.Add(AngleData.Angle);
		OutTextures.Add(AngleData.TextureMask);
		OutAllowSourceTextureOverwrites.Add(AngleData.bAllowSourceTextureOverwrite);

		TArray<FColor>& Pixels = OutPixelsByMask.AddDefaulted_GetRef();
		if (AngleData.PaintRenderTarget)
		{
			Tool.CaptureRenderTargetPixels(AngleData.PaintRenderTarget, Pixels);
		}
	}
}

void RestoreMaskStateOnNextTick(
	UQuickSDFPaintTool* Tool,
	const TArray<FGuid>& MaskGuids,
	const TArray<float>& Angles,
	const TArray<UTexture2D*>& Textures,
	const TArray<bool>& AllowSourceTextureOverwrites,
	const TArray<TArray<FColor>>& PixelsByMask)
{
	if (!Tool)
	{
		return;
	}

	Tool->RestoreMaskStateByGuid(MaskGuids, Angles, Textures, AllowSourceTextureOverwrites, PixelsByMask);

	TWeakObjectPtr<UQuickSDFPaintTool> WeakTool(Tool);
	TArray<FGuid> DeferredGuids = MaskGuids;
	TArray<float> DeferredAngles = Angles;
	TArray<UTexture2D*> DeferredTextures = Textures;
	TArray<bool> DeferredAllowSourceTextureOverwrites = AllowSourceTextureOverwrites;
	TArray<TArray<FColor>> DeferredPixelsByMask = PixelsByMask;
	FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda(
		[WeakTool, DeferredGuids = MoveTemp(DeferredGuids), DeferredAngles = MoveTemp(DeferredAngles), DeferredTextures = MoveTemp(DeferredTextures), DeferredAllowSourceTextureOverwrites = MoveTemp(DeferredAllowSourceTextureOverwrites), DeferredPixelsByMask = MoveTemp(DeferredPixelsByMask)](float)
		{
			if (WeakTool.IsValid())
			{
				WeakTool->RestoreMaskStateByGuid(DeferredGuids, DeferredAngles, DeferredTextures, DeferredAllowSourceTextureOverwrites, DeferredPixelsByMask);
			}
			return false;
		}));
}
}
