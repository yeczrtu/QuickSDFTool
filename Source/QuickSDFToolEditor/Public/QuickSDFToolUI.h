#pragma once

#include "CoreMinimal.h"
#include "Widgets/SWidget.h"

class UQuickSDFPaintTool;
class UQuickSDFToolProperties;

enum class EQuickSDFPaintToggle : uint8
{
	PaintAllAngles,
	AutoSyncLight,
	ShowPreview,
	OverlayUV,
	OverlayOriginalShadow,
	OnionSkin,
	QuickLine,
	Symmetry,
};

namespace QuickSDFToolUI
{
using FGetPaintTool = TFunction<UQuickSDFPaintTool*()>;

const TArray<EQuickSDFPaintToggle>& GetPaintToggles();
UQuickSDFPaintTool* GetActivePaintTool();
FName GetTogglePropertyName(EQuickSDFPaintToggle Toggle);
FText GetToggleLabel(EQuickSDFPaintToggle Toggle);
FText GetToggleDescription(EQuickSDFPaintToggle Toggle);
FName GetToggleIconName(EQuickSDFPaintToggle Toggle);
bool GetToggleValue(const UQuickSDFToolProperties* Properties, EQuickSDFPaintToggle Toggle);
void SetToggleValue(UQuickSDFPaintTool* Tool, UQuickSDFToolProperties* Properties, EQuickSDFPaintToggle Toggle, bool bValue);
void ToggleValue(UQuickSDFPaintTool* Tool, UQuickSDFToolProperties* Properties, EQuickSDFPaintToggle Toggle);
TSharedRef<SWidget> MakeIconLabelButton(const FName IconName, const FText& Label, const FText& ToolTip, FOnClicked OnClicked);
TSharedRef<SWidget> MakePaintToggleButton(EQuickSDFPaintToggle Toggle, FGetPaintTool GetPaintTool, TWeakObjectPtr<UQuickSDFToolProperties> FallbackProperties = TWeakObjectPtr<UQuickSDFToolProperties>());
TSharedRef<SWidget> MakePaintToggleBar(FGetPaintTool GetPaintTool, TWeakObjectPtr<UQuickSDFToolProperties> FallbackProperties = TWeakObjectPtr<UQuickSDFToolProperties>());
TSharedRef<SWidget> MakeQuickToggleMenu(FGetPaintTool GetPaintTool);
void ShowQuickToggleMenu(TSharedRef<SWidget> ParentWidget, const FVector2D& ScreenPosition, FGetPaintTool GetPaintTool);
}
