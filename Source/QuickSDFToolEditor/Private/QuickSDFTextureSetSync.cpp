#include "QuickSDFTextureSetSync.h"

#include "Components/MeshComponent.h"
#include "Components/SkinnedMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/SkinnedAsset.h"
#include "Engine/SkinnedAssetCommon.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInterface.h"
#include "QuickSDFPaintTool.h"
#include "QuickSDFPaintToolPrivate.h"

using namespace QuickSDFPaintToolPrivate;

namespace
{
int32 FindTextureSetForMaterialSlot(const UQuickSDFAsset* Asset, int32 MaterialSlotIndex)
{
	if (!Asset)
	{
		return INDEX_NONE;
	}

	for (int32 Index = 0; Index < Asset->TextureSets.Num(); ++Index)
	{
		if (Asset->TextureSets[Index].MaterialSlotIndex == MaterialSlotIndex)
		{
			return Index;
		}
	}
	return INDEX_NONE;
}

FName GetMaterialSlotName(const UMeshComponent* Component, int32 MaterialSlotIndex)
{
	if (!Component)
	{
		return NAME_None;
	}

	const TArray<FName> SlotNames = Component->GetMaterialSlotNames();
	if (SlotNames.IsValidIndex(MaterialSlotIndex) && !SlotNames[MaterialSlotIndex].IsNone())
	{
		return SlotNames[MaterialSlotIndex];
	}

	return FName(*FString::Printf(TEXT("Slot_%d"), MaterialSlotIndex));
}

FString GetMaterialDisplayName(UMaterialInterface* Material)
{
	return Material ? Material->GetName() : FString(TEXT("None"));
}

FName GetFallbackMaterialSlotName(const UMeshComponent* Component, int32 MaterialSlotIndex)
{
	const TArray<FName> SlotNames = Component ? Component->GetMaterialSlotNames() : TArray<FName>();
	if (SlotNames.IsValidIndex(MaterialSlotIndex) && !SlotNames[MaterialSlotIndex].IsNone())
	{
		return SlotNames[MaterialSlotIndex];
	}

	return FName(*FString::Printf(TEXT("Slot_%d"), MaterialSlotIndex));
}

QuickSDFTextureSetSync::FMaterialSlotInfo MakeMaterialSlotInfo(
	const UMeshComponent* Component,
	int32 MaterialSlotIndex,
	FName SlotName,
	UMaterialInterface* FallbackMaterial)
{
	QuickSDFTextureSetSync::FMaterialSlotInfo SlotInfo;
	SlotInfo.MaterialSlotIndex = MaterialSlotIndex;
	SlotInfo.SlotName = SlotName.IsNone() ? GetFallbackMaterialSlotName(Component, MaterialSlotIndex) : SlotName;

	UMaterialInterface* Material = Component ? Component->GetMaterial(MaterialSlotIndex) : nullptr;
	SlotInfo.MaterialName = GetMaterialDisplayName(Material ? Material : FallbackMaterial);
	return SlotInfo;
}

bool IsMaterialSlotVisibleForComponent(const UMeshComponent* Component, int32 MaterialSlotIndex)
{
	if (!Component || MaterialSlotIndex == INDEX_NONE)
	{
		return false;
	}

	for (const QuickSDFTextureSetSync::FMaterialSlotInfo& SlotInfo : QuickSDFTextureSetSync::GetMaterialSlotsForComponent(Component))
	{
		if (SlotInfo.MaterialSlotIndex == MaterialSlotIndex)
		{
			return true;
		}
	}
	return false;
}

bool TextureSetHasStoredBakeData(const FQuickSDFTextureSetData& TextureSet)
{
	if (TextureSet.FinalSDFTexture)
	{
		return true;
	}

	for (const FQuickSDFAngleData& AngleData : TextureSet.AngleDataList)
	{
		if (AngleData.TextureMask)
		{
			return true;
		}
	}

	return false;
}

bool TextureSetHasNonWhitePaintData(const UQuickSDFPaintTool* Tool, const FQuickSDFTextureSetData& TextureSet)
{
	if (!Tool)
	{
		return false;
	}

	TArray<FColor> Pixels;
	for (const FQuickSDFAngleData& AngleData : TextureSet.AngleDataList)
	{
		if (!AngleData.PaintRenderTarget || !Tool->CaptureRenderTargetPixels(AngleData.PaintRenderTarget, Pixels))
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

void NormalizeTextureSetBakeState(const UQuickSDFPaintTool* Tool, FQuickSDFTextureSetData& TextureSet)
{
	if (!Tool || !TextureSet.bInitialBakeComplete || TextureSetHasStoredBakeData(TextureSet))
	{
		return;
	}

	if (!TextureSetHasNonWhitePaintData(Tool, TextureSet))
	{
		TextureSet.bInitialBakeComplete = false;
		TextureSet.bDirty = false;
	}
}
}

namespace QuickSDFTextureSetSync
{
TArray<FMaterialSlotInfo> GetMaterialSlotsForComponent(const UMeshComponent* Component)
{
	TArray<FMaterialSlotInfo> Slots;
	if (!Component)
	{
		return Slots;
	}

	if (const UStaticMeshComponent* StaticMeshComponent = Cast<UStaticMeshComponent>(Component))
	{
		if (const UStaticMesh* StaticMesh = StaticMeshComponent->GetStaticMesh())
		{
			const TArray<FStaticMaterial>& StaticMaterials = StaticMesh->GetStaticMaterials();
			if (StaticMaterials.Num() > 0)
			{
				Slots.Reserve(StaticMaterials.Num());
				for (int32 MaterialSlotIndex = 0; MaterialSlotIndex < StaticMaterials.Num(); ++MaterialSlotIndex)
				{
					const FStaticMaterial& StaticMaterial = StaticMaterials[MaterialSlotIndex];
					Slots.Add(MakeMaterialSlotInfo(Component, MaterialSlotIndex, StaticMaterial.MaterialSlotName, StaticMaterial.MaterialInterface.Get()));
				}
				return Slots;
			}
		}
	}

	if (const USkinnedMeshComponent* SkinnedMeshComponent = Cast<USkinnedMeshComponent>(Component))
	{
		if (const USkinnedAsset* SkinnedAsset = SkinnedMeshComponent->GetSkinnedAsset())
		{
			const TArray<FSkeletalMaterial>& SkeletalMaterials = SkinnedAsset->GetMaterials();
			if (SkeletalMaterials.Num() > 0)
			{
				Slots.Reserve(SkeletalMaterials.Num());
				for (int32 MaterialSlotIndex = 0; MaterialSlotIndex < SkeletalMaterials.Num(); ++MaterialSlotIndex)
				{
					const FSkeletalMaterial& SkeletalMaterial = SkeletalMaterials[MaterialSlotIndex];
					Slots.Add(MakeMaterialSlotInfo(Component, MaterialSlotIndex, SkeletalMaterial.MaterialSlotName, SkeletalMaterial.MaterialInterface.Get()));
				}
				return Slots;
			}
		}
	}

	const int32 NumMaterialSlots = Component->GetNumMaterials();
	Slots.Reserve(NumMaterialSlots);
	for (int32 MaterialSlotIndex = 0; MaterialSlotIndex < NumMaterialSlots; ++MaterialSlotIndex)
	{
		Slots.Add(MakeMaterialSlotInfo(Component, MaterialSlotIndex, GetMaterialSlotName(Component, MaterialSlotIndex), Component->GetMaterial(MaterialSlotIndex)));
	}
	return Slots;
}

TArray<int32> GetVisibleTextureSetIndices(const UQuickSDFAsset* Asset, const UMeshComponent* Component)
{
	TArray<int32> TextureSetIndices;
	if (!Asset)
	{
		return TextureSetIndices;
	}

	if (!Component)
	{
		TextureSetIndices.Reserve(Asset->TextureSets.Num());
		for (int32 TextureSetIndex = 0; TextureSetIndex < Asset->TextureSets.Num(); ++TextureSetIndex)
		{
			TextureSetIndices.Add(TextureSetIndex);
		}
		return TextureSetIndices;
	}

	for (const FMaterialSlotInfo& SlotInfo : GetMaterialSlotsForComponent(Component))
	{
		const int32 TextureSetIndex = FindTextureSetForMaterialSlot(Asset, SlotInfo.MaterialSlotIndex);
		if (TextureSetIndex != INDEX_NONE)
		{
			TextureSetIndices.Add(TextureSetIndex);
		}
	}
	return TextureSetIndices;
}

bool HasVisibleTextureSet(const UQuickSDFAsset* Asset, const UMeshComponent* Component)
{
	return GetVisibleTextureSetIndices(Asset, Component).Num() > 0;
}

void ResetPropertiesForNoTarget(UQuickSDFToolProperties* Properties)
{
	if (!Properties)
	{
		return;
	}

	Properties->TargetAsset = nullptr;
	Properties->ActiveTextureSetIndex = INDEX_NONE;
	Properties->TargetMaterialSlot = INDEX_NONE;
	Properties->BakeAngleOffsetDegrees = 0.0f;
	Properties->NumAngles = 0;
	Properties->TargetAngles.Reset();
	Properties->TargetTextures.Reset();
}

void InitializeDefaultAngleData(TArray<FQuickSDFAngleData>& AngleData, const UQuickSDFToolProperties* Properties, bool bResetExisting)
{
	if (!bResetExisting && AngleData.Num() > 0)
	{
		return;
	}

	const bool bSymmetry = !Properties || Properties->UsesFrontHalfAngles();
	const float MaxAngle = bSymmetry ? 90.0f : 180.0f;
	const int32 AngleCount = GetQuickSDFDefaultAngleCount(bSymmetry);
	AngleData.Reset();
	AngleData.Reserve(AngleCount);

	for (int32 Index = 0; Index < AngleCount; ++Index)
	{
		FQuickSDFAngleData Data;
		Data.Angle = AngleCount > 1
			? (static_cast<float>(Index) / static_cast<float>(AngleCount - 1)) * MaxAngle
			: 0.0f;
		Data.MaskGuid = FGuid::NewGuid();
		AngleData.Add(Data);
	}
}

void SyncPropertiesFromActiveAsset(UQuickSDFToolProperties* Properties, UQuickSDFAsset* Asset)
{
	if (!Properties || !Asset)
	{
		ResetPropertiesForNoTarget(Properties);
		return;
	}

	Properties->TargetAsset = Asset;
	Properties->ActiveTextureSetIndex = Asset->ActiveTextureSetIndex;
	const FQuickSDFTextureSetData* ActiveSet = Asset->GetActiveTextureSet();
	if (!ActiveSet)
	{
		Properties->Resolution = Asset->Resolution;
		Properties->UVChannel = Asset->UVChannel;
		Properties->TargetMaterialSlot = INDEX_NONE;
		Properties->BakeAngleOffsetDegrees = 0.0f;
		Properties->NumAngles = 0;
		Properties->TargetAngles.Reset();
		Properties->TargetTextures.Reset();
		return;
	}

	EnsureMaskGuids(Asset);
	Properties->Resolution = ActiveSet->Resolution;
	Properties->UVChannel = ActiveSet->UVChannel;
	Properties->TargetMaterialSlot = ActiveSet->MaterialSlotIndex;
	Properties->BakeAngleOffsetDegrees = FMath::Clamp(ActiveSet->BakeAngleOffsetDegrees, 0.0f, 90.0f);
	Properties->NumAngles = ActiveSet->AngleDataList.Num();
	Properties->TargetAngles.SetNum(Properties->NumAngles);
	Properties->TargetTextures.SetNum(Properties->NumAngles);

	for (int32 Index = 0; Index < Properties->NumAngles; ++Index)
	{
		Properties->TargetAngles[Index] = ActiveSet->AngleDataList[Index].Angle;
		Properties->TargetTextures[Index] = ActiveSet->AngleDataList[Index].TextureMask;
	}
}

void RefreshTextureSetsForComponent(
	UQuickSDFAsset* Asset,
	UMeshComponent* Component,
	UQuickSDFToolProperties* Properties,
	UWorld* World,
	bool bInitializeRenderTargets,
	const UQuickSDFPaintTool* PaintToolForBakeNormalization)
{
	if (!Properties || !Asset)
	{
		ResetPropertiesForNoTarget(Properties);
		return;
	}

	Asset->MigrateLegacyDataToTextureSetsIfNeeded();

	if (Component)
	{
		TArray<int32> VisibleTextureSetIndices;
		for (const FMaterialSlotInfo& SlotInfo : GetMaterialSlotsForComponent(Component))
		{
			const int32 MaterialSlotIndex = SlotInfo.MaterialSlotIndex;
			int32 TextureSetIndex = FindTextureSetForMaterialSlot(Asset, MaterialSlotIndex);
			if (TextureSetIndex == INDEX_NONE && MaterialSlotIndex == 0 && Asset->TextureSets.Num() == 1 && Asset->TextureSets[0].MaterialSlotIndex == INDEX_NONE)
			{
				TextureSetIndex = 0;
				Asset->TextureSets[0].MaterialSlotIndex = MaterialSlotIndex;
			}

			if (TextureSetIndex == INDEX_NONE)
			{
				TextureSetIndex = Asset->TextureSets.AddDefaulted();
				FQuickSDFTextureSetData& NewSet = Asset->TextureSets[TextureSetIndex];
				NewSet.MaterialSlotIndex = MaterialSlotIndex;
				NewSet.UVChannel = Properties->UVChannel;
				NewSet.Resolution = Properties->Resolution;
				InitializeDefaultAngleData(NewSet.AngleDataList, Properties, true);
			}

			FQuickSDFTextureSetData& TextureSet = Asset->TextureSets[TextureSetIndex];
			TextureSet.SlotName = SlotInfo.SlotName;
			TextureSet.MaterialName = SlotInfo.MaterialName;
			if (TextureSet.Resolution.X <= 0 || TextureSet.Resolution.Y <= 0)
			{
				TextureSet.Resolution = Properties->Resolution;
			}
			if (TextureSet.AngleDataList.Num() == 0)
			{
				InitializeDefaultAngleData(TextureSet.AngleDataList, Properties, true);
			}
			NormalizeTextureSetBakeState(PaintToolForBakeNormalization, TextureSet);
			VisibleTextureSetIndices.Add(TextureSetIndex);
		}

		if (!VisibleTextureSetIndices.Contains(Asset->ActiveTextureSetIndex))
		{
			Asset->ActiveTextureSetIndex = VisibleTextureSetIndices.Num() > 0 ? VisibleTextureSetIndices[0] : INDEX_NONE;
		}
	}

	if (!Asset->TextureSets.IsValidIndex(Asset->ActiveTextureSetIndex))
	{
		Asset->ActiveTextureSetIndex = INDEX_NONE;
	}

	if (Asset->TextureSets.IsValidIndex(Asset->ActiveTextureSetIndex))
	{
		if (!Component || IsMaterialSlotVisibleForComponent(Component, Asset->TextureSets[Asset->ActiveTextureSetIndex].MaterialSlotIndex))
		{
			Asset->SetActiveTextureSetIndex(Asset->ActiveTextureSetIndex);
		}
		else
		{
			Asset->ActiveTextureSetIndex = INDEX_NONE;
		}
	}

	if (bInitializeRenderTargets && Asset->TextureSets.IsValidIndex(Asset->ActiveTextureSetIndex))
	{
		Asset->InitializeRenderTargets(World);
	}

	SyncPropertiesFromActiveAsset(Properties, Asset);
}
}
