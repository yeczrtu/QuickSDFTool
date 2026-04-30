#include "QuickSDFToolStyle.h"

#include "Brushes/SlateImageBrush.h"
#include "Interfaces/IPluginManager.h"
#include "Styling/AppStyle.h"
#include "Styling/SlateStyle.h"
#include "Styling/SlateStyleMacros.h"
#include "Styling/SlateStyleRegistry.h"

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
	StyleSet->Set("QuickSDF.Action.ImportMasks", new IMAGE_BRUSH_SVG("Icons/QuickSDFImport", Icon16x16));
	StyleSet->Set("QuickSDF.Action.SaveAsset", new IMAGE_BRUSH_SVG("Icons/QuickSDFSave", Icon16x16));
	StyleSet->Set("QuickSDF.Action.ExportMasks", new IMAGE_BRUSH_SVG("Icons/QuickSDFExport", Icon16x16));
	StyleSet->Set("QuickSDF.Action.Rebake", new IMAGE_BRUSH_SVG("Icons/QuickSDFRebake", Icon16x16));
	StyleSet->Set("QuickSDF.Action.FillWhite", new IMAGE_BRUSH_SVG("Icons/QuickSDFFillWhite", Icon16x16));
	StyleSet->Set("QuickSDF.Action.FillBlack", new IMAGE_BRUSH_SVG("Icons/QuickSDFFillBlack", Icon16x16));
	StyleSet->Set("QuickSDF.Action.CompleteToEight", new IMAGE_BRUSH_SVG("Icons/QuickSDFComplete", Icon16x16));
	StyleSet->Set("QuickSDF.Action.Redistribute", new IMAGE_BRUSH_SVG("Icons/QuickSDFRedistribute", Icon16x16));
	StyleSet->Set("QuickSDF.Action.AddKey", new IMAGE_BRUSH_SVG("Icons/QuickSDFAdd", Icon16x16));
	StyleSet->Set("QuickSDF.Action.DeleteKey", new IMAGE_BRUSH_SVG("Icons/QuickSDFDelete", Icon16x16));
	StyleSet->Set("QuickSDF.Action.Snap", new IMAGE_BRUSH_SVG("Icons/QuickSDFSnap", Icon16x16));

	StyleSet->Set("QuickSDF.Toggle.PaintAllAngles", new IMAGE_BRUSH_SVG("Icons/QuickSDFPaintAll", Icon16x16));
	StyleSet->Set("QuickSDF.Toggle.AutoSyncLight", new IMAGE_BRUSH_SVG("Icons/QuickSDFAutoLight", Icon16x16));
	StyleSet->Set("QuickSDF.Toggle.ShowPreview", new IMAGE_BRUSH_SVG("Icons/QuickSDFPreview", Icon16x16));
	StyleSet->Set("QuickSDF.Toggle.OverlayUV", new IMAGE_BRUSH_SVG("Icons/QuickSDFUV", Icon16x16));
	StyleSet->Set("QuickSDF.Toggle.OverlayOriginalShadow", new IMAGE_BRUSH_SVG("Icons/QuickSDFShadow", Icon16x16));
	StyleSet->Set("QuickSDF.Toggle.OnionSkin", new IMAGE_BRUSH_SVG("Icons/QuickSDFOnion", Icon16x16));
	StyleSet->Set("QuickSDF.Toggle.QuickLine", new IMAGE_BRUSH_SVG("Icons/QuickSDFQuickStroke", Icon16x16));
	StyleSet->Set("QuickSDF.Toggle.Symmetry", new IMAGE_BRUSH_SVG("Icons/QuickSDFSymmetry", Icon16x16));

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
