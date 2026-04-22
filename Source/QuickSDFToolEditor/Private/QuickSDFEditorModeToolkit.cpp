#include "QuickSDFEditorModeToolkit.h"
#include "QuickSDFEditorMode.h"
#include "Engine/Selection.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Text/STextBlock.h"
#include "EditorModeManager.h"

#define LOCTEXT_NAMESPACE "FQuickSDFEditorModeToolkit"

FQuickSDFEditorModeToolkit::FQuickSDFEditorModeToolkit()
{
}

void FQuickSDFEditorModeToolkit::Init(const TSharedPtr<IToolkitHost>& InitToolkitHost)
{
	FModeToolkit::Init(InitToolkitHost);
}

FName FQuickSDFEditorModeToolkit::GetToolkitFName() const
{
	return FName("QuickSDFEditorMode");
}

FText FQuickSDFEditorModeToolkit::GetBaseToolkitName() const
{
	return INVTEXT("Quick SDF Tool");
}

void FQuickSDFEditorModeToolkit::GetToolPaletteNames(TArray<FName>& PaletteNames) const
{
	PaletteNames.Add(FName(TEXT("test")));
	PaletteNames.Add(FName(TEXT("aiueo")));
}

FText FQuickSDFEditorModeToolkit::GetToolPaletteDisplayName(FName Palette) const
{
	if (Palette == FName(TEXT("test")))
	{
		return LOCTEXT("test", "test");
	}
	if (Palette == FName(TEXT("aiueo")))
	{
		return LOCTEXT("aaaaa", "aiueo");
	}
	return FText();
}

#undef LOCTEXT_NAMESPACE
