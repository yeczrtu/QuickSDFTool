#include "QuickSDFToolStyle.h"

#include "Brushes/SlateImageBrush.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Interfaces/IPluginManager.h"
#include "Styling/AppStyle.h"
#include "Styling/SlateTypes.h"
#include "Styling/SlateStyle.h"
#include "Styling/SlateStyleMacros.h"
#include "Styling/SlateStyleRegistry.h"
#include "Styling/StyleColors.h"

#define IMAGE_PLUGIN_BRUSH(RelativePath, ...) FSlateImageBrush(FQuickSDFToolStyle::InContent(RelativePath, ".png"), __VA_ARGS__)
#define RootToContentDir StyleSet->RootToContentDir

TSharedPtr<FSlateStyleSet> FQuickSDFToolStyle::StyleSet = nullptr;

FString FQuickSDFToolStyle::InContent(const FString& RelativePath, const ANSICHAR* Extension)
{
	static FString ContentDir = IPluginManager::Get().FindPlugin(TEXT("QuickSDFTool"))->GetContentDir();
	return (ContentDir / RelativePath) + Extension;
}

void FQuickSDFToolStyle::Initialize()
{
	if (StyleSet.IsValid())
	{
		return;
	}

	const FVector2D Icon16x16(16.0f, 16.0f);
	const FVector2D Icon20x20(20.0f, 20.0f);
	const FVector2D Icon40x40(40.0f, 40.0f);

	StyleSet = MakeShared<FSlateStyleSet>(GetStyleSetName());
	StyleSet->SetParentStyleName(FAppStyle::GetAppStyleSetName());
	StyleSet->SetContentRoot(IPluginManager::Get().FindPlugin(TEXT("QuickSDFTool"))->GetContentDir());

	StyleSet->Set("LevelEditor.QuickSDFToolMode", new IMAGE_BRUSH_SVG("Icons/QuickSDFMode", Icon20x20));
	StyleSet->Set("LevelEditor.QuickSDFToolMode.Small", new IMAGE_BRUSH_SVG("Icons/QuickSDFMode", Icon16x16));

	StyleSet->Set("QuickSDF.SelectTextureAsset", new IMAGE_BRUSH_SVG("Icons/QuickSDFSelect", Icon20x20));
	StyleSet->Set("QuickSDF.SelectTextureAsset.Small", new IMAGE_BRUSH_SVG("Icons/QuickSDFSelect", Icon40x40));
	StyleSet->Set("QuickSDF.PaintTextureColor", new IMAGE_BRUSH_SVG("Icons/QuickSDFPaint", Icon20x20));
	StyleSet->Set("QuickSDF.PaintTextureColor.Small", new IMAGE_BRUSH_SVG("Icons/QuickSDFPaint", Icon40x40));
	StyleSet->Set("QuickSDF.GenerateSDF", new IMAGE_BRUSH_SVG("Icons/QuickSDFGenerate", Icon20x20));
	StyleSet->Set("QuickSDF.GenerateSDF.Small", new IMAGE_BRUSH_SVG("Icons/QuickSDFGenerate", Icon20x20));

	StyleSet->Set("QuickSDF.Action.CreateThresholdMap", new IMAGE_BRUSH_SVG("Icons/QuickSDFGenerate", Icon16x16));
	StyleSet->Set("QuickSDF.Action.Bake", new IMAGE_BRUSH_SVG("Icons/QuickSDFBake", Icon16x16));
	StyleSet->Set("QuickSDF.Action.ImportMasks", new IMAGE_BRUSH_SVG("Icons/QuickSDFImport", Icon16x16));
	StyleSet->Set("QuickSDF.Action.SaveAsset", new IMAGE_BRUSH_SVG("Icons/QuickSDFSave", Icon16x16));
	StyleSet->Set("QuickSDF.Action.ExportMasks", new IMAGE_BRUSH_SVG("Icons/QuickSDFExport", Icon16x16));
	StyleSet->Set("QuickSDF.Action.Rebake", new IMAGE_BRUSH_SVG("Icons/QuickSDFRebake", Icon16x16));
	StyleSet->Set("QuickSDF.Action.FillWhite", new IMAGE_BRUSH_SVG("Icons/QuickSDFFillWhite", Icon16x16));
	StyleSet->Set("QuickSDF.Action.FillBlack", new IMAGE_BRUSH_SVG("Icons/QuickSDFFillBlack", Icon16x16));
	StyleSet->Set("QuickSDF.Action.CompleteToEight", new IMAGE_BRUSH_SVG("Icons/QuickSDFComplete", Icon16x16));
	StyleSet->Set("QuickSDF.Action.Redistribute", new IMAGE_BRUSH_SVG("Icons/QuickSDFRedistribute", Icon16x16));
	StyleSet->Set("QuickSDF.Action.AddKey", new IMAGE_BRUSH_SVG("Icons/QuickSDFAdd", Icon16x16));
	StyleSet->Set("QuickSDF.Action.DuplicateKey", new IMAGE_BRUSH_SVG("Icons/QuickSDFDuplicate", Icon16x16));
	StyleSet->Set("QuickSDF.Action.DeleteKey", new IMAGE_BRUSH_SVG("Icons/QuickSDFDelete", Icon16x16));

	StyleSet->Set("QuickSDF.PaintTarget.Current", new IMAGE_BRUSH_SVG("Icons/QuickSDFPaintCurrent", Icon16x16));
	StyleSet->Set("QuickSDF.PaintTarget.All", new IMAGE_BRUSH_SVG("Icons/QuickSDFPaintAll", Icon16x16));
	StyleSet->Set("QuickSDF.PaintTarget.Before", new IMAGE_BRUSH_SVG("Icons/QuickSDFPaintBefore", Icon16x16));
	StyleSet->Set("QuickSDF.PaintTarget.After", new IMAGE_BRUSH_SVG("Icons/QuickSDFPaintAfter", Icon16x16));

	StyleSet->Set("QuickSDF.Toggle.PaintAllAngles", new IMAGE_BRUSH_SVG("Icons/QuickSDFPaintAll", Icon16x16));
	StyleSet->Set("QuickSDF.Toggle.AutoSyncLight", new IMAGE_BRUSH_SVG("Icons/QuickSDFAutoLight", Icon16x16));
	StyleSet->Set("QuickSDF.Toggle.ShowPreview", new IMAGE_BRUSH_SVG("Icons/QuickSDFPreview", Icon16x16));
	StyleSet->Set("QuickSDF.Toggle.OverlayUV", new IMAGE_BRUSH_SVG("Icons/QuickSDFUV", Icon16x16));
	StyleSet->Set("QuickSDF.Toggle.OnionSkin", new IMAGE_BRUSH_SVG("Icons/QuickSDFOnion", Icon16x16));
	StyleSet->Set("QuickSDF.Toggle.QuickLine", new IMAGE_BRUSH_SVG("Icons/QuickSDFQuickStroke", Icon16x16));
	StyleSet->Set("QuickSDF.Toggle.Symmetry", new IMAGE_BRUSH_SVG("Icons/QuickSDFSymmetry", Icon16x16));
	StyleSet->Set("QuickSDF.Toggle.MonotonicGuard", new IMAGE_BRUSH_SVG("Icons/QuickSDFSnap", Icon16x16));
	StyleSet->Set("QuickSDF.MaterialPreview.OriginalPaint", new IMAGE_BRUSH_SVG("Icons/QuickSDFPreviewOriginalPaint", Icon16x16));
	StyleSet->Set("QuickSDF.MaterialPreview.Painted", new IMAGE_BRUSH_SVG("Icons/QuickSDFPreviewPainted", Icon16x16));
	StyleSet->Set("QuickSDF.MaterialPreview.PaintUV", new IMAGE_BRUSH_SVG("Icons/QuickSDFPreviewPaintUV", Icon16x16));
	StyleSet->Set("QuickSDF.MaterialPreview.PaintShadow", new IMAGE_BRUSH_SVG("Icons/QuickSDFPreviewPaintShadow", Icon16x16));

	const float TimelineButtonRadius = 3.0f;
	const FLinearColor TimelineBorderColor = FStyleColors::DropdownOutline.GetSpecifiedColor();

	const FButtonStyle TimelineButtonStyle = FButtonStyle()
		.SetNormal(FSlateRoundedBoxBrush(FStyleColors::Dropdown, TimelineButtonRadius, TimelineBorderColor, 1.0f))
		.SetHovered(FSlateRoundedBoxBrush(FStyleColors::Hover, TimelineButtonRadius, TimelineBorderColor, 1.0f))
		.SetPressed(FSlateRoundedBoxBrush(FStyleColors::Recessed, TimelineButtonRadius, TimelineBorderColor, 1.0f))
		.SetNormalForeground(FStyleColors::Foreground)
		.SetHoveredForeground(FStyleColors::ForegroundHover)
		.SetPressedForeground(FStyleColors::ForegroundHover)
		.SetDisabledForeground(FStyleColors::Foreground)
		.SetNormalPadding(FMargin(1.0f))
		.SetPressedPadding(FMargin(1.0f));
	StyleSet->Set("QuickSDF.Timeline.Button", TimelineButtonStyle);

	const FCheckBoxStyle TimelineToggleStyle = FCheckBoxStyle()
		.SetCheckBoxType(ESlateCheckBoxType::ToggleButton)
		.SetUncheckedImage(FSlateRoundedBoxBrush(FStyleColors::Dropdown, TimelineButtonRadius, TimelineBorderColor, 1.0f))
		.SetUncheckedHoveredImage(FSlateRoundedBoxBrush(FStyleColors::Hover, TimelineButtonRadius, TimelineBorderColor, 1.0f))
		.SetUncheckedPressedImage(FSlateRoundedBoxBrush(FStyleColors::Recessed, TimelineButtonRadius, TimelineBorderColor, 1.0f))
		.SetCheckedImage(FSlateRoundedBoxBrush(FStyleColors::Primary, TimelineButtonRadius, TimelineBorderColor, 1.0f))
		.SetCheckedHoveredImage(FSlateRoundedBoxBrush(FStyleColors::PrimaryHover, TimelineButtonRadius, TimelineBorderColor, 1.0f))
		.SetCheckedPressedImage(FSlateRoundedBoxBrush(FStyleColors::PrimaryPress, TimelineButtonRadius, TimelineBorderColor, 1.0f))
		.SetForegroundColor(FStyleColors::Foreground)
		.SetHoveredForegroundColor(FStyleColors::ForegroundHover)
		.SetPressedForegroundColor(FStyleColors::ForegroundHover)
		.SetCheckedForegroundColor(FStyleColors::ForegroundHover)
		.SetCheckedHoveredForegroundColor(FStyleColors::ForegroundHover)
		.SetCheckedPressedForegroundColor(FStyleColors::ForegroundHover)
		.SetPadding(FMargin(2.0f));
	StyleSet->Set("QuickSDF.Timeline.ToggleButton", TimelineToggleStyle);

	const float MaterialSlotRadius = 4.0f;
	const float MaterialSlotActionRadius = 3.0f;
	const FLinearColor MaterialSlotBorderColor(0.18f, 0.18f, 0.18f, 1.0f);
	const FLinearColor MaterialSlotActiveBorderColor(0.24f, 0.57f, 0.72f, 1.0f);

	const FCheckBoxStyle MaterialSlotRowStyle = FCheckBoxStyle()
		.SetCheckBoxType(ESlateCheckBoxType::ToggleButton)
		.SetUncheckedImage(FSlateRoundedBoxBrush(FLinearColor(0.030f, 0.032f, 0.034f, 1.0f), MaterialSlotRadius, MaterialSlotBorderColor, 1.0f))
		.SetUncheckedHoveredImage(FSlateRoundedBoxBrush(FLinearColor(0.055f, 0.058f, 0.062f, 1.0f), MaterialSlotRadius, FLinearColor(0.24f, 0.24f, 0.24f, 1.0f), 1.0f))
		.SetUncheckedPressedImage(FSlateRoundedBoxBrush(FLinearColor(0.020f, 0.022f, 0.025f, 1.0f), MaterialSlotRadius, FLinearColor(0.20f, 0.20f, 0.20f, 1.0f), 1.0f))
		.SetCheckedImage(FSlateRoundedBoxBrush(FLinearColor(0.060f, 0.082f, 0.096f, 1.0f), MaterialSlotRadius, MaterialSlotActiveBorderColor, 1.0f))
		.SetCheckedHoveredImage(FSlateRoundedBoxBrush(FLinearColor(0.075f, 0.105f, 0.122f, 1.0f), MaterialSlotRadius, MaterialSlotActiveBorderColor, 1.0f))
		.SetCheckedPressedImage(FSlateRoundedBoxBrush(FLinearColor(0.045f, 0.066f, 0.080f, 1.0f), MaterialSlotRadius, MaterialSlotActiveBorderColor, 1.0f))
		.SetForegroundColor(FStyleColors::Foreground)
		.SetHoveredForegroundColor(FStyleColors::ForegroundHover)
		.SetPressedForegroundColor(FStyleColors::ForegroundHover)
		.SetCheckedForegroundColor(FStyleColors::ForegroundHover)
		.SetCheckedHoveredForegroundColor(FStyleColors::ForegroundHover)
		.SetCheckedPressedForegroundColor(FStyleColors::ForegroundHover)
		.SetPadding(FMargin(0.0f));
	StyleSet->Set("QuickSDF.MaterialSlot.Row", MaterialSlotRowStyle);

	const FButtonStyle MaterialSlotActionButtonStyle = FButtonStyle()
		.SetNormal(FSlateRoundedBoxBrush(FLinearColor(0.105f, 0.108f, 0.112f, 1.0f), MaterialSlotActionRadius, FLinearColor(0.20f, 0.20f, 0.20f, 1.0f), 1.0f))
		.SetHovered(FSlateRoundedBoxBrush(FLinearColor(0.145f, 0.150f, 0.156f, 1.0f), MaterialSlotActionRadius, FLinearColor(0.31f, 0.31f, 0.31f, 1.0f), 1.0f))
		.SetPressed(FSlateRoundedBoxBrush(FLinearColor(0.070f, 0.074f, 0.080f, 1.0f), MaterialSlotActionRadius, MaterialSlotActiveBorderColor, 1.0f))
		.SetNormalForeground(FStyleColors::Foreground)
		.SetHoveredForeground(FStyleColors::ForegroundHover)
		.SetPressedForeground(FStyleColors::ForegroundHover)
		.SetDisabledForeground(FStyleColors::Foreground)
		.SetNormalPadding(FMargin(1.0f))
		.SetPressedPadding(FMargin(1.0f));
	StyleSet->Set("QuickSDF.MaterialSlot.ActionButton", MaterialSlotActionButtonStyle);

	StyleSet->Set("QuickSDF.MaterialSlot.Status.Baked", new FSlateRoundedBoxBrush(FLinearColor(0.18f, 0.42f, 0.25f, 1.0f), 3.0f, FLinearColor(0.25f, 0.56f, 0.34f, 1.0f), 1.0f));
	StyleSet->Set("QuickSDF.MaterialSlot.Status.Empty", new FSlateRoundedBoxBrush(FLinearColor(0.155f, 0.155f, 0.155f, 1.0f), 3.0f, FLinearColor(0.24f, 0.24f, 0.24f, 1.0f), 1.0f));
	StyleSet->Set("QuickSDF.MaterialSlot.Status.Dirty", new FSlateRoundedBoxBrush(FLinearColor(0.42f, 0.32f, 0.12f, 1.0f), 3.0f, FLinearColor(0.62f, 0.48f, 0.20f, 1.0f), 1.0f));
	StyleSet->Set("QuickSDF.MaterialSlot.Status.Warning", new FSlateRoundedBoxBrush(FLinearColor(0.45f, 0.20f, 0.10f, 1.0f), 3.0f, FLinearColor(0.70f, 0.36f, 0.16f, 1.0f), 1.0f));
	StyleSet->Set("QuickSDF.MaterialSlot.Status.Missing", new FSlateRoundedBoxBrush(FLinearColor(0.13f, 0.13f, 0.13f, 1.0f), 3.0f, FLinearColor(0.22f, 0.22f, 0.22f, 1.0f), 1.0f));
	StyleSet->Set("QuickSDF.MaterialSlot.IndexBadge", new FSlateRoundedBoxBrush(FLinearColor(0.105f, 0.108f, 0.112f, 1.0f), 3.0f, FLinearColor(0.20f, 0.20f, 0.20f, 1.0f), 1.0f));
	StyleSet->Set("QuickSDF.MaterialSlot.IndexBadge.Active", new FSlateRoundedBoxBrush(FLinearColor(0.12f, 0.32f, 0.42f, 1.0f), 3.0f, FLinearColor(0.24f, 0.57f, 0.72f, 1.0f), 1.0f));

	FSlateStyleRegistry::RegisterSlateStyle(*StyleSet.Get());
}

void FQuickSDFToolStyle::Shutdown()
{
	if (StyleSet.IsValid())
	{
		FSlateStyleRegistry::UnRegisterSlateStyle(*StyleSet.Get());
		ensure(StyleSet.IsUnique());
		StyleSet.Reset();
	}
}

TSharedPtr<ISlateStyle> FQuickSDFToolStyle::Get()
{
	return StyleSet;
}

FName FQuickSDFToolStyle::GetStyleSetName()
{
	static FName StyleName(TEXT("QuickSDFToolStyle"));
	return StyleName;
}

const FSlateBrush* FQuickSDFToolStyle::GetBrush(FName PropertyName, const ANSICHAR* Specifier)
{
	return StyleSet.IsValid() ? StyleSet->GetBrush(PropertyName, Specifier) : nullptr;
}

#undef IMAGE_PLUGIN_BRUSH
#undef RootToContentDir
