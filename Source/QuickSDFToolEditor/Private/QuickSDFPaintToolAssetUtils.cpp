#include "QuickSDFPaintToolMaskUtils.h"

#include "QuickSDFPaintTool.h"

#include "Containers/Ticker.h"
#include "Editor.h"
#include "Engine/Selection.h"
#include "Misc/DefaultValueHelper.h"
#include "Misc/PackageName.h"

namespace QuickSDFPaintToolPrivate
{
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

	for (const FQuickSDFAngleData& AngleData : Asset->GetActiveAngleDataList())
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
	for (const FQuickSDFAngleData& AngleData : Asset->GetActiveAngleDataList())
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
	for (FQuickSDFAngleData& AngleData : Asset->GetActiveAngleDataList())
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

	for (int32 Index = 0; Index < Asset->GetActiveAngleDataList().Num(); ++Index)
	{
		if (Asset->GetActiveAngleDataList()[Index].MaskGuid == MaskGuid)
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
	for (const FQuickSDFAngleData& AngleData : Asset->GetActiveAngleDataList())
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
