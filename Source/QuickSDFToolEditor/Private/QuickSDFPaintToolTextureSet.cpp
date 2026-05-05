#include "QuickSDFPaintTool.h"

#include "Components/MeshComponent.h"
#include "Engine/Texture2D.h"
#include "InteractiveToolManager.h"
#include "Materials/MaterialInterface.h"
#include "QuickSDFAsset.h"
#include "QuickSDFPaintToolPrivate.h"
#include "QuickSDFTextureSetSync.h"
#include "QuickSDFToolSubsystem.h"

#if WITH_EDITOR
#include "Editor.h"
#endif

#define LOCTEXT_NAMESPACE "QuickSDFPaintToolTextureSet"

using namespace QuickSDFPaintToolPrivate;

void UQuickSDFPaintTool::InitializeDefaultAngleData(TArray<FQuickSDFAngleData>& AngleData, bool bResetExisting) const
{
	QuickSDFTextureSetSync::InitializeDefaultAngleData(AngleData, Properties, bResetExisting);
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

	QuickSDFTextureSetSync::RefreshTextureSetsForComponent(
		Asset,
		CurrentComponent.Get(),
		Properties,
		GetToolManager()->GetContextQueriesAPI()->GetCurrentEditingWorld(),
		true,
		this);
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

	if (!Asset->SetActiveTextureSetIndex(TextureSetIndex))
	{
		return false;
	}

	FQuickSDFTextureSetData* ActiveSet = Asset->GetActiveTextureSet();
	if (ActiveSet && ActiveSet->AngleDataList.Num() == 0)
	{
		Asset->Modify();
		InitializeDefaultAngleData(ActiveSet->AngleDataList, true);
		Asset->SyncLegacyFromActiveTextureSet();
	}

	Properties->ActiveTextureSetIndex = TextureSetIndex;
	Properties->TargetMaterialSlot = ActiveSet ? ActiveSet->MaterialSlotIndex : INDEX_NONE;
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

void UQuickSDFPaintTool::GenerateSelectedTextureSetSDF()
{
	GenerateSDF();
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

FText UQuickSDFPaintTool::GetTextureSetStatusTooltip(int32 TextureSetIndex) const
{
	UQuickSDFToolSubsystem* Subsystem = GEditor ? GEditor->GetEditorSubsystem<UQuickSDFToolSubsystem>() : nullptr;
	const UQuickSDFAsset* Asset = Subsystem ? Subsystem->GetActiveSDFAsset() : nullptr;
	if (!Asset || !Asset->TextureSets.IsValidIndex(TextureSetIndex))
	{
		return LOCTEXT("TextureSetMissingTooltip", "This material slot is not available.");
	}

	const FQuickSDFTextureSetData& TextureSet = Asset->TextureSets[TextureSetIndex];
	if (TextureSet.bDirty)
	{
		return LOCTEXT("TextureSetDirtyTooltip", "This material slot has mask edits that have not been baked or generated yet.");
	}
	if (TextureSet.bInitialBakeComplete)
	{
		return LOCTEXT("TextureSetBakedTooltip", "This material slot has generated SDF data.");
	}
	return LOCTEXT("TextureSetEmptyTooltip", "This material slot has no baked mask data yet.");
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
