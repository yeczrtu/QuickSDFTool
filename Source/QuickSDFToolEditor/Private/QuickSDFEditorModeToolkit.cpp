#include "QuickSDFEditorModeToolkit.h"
#include "Engine/Selection.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Text/STextBlock.h"
#include "EditorModeManager.h"
#include "QuickSDFPreviewWidget.h"
#include "Editor.h"

#define LOCTEXT_NAMESPACE "FQuickSDFEditorModeToolkit"

FQuickSDFEditorModeToolkit::FQuickSDFEditorModeToolkit()
{
	UClass* WidgetClass = StaticLoadClass(UUserWidget::StaticClass(), nullptr, TEXT("/QuickSDFTool/Widget/WBP_TexturePreview.WBP_TexturePreview_C"));
	if (WidgetClass)
	{
		if (!PreviewWidgetInstance.IsValid())
		{
			UWorld* World = GLevelEditorModeTools().GetWorld();
			PreviewWidgetInstance = TStrongObjectPtr<UQuickSDFPreviewWidget>(CreateWidget<UQuickSDFPreviewWidget>(World, WidgetClass));
		}
	}
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
	PaletteNames.Add(FName(TEXT("Default")));
}
/*TSharedPtr<SWidget> FQuickSDFEditorModeToolkit::GetInlineContent() const
{
	TSharedPtr<SWidget> Dst = FModeToolkit::GetInlineContent();
	
	if (PreviewWidgetInstance.IsValid())
	{
		return SNew(SVerticalBox)
			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				Dst.ToSharedRef()
			]
			+ SVerticalBox::Slot()
			.FillHeight(1.0f)
			[
				PreviewWidgetInstance->TakeWidget()
			];
	}
	
	return Dst;
}*/

#undef LOCTEXT_NAMESPACE
