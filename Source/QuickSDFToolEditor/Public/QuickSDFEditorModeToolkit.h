#pragma once

#include "CoreMinimal.h"
#include "Toolkits/BaseToolkit.h"
#include "QuickSDFEditorMode.h"

class UQuickSDFPreviewWidget;
class FQuickSDFEditorModeToolkit : public FModeToolkit
{
public:
	FQuickSDFEditorModeToolkit();

	/** FModeToolkit interface */
	virtual void Init(const TSharedPtr<IToolkitHost>& InitToolkitHost) override;

	/** IToolkit interface */
	virtual FName GetToolkitFName() const override;
	virtual FText GetBaseToolkitName() const override;
	// 横のUIのタブ
	virtual void GetToolPaletteNames(TArray<FName>& PaletteNames) const override;
	virtual FText GetToolPaletteDisplayName(FName Palette) const override;
	virtual TSharedPtr<SWidget> GetInlineContent() const override;
private:
	TStrongObjectPtr<UQuickSDFPreviewWidget> PreviewWidgetInstance;
};
