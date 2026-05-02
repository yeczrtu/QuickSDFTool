#include "QuickSDFAsset.h"

#include "CanvasItem.h"
#include "Engine/Canvas.h"
#include "Engine/TextureRenderTarget2D.h"
#include "RenderResource.h"
#include "TextureResource.h"

UQuickSDFAsset::UQuickSDFAsset()
{
	Resolution = FIntPoint(1024, 1024);
	UVChannel = 0;
	FinalSDFTexture = nullptr;
	ActiveTextureSetIndex = 0;
}

void UQuickSDFAsset::InitializeRenderTargets(UWorld* InWorld)
{
	for (FQuickSDFAngleData& Data : AngleDataList)
	{
		if (!Data.PaintRenderTarget)
		{
			Data.PaintRenderTarget = NewObject<UTextureRenderTarget2D>(this);
			Data.PaintRenderTarget->RenderTargetFormat = RTF_R8;
			Data.PaintRenderTarget->ClearColor = FLinearColor::White;
			Data.PaintRenderTarget->InitAutoFormat(Resolution.X, Resolution.Y);
			Data.PaintRenderTarget->UpdateResourceImmediate(true);

			if (Data.TextureMask && InWorld)
			{
				FTextureRenderTargetResource* RTResource = Data.PaintRenderTarget->GameThread_GetRenderTargetResource();
				if (RTResource)
				{
					FCanvas Canvas(RTResource, nullptr, InWorld, GMaxRHIFeatureLevel);
					FCanvasTileItem TileItem(FVector2D::ZeroVector, Data.TextureMask->GetResource(), FVector2D(Resolution.X, Resolution.Y), FLinearColor::White);
					TileItem.BlendMode = SE_BLEND_Opaque;
					Canvas.DrawItem(TileItem);
					Canvas.Flush_GameThread(true);
				}
			}
		}
	}

	SyncActiveTextureSetFromLegacy();
}

void UQuickSDFAsset::BakeToTextures()
{
}

FQuickSDFTextureSetData* UQuickSDFAsset::GetActiveTextureSet()
{
	return TextureSets.IsValidIndex(ActiveTextureSetIndex) ? &TextureSets[ActiveTextureSetIndex] : nullptr;
}

const FQuickSDFTextureSetData* UQuickSDFAsset::GetActiveTextureSet() const
{
	return TextureSets.IsValidIndex(ActiveTextureSetIndex) ? &TextureSets[ActiveTextureSetIndex] : nullptr;
}

bool UQuickSDFAsset::SetActiveTextureSetIndex(int32 NewIndex)
{
	if (!TextureSets.IsValidIndex(NewIndex))
	{
		return false;
	}

	if (NewIndex == ActiveTextureSetIndex)
	{
		if (AngleDataList.Num() == 0 && TextureSets[NewIndex].AngleDataList.Num() > 0)
		{
			SyncLegacyFromActiveTextureSet();
		}
		else
		{
			SyncActiveTextureSetFromLegacy();
		}
		return true;
	}

	if (TextureSets.IsValidIndex(ActiveTextureSetIndex))
	{
		SyncActiveTextureSetFromLegacy();
	}

	ActiveTextureSetIndex = NewIndex;
	SyncLegacyFromActiveTextureSet();
	return true;
}

void UQuickSDFAsset::MigrateLegacyDataToTextureSetsIfNeeded()
{
	if (TextureSets.Num() > 0 || AngleDataList.Num() == 0)
	{
		return;
	}

	bool bHasStoredLegacyBakeData = FinalSDFTexture != nullptr;
	for (const FQuickSDFAngleData& AngleData : AngleDataList)
	{
		if (AngleData.TextureMask)
		{
			bHasStoredLegacyBakeData = true;
			break;
		}
	}

	FQuickSDFTextureSetData& TextureSet = TextureSets.AddDefaulted_GetRef();
	TextureSet.MaterialSlotIndex = INDEX_NONE;
	TextureSet.SlotName = TEXT("Default");
	TextureSet.MaterialName = TEXT("Legacy");
	TextureSet.UVChannel = UVChannel;
	TextureSet.Resolution = Resolution;
	TextureSet.AngleDataList = AngleDataList;
	TextureSet.FinalSDFTexture = FinalSDFTexture;
	TextureSet.bInitialBakeComplete = bHasStoredLegacyBakeData;
	TextureSet.bDirty = false;
	ActiveTextureSetIndex = 0;
}

void UQuickSDFAsset::SyncActiveTextureSetFromLegacy()
{
	if (FQuickSDFTextureSetData* TextureSet = GetActiveTextureSet())
	{
		TextureSet->UVChannel = UVChannel;
		TextureSet->Resolution = Resolution;
		TextureSet->AngleDataList = AngleDataList;
		TextureSet->FinalSDFTexture = FinalSDFTexture;
	}
}

void UQuickSDFAsset::SyncLegacyFromActiveTextureSet()
{
	if (const FQuickSDFTextureSetData* TextureSet = GetActiveTextureSet())
	{
		UVChannel = TextureSet->UVChannel;
		Resolution = TextureSet->Resolution;
		AngleDataList = TextureSet->AngleDataList;
		FinalSDFTexture = TextureSet->FinalSDFTexture;
	}
}
