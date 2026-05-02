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

void UQuickSDFAsset::PostLoad()
{
	Super::PostLoad();
	MigrateLegacyDataToTextureSetsIfNeeded();
	SyncLegacyFromActiveTextureSet();
}

void UQuickSDFAsset::InitializeRenderTargets(UWorld* InWorld)
{
	MigrateLegacyDataToTextureSetsIfNeeded();
	TArray<FQuickSDFAngleData>& ActiveAngles = GetActiveAngleDataList();
	const FIntPoint ActiveResolution = GetActiveResolution();

	for (FQuickSDFAngleData& Data : ActiveAngles)
	{
		if (!Data.PaintRenderTarget)
		{
			Data.PaintRenderTarget = NewObject<UTextureRenderTarget2D>(this);
			Data.PaintRenderTarget->RenderTargetFormat = RTF_R8;
			Data.PaintRenderTarget->ClearColor = FLinearColor::White;
			Data.PaintRenderTarget->InitAutoFormat(ActiveResolution.X, ActiveResolution.Y);
			Data.PaintRenderTarget->UpdateResourceImmediate(true);

			if (Data.TextureMask && InWorld)
			{
				FTextureRenderTargetResource* RTResource = Data.PaintRenderTarget->GameThread_GetRenderTargetResource();
				if (RTResource)
				{
					FCanvas Canvas(RTResource, nullptr, InWorld, GMaxRHIFeatureLevel);
					FCanvasTileItem TileItem(FVector2D::ZeroVector, Data.TextureMask->GetResource(), FVector2D(ActiveResolution.X, ActiveResolution.Y), FLinearColor::White);
					TileItem.BlendMode = SE_BLEND_Opaque;
					Canvas.DrawItem(TileItem);
					Canvas.Flush_GameThread(true);
				}
			}
		}
	}

	SyncLegacyFromActiveTextureSet();
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

FQuickSDFTextureSetData& UQuickSDFAsset::EnsureActiveTextureSet()
{
	MigrateLegacyDataToTextureSetsIfNeeded();

	if (!TextureSets.IsValidIndex(ActiveTextureSetIndex))
	{
		ActiveTextureSetIndex = TextureSets.Num() > 0 ? 0 : INDEX_NONE;
	}

	if (!TextureSets.IsValidIndex(ActiveTextureSetIndex))
	{
		FQuickSDFTextureSetData& TextureSet = TextureSets.AddDefaulted_GetRef();
		TextureSet.MaterialSlotIndex = INDEX_NONE;
		TextureSet.SlotName = TEXT("Default");
		TextureSet.MaterialName = TEXT("Default");
		TextureSet.UVChannel = UVChannel;
		TextureSet.Resolution = Resolution;
		TextureSet.AngleDataList = AngleDataList;
		TextureSet.FinalSDFTexture = FinalSDFTexture;
		ActiveTextureSetIndex = 0;
	}

	return TextureSets[ActiveTextureSetIndex];
}

TArray<FQuickSDFAngleData>& UQuickSDFAsset::GetActiveAngleDataList()
{
	return EnsureActiveTextureSet().AngleDataList;
}

const TArray<FQuickSDFAngleData>& UQuickSDFAsset::GetActiveAngleDataList() const
{
	if (const FQuickSDFTextureSetData* TextureSet = GetActiveTextureSet())
	{
		return TextureSet->AngleDataList;
	}
	return AngleDataList;
}

FIntPoint& UQuickSDFAsset::GetActiveResolution()
{
	return EnsureActiveTextureSet().Resolution;
}

const FIntPoint& UQuickSDFAsset::GetActiveResolution() const
{
	if (const FQuickSDFTextureSetData* TextureSet = GetActiveTextureSet())
	{
		return TextureSet->Resolution;
	}
	return Resolution;
}

int32& UQuickSDFAsset::GetActiveUVChannel()
{
	return EnsureActiveTextureSet().UVChannel;
}

const int32& UQuickSDFAsset::GetActiveUVChannel() const
{
	if (const FQuickSDFTextureSetData* TextureSet = GetActiveTextureSet())
	{
		return TextureSet->UVChannel;
	}
	return UVChannel;
}

UTexture2D*& UQuickSDFAsset::GetActiveFinalSDFTexture()
{
	return EnsureActiveTextureSet().FinalSDFTexture;
}

UTexture2D* UQuickSDFAsset::GetActiveFinalSDFTexture() const
{
	if (const FQuickSDFTextureSetData* TextureSet = GetActiveTextureSet())
	{
		return TextureSet->FinalSDFTexture;
	}
	return FinalSDFTexture;
}

bool UQuickSDFAsset::SetActiveTextureSetIndex(int32 NewIndex)
{
	MigrateLegacyDataToTextureSetsIfNeeded();

	if (!TextureSets.IsValidIndex(NewIndex))
	{
		return false;
	}

	if (NewIndex == ActiveTextureSetIndex)
	{
		SyncLegacyFromActiveTextureSet();
		return true;
	}

	ActiveTextureSetIndex = NewIndex;
	SyncLegacyFromActiveTextureSet();
	return true;
}

void UQuickSDFAsset::MigrateLegacyDataToTextureSetsIfNeeded()
{
	if (TextureSets.Num() > 0)
	{
		if (!TextureSets.IsValidIndex(ActiveTextureSetIndex))
		{
			ActiveTextureSetIndex = 0;
		}
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
	SyncLegacyFromActiveTextureSet();
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
