#include "QuickSDFPaintTool.h"

#include "Components/MeshComponent.h"
#include "Engine/Texture2D.h"
#include "InteractiveToolManager.h"
#include "Materials/MaterialInterface.h"
#include "QuickSDFAsset.h"
#include "QuickSDFPaintToolPrivate.h"
#include "QuickSDFToolSubsystem.h"

#if WITH_EDITOR
#include "Editor.h"
#endif

#define LOCTEXT_NAMESPACE "QuickSDFPaintToolTextureSet"

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

FString GetMaterialDisplayName(const UMeshComponent* Component, int32 MaterialSlotIndex)
{
	UMaterialInterface* Material = Component ? Component->GetMaterial(MaterialSlotIndex) : nullptr;
	return Material ? Material->GetName() : FString(TEXT("None"));
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

bool TextureSetHasNonWhitePaintData(const UQuickSDFPaintTool& Tool, const FQuickSDFTextureSetData& TextureSet)
{
	TArray<FColor> Pixels;
	for (const FQuickSDFAngleData& AngleData : TextureSet.AngleDataList)
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

void NormalizeTextureSetBakeState(const UQuickSDFPaintTool& Tool, FQuickSDFTextureSetData& TextureSet)
{
	if (!TextureSet.bInitialBakeComplete || TextureSetHasStoredBakeData(TextureSet))
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

void UQuickSDFPaintTool::InitializeDefaultAngleData(TArray<FQuickSDFAngleData>& AngleData, bool bResetExisting) const
{
	if (!bResetExisting && AngleData.Num() > 0)
	{
		return;
	}

	const bool bSymmetry = Properties ? Properties->bSymmetryMode : true;
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

void UQuickSDFPaintTool::RefreshTextureSetsForCurrentComponent()
{
	if (!Properties)
	{
		return;
	}

	UQuickSDFToolSubsystem* Subsystem = GEditor ? GEditor->GetEditorSubsystem<UQuickSDFToolSubsystem>() : nullptr;
	UQuickSDFAsset* Asset = Subsystem ? Subsystem->GetActiveSDFAsset() : nullptr;
	if (!Asset)
	{
		return;
	}

	Asset->MigrateLegacyDataToTextureSetsIfNeeded();

	if (CurrentComponent.IsValid())
	{
		const int32 NumMaterialSlots = FMath::Max(CurrentComponent->GetNumMaterials(), 1);
		for (int32 MaterialSlotIndex = 0; MaterialSlotIndex < NumMaterialSlots; ++MaterialSlotIndex)
		{
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
				InitializeDefaultAngleData(NewSet.AngleDataList, true);
			}

			FQuickSDFTextureSetData& TextureSet = Asset->TextureSets[TextureSetIndex];
			TextureSet.SlotName = GetMaterialSlotName(CurrentComponent.Get(), MaterialSlotIndex);
			TextureSet.MaterialName = GetMaterialDisplayName(CurrentComponent.Get(), MaterialSlotIndex);
			if (TextureSet.Resolution.X <= 0 || TextureSet.Resolution.Y <= 0)
			{
				TextureSet.Resolution = Properties->Resolution;
			}
			if (TextureSet.AngleDataList.Num() == 0)
			{
				InitializeDefaultAngleData(TextureSet.AngleDataList, true);
			}
			NormalizeTextureSetBakeState(*this, TextureSet);
		}
	}
	else if (Asset->TextureSets.Num() == 0)
	{
		FQuickSDFTextureSetData& TextureSet = Asset->TextureSets.AddDefaulted_GetRef();
		TextureSet.MaterialSlotIndex = INDEX_NONE;
		TextureSet.SlotName = TEXT("Default");
		TextureSet.MaterialName = TEXT("No Mesh");
		TextureSet.UVChannel = Properties->UVChannel;
		TextureSet.Resolution = Properties->Resolution;
		InitializeDefaultAngleData(TextureSet.AngleDataList, true);
	}

	if (!Asset->TextureSets.IsValidIndex(Asset->ActiveTextureSetIndex))
	{
		Asset->ActiveTextureSetIndex = 0;
	}

	Asset->SetActiveTextureSetIndex(Asset->ActiveTextureSetIndex);
	FQuickSDFTextureSetData* ActiveSet = Asset->GetActiveTextureSet();
	Properties->ActiveTextureSetIndex = Asset->ActiveTextureSetIndex;
	Properties->TargetMaterialSlot = ActiveSet ? ActiveSet->MaterialSlotIndex : INDEX_NONE;
	Properties->bIsolateTargetMaterialSlot = Properties->TargetMaterialSlot >= 0;
	Asset->InitializeRenderTargets(GetToolManager()->GetContextQueriesAPI()->GetCurrentEditingWorld());
	SyncPropertiesFromActiveAsset();
	InvalidateUVOverlayCache();
	ApplyTargetMaterialSlotIsolation();
	RefreshPreviewMaterial();
	++MaskRevision;
}

bool UQuickSDFPaintTool::SelectTextureSet(int32 TextureSetIndex)
{
	if (!Properties)
	{
		return false;
	}

	UQuickSDFToolSubsystem* Subsystem = GEditor ? GEditor->GetEditorSubsystem<UQuickSDFToolSubsystem>() : nullptr;
	UQuickSDFAsset* Asset = Subsystem ? Subsystem->GetActiveSDFAsset() : nullptr;
	if (!Asset || !Asset->TextureSets.IsValidIndex(TextureSetIndex))
	{
		return false;
	}

	Asset->Modify();
	if (!Asset->SetActiveTextureSetIndex(TextureSetIndex))
	{
		return false;
	}

	FQuickSDFTextureSetData* ActiveSet = Asset->GetActiveTextureSet();
	if (ActiveSet && ActiveSet->AngleDataList.Num() == 0)
	{
		InitializeDefaultAngleData(ActiveSet->AngleDataList, true);
		Asset->SyncLegacyFromActiveTextureSet();
	}

	Properties->Modify();
	Properties->ActiveTextureSetIndex = TextureSetIndex;
	Properties->TargetMaterialSlot = ActiveSet ? ActiveSet->MaterialSlotIndex : INDEX_NONE;
	Properties->bIsolateTargetMaterialSlot = Properties->TargetMaterialSlot >= 0;
	Asset->InitializeRenderTargets(GetToolManager()->GetContextQueriesAPI()->GetCurrentEditingWorld());
	SyncPropertiesFromActiveAsset();
	InvalidateUVOverlayCache();
	ApplyTargetMaterialSlotIsolation();
	RefreshPreviewMaterial();
	++MaskRevision;
	return true;
}

void UQuickSDFPaintTool::BakeSelectedTextureSet()
{
	UQuickSDFToolSubsystem* Subsystem = GEditor ? GEditor->GetEditorSubsystem<UQuickSDFToolSubsystem>() : nullptr;
	UQuickSDFAsset* Asset = Subsystem ? Subsystem->GetActiveSDFAsset() : nullptr;
	FQuickSDFTextureSetData* ActiveSet = Asset ? Asset->GetActiveTextureSet() : nullptr;
	if (!Properties || !Asset || !ActiveSet)
	{
		return;
	}

	if (ActiveSet->AngleDataList.Num() == 0)
	{
		InitializeDefaultAngleData(ActiveSet->AngleDataList, true);
		Asset->SyncLegacyFromActiveTextureSet();
	}

	GetToolManager()->BeginUndoTransaction(LOCTEXT("BakeSelectedTextureSet", "Bake Quick SDF Texture Set"));
	Asset->Modify();
	Properties->Modify();
	Asset->InitializeRenderTargets(GetToolManager()->GetContextQueriesAPI()->GetCurrentEditingWorld());
	FillOriginalShadingAll();
	ActiveSet = Asset->GetActiveTextureSet();
	if (ActiveSet)
	{
		ActiveSet->bInitialBakeComplete = true;
		ActiveSet->bDirty = false;
		ActiveSet->bHasWarning = false;
		ActiveSet->WarningMessage = FText::GetEmpty();
	}
	Asset->SyncLegacyFromActiveTextureSet();
	SyncPropertiesFromActiveAsset();
	RefreshPreviewMaterial();
	++MaskRevision;
	GetToolManager()->EndUndoTransaction();
}

void UQuickSDFPaintTool::BakeMissingTextureSets()
{
	UQuickSDFToolSubsystem* Subsystem = GEditor ? GEditor->GetEditorSubsystem<UQuickSDFToolSubsystem>() : nullptr;
	UQuickSDFAsset* Asset = Subsystem ? Subsystem->GetActiveSDFAsset() : nullptr;
	if (!Asset)
	{
		return;
	}

	const int32 OriginalIndex = Asset->ActiveTextureSetIndex;
	for (int32 TextureSetIndex = 0; TextureSetIndex < Asset->TextureSets.Num(); ++TextureSetIndex)
	{
		if (!Asset->TextureSets[TextureSetIndex].bInitialBakeComplete)
		{
			SelectTextureSet(TextureSetIndex);
			BakeSelectedTextureSet();
		}
	}
	SelectTextureSet(OriginalIndex);
}

void UQuickSDFPaintTool::GenerateSelectedTextureSetSDF()
{
	GenerateSDF();
}

void UQuickSDFPaintTool::GenerateAllBakedTextureSets()
{
	UQuickSDFToolSubsystem* Subsystem = GEditor ? GEditor->GetEditorSubsystem<UQuickSDFToolSubsystem>() : nullptr;
	UQuickSDFAsset* Asset = Subsystem ? Subsystem->GetActiveSDFAsset() : nullptr;
	if (!Asset)
	{
		return;
	}

	const int32 OriginalIndex = Asset->ActiveTextureSetIndex;
	for (int32 TextureSetIndex = 0; TextureSetIndex < Asset->TextureSets.Num(); ++TextureSetIndex)
	{
		const FQuickSDFTextureSetData& TextureSet = Asset->TextureSets[TextureSetIndex];
		if (TextureSet.bInitialBakeComplete)
		{
			SelectTextureSet(TextureSetIndex);
			GenerateSDF();
		}
	}
	SelectTextureSet(OriginalIndex);
}

FText UQuickSDFPaintTool::GetActiveTextureSetLabel() const
{
	UQuickSDFToolSubsystem* Subsystem = GEditor ? GEditor->GetEditorSubsystem<UQuickSDFToolSubsystem>() : nullptr;
	const UQuickSDFAsset* Asset = Subsystem ? Subsystem->GetActiveSDFAsset() : nullptr;
	const FQuickSDFTextureSetData* TextureSet = Asset ? Asset->GetActiveTextureSet() : nullptr;
	if (!TextureSet)
	{
		return LOCTEXT("NoTextureSet", "No Texture Set");
	}

	return FText::Format(
		LOCTEXT("TextureSetLabelFormat", "{0}: {1}"),
		FText::AsNumber(TextureSet->MaterialSlotIndex),
		FText::FromName(TextureSet->SlotName));
}

FText UQuickSDFPaintTool::GetTextureSetStatusText(int32 TextureSetIndex) const
{
	UQuickSDFToolSubsystem* Subsystem = GEditor ? GEditor->GetEditorSubsystem<UQuickSDFToolSubsystem>() : nullptr;
	const UQuickSDFAsset* Asset = Subsystem ? Subsystem->GetActiveSDFAsset() : nullptr;
	if (!Asset || !Asset->TextureSets.IsValidIndex(TextureSetIndex))
	{
		return LOCTEXT("TextureSetMissing", "Missing");
	}

	const FQuickSDFTextureSetData& TextureSet = Asset->TextureSets[TextureSetIndex];
	if (TextureSet.bHasWarning)
	{
		return LOCTEXT("TextureSetWarning", "Warning");
	}
	if (TextureSet.bDirty)
	{
		return LOCTEXT("TextureSetDirty", "Dirty");
	}
	if (TextureSet.bInitialBakeComplete)
	{
		return LOCTEXT("TextureSetBaked", "Baked");
	}
	return LOCTEXT("TextureSetEmpty", "Empty");
}

FLinearColor UQuickSDFPaintTool::GetTextureSetStatusColor(int32 TextureSetIndex) const
{
	UQuickSDFToolSubsystem* Subsystem = GEditor ? GEditor->GetEditorSubsystem<UQuickSDFToolSubsystem>() : nullptr;
	const UQuickSDFAsset* Asset = Subsystem ? Subsystem->GetActiveSDFAsset() : nullptr;
	if (!Asset || !Asset->TextureSets.IsValidIndex(TextureSetIndex))
	{
		return FLinearColor(0.45f, 0.45f, 0.45f, 1.0f);
	}

	const FQuickSDFTextureSetData& TextureSet = Asset->TextureSets[TextureSetIndex];
	if (TextureSet.bHasWarning)
	{
		return FLinearColor(1.0f, 0.55f, 0.24f, 1.0f);
	}
	if (TextureSet.bDirty)
	{
		return FLinearColor(1.0f, 0.82f, 0.22f, 1.0f);
	}
	if (TextureSet.bInitialBakeComplete)
	{
		return FLinearColor(0.35f, 0.78f, 0.45f, 1.0f);
	}
	return FLinearColor(0.48f, 0.48f, 0.48f, 1.0f);
}

void UQuickSDFPaintTool::SyncActiveTextureSetFromProperties()
{
	UQuickSDFToolSubsystem* Subsystem = GEditor ? GEditor->GetEditorSubsystem<UQuickSDFToolSubsystem>() : nullptr;
	UQuickSDFAsset* Asset = Subsystem ? Subsystem->GetActiveSDFAsset() : nullptr;
	FQuickSDFTextureSetData* ActiveSet = Asset ? Asset->GetActiveTextureSet() : nullptr;
	if (!Properties || !Asset || !ActiveSet)
	{
		return;
	}

	Asset->SyncLegacyFromActiveTextureSet();
	ActiveSet = Asset->GetActiveTextureSet();
	if (ActiveSet)
	{
		ActiveSet->bDirty = true;
	}
}

#undef LOCTEXT_NAMESPACE
