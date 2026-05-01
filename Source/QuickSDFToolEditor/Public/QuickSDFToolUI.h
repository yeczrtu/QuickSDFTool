#pragma once

#include "CoreMinimal.h"
#include "Widgets/SWidget.h"

class UQuickSDFPaintTool;
class UQuickSDFToolProperties;
enum class EQuickSDFPaintTargetMode : uint8;

enum class EQuickSDFPaintToggle : uint8
{
	AutoSyncLight,
	ShowPreview,
	OverlayUV,
	OverlayOriginalShadow,
	OnionSkin,
	QuickLine,
	Symmetry,
	MonotonicGuard,
};

namespace QuickSDFToolUI
{
using FGetPaintTool = TFunction<UQuickSDFPaintTool*()>;

const TArray<EQuickSDFPaintToggle>& GetPaintToggles();
const TArray<EQuickSDFPaintTargetMode>& GetPaintTargetModes();
UQuickSDFPaintTool* GetActivePaintTool();
EQuickSDFPaintTargetMode GetPaintTargetMode(const UQuickSDFToolProperties* Properties);
FText GetPaintTargetModeLabel(EQuickSDFPaintTargetMode Mode);
FText GetPaintTargetModeDescription(EQuickSDFPaintTargetMode Mode);
FName GetPaintTargetModeIconName(EQuickSDFPaintTargetMode Mode);
void SetPaintTargetMode(UQuickSDFPaintTool* Tool, UQuickSDFToolProperties* Properties, EQuickSDFPaintTargetMode Mode);
void CyclePaintTargetMode(UQuickSDFPaintTool* Tool, UQuickSDFToolProperties* Properties);
FName GetTogglePropertyName(EQuickSDFPaintToggle Toggle);
FText GetToggleLabel(EQuickSDFPaintToggle Toggle);
FText GetToggleDescription(EQuickSDFPaintToggle Toggle);
FName GetToggleIconName(EQuickSDFPaintToggle Toggle);
bool GetToggleValue(const UQuickSDFToolProperties* Properties, EQuickSDFPaintToggle Toggle);
void SetToggleValue(UQuickSDFPaintTool* Tool, UQuickSDFToolProperties* Properties, EQuickSDFPaintToggle Toggle, bool bValue);
void ToggleValue(UQuickSDFPaintTool* Tool, UQuickSDFToolProperties* Properties, EQuickSDFPaintToggle Toggle);
TSharedRef<SWidget> MakeIconLabelButton(const FName IconName, const FText& Label, const FText& ToolTip, FOnClicked OnClicked);
TSharedRef<SWidget> MakePaintTargetModeSelector(FGetPaintTool GetPaintTool, TWeakObjectPtr<UQuickSDFToolProperties> FallbackProperties = TWeakObjectPtr<UQuickSDFToolProperties>());
TSharedRef<SWidget> MakePaintToggleButton(EQuickSDFPaintToggle Toggle, FGetPaintTool GetPaintTool, TWeakObjectPtr<UQuickSDFToolProperties> FallbackProperties = TWeakObjectPtr<UQuickSDFToolProperties>());
TSharedRef<SWidget> MakePaintToggleBar(FGetPaintTool GetPaintTool, TWeakObjectPtr<UQuickSDFToolProperties> FallbackProperties = TWeakObjectPtr<UQuickSDFToolProperties>());
TSharedRef<SWidget> MakeQuickToggleMenu(FGetPaintTool GetPaintTool);
void ShowQuickToggleMenu(TSharedRef<SWidget> ParentWidget, const FVector2D& ScreenPosition, FGetPaintTool GetPaintTool);
}
