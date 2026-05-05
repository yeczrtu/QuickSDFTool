#pragma once

#include "CoreMinimal.h"
#include "QuickSDFAsset.h"
#include "QuickSDFToolProperties.h"

class UMeshComponent;
class UQuickSDFPaintTool;

namespace QuickSDFTextureSetSync
{
struct FMaterialSlotInfo
{
	int32 MaterialSlotIndex = INDEX_NONE;
	FName SlotName = NAME_None;
	FString MaterialName;
};

TArray<FMaterialSlotInfo> GetMaterialSlotsForComponent(const UMeshComponent* Component);
TArray<int32> GetVisibleTextureSetIndices(const UQuickSDFAsset* Asset, const UMeshComponent* Component);
bool HasVisibleTextureSet(const UQuickSDFAsset* Asset, const UMeshComponent* Component);
void ResetPropertiesForNoTarget(UQuickSDFToolProperties* Properties);
void InitializeDefaultAngleData(TArray<FQuickSDFAngleData>& AngleData, const UQuickSDFToolProperties* Properties, bool bResetExisting);
void SyncPropertiesFromActiveAsset(UQuickSDFToolProperties* Properties, UQuickSDFAsset* Asset);
void RefreshTextureSetsForComponent(
	UQuickSDFAsset* Asset,
	UMeshComponent* Component,
	UQuickSDFToolProperties* Properties,
	UWorld* World,
	bool bInitializeRenderTargets,
	const UQuickSDFPaintTool* PaintToolForBakeNormalization = nullptr);
}
