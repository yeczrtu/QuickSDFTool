#include "QuickSDFToolUI.h"

#include "Editor.h"
#include "EditorModeManager.h"
#include "Framework/Application/SlateApplication.h"
#include "InteractiveToolManager.h"
#include "QuickSDFEditorMode.h"
#include "QuickSDFPaintTool.h"
#include "QuickSDFToolProperties.h"
#include "QuickSDFToolStyle.h"
#include "Styling/AppStyle.h"
#include "Textures/SlateIcon.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SGridPanel.h"
#include "Widgets/Layout/SUniformGridPanel.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "QuickSDFToolUI"

namespace
{
UQuickSDFToolProperties* GetProperties(UQuickSDFPaintTool* Tool, TWeakObjectPtr<UQuickSDFToolProperties> FallbackProperties)
{
	return Tool && Tool->Properties ? Tool->Properties.Get() : FallbackProperties.Get();
}

FText GetToggleToolTip(UQuickSDFToolProperties* Properties, EQuickSDFPaintToggle Toggle)
{
	const bool bEnabled = QuickSDFToolUI::GetToggleValue(Properties, Toggle);
	return FText::Format(
		LOCTEXT("ToggleTooltipFormat", "{0}: {1}\n{2}"),
		QuickSDFToolUI::GetToggleLabel(Toggle),
		bEnabled ? LOCTEXT("ToggleOn", "On") : LOCTEXT("ToggleOff", "Off"),
		QuickSDFToolUI::GetToggleDescription(Toggle));
}

FText GetPaintTargetModeToolTip(UQuickSDFToolProperties* Properties, EQuickSDFPaintTargetMode Mode)
{
	const bool bSelected = QuickSDFToolUI::GetPaintTargetMode(Properties) == Mode;
	return FText::Format(
		LOCTEXT("PaintTargetModeTooltipFormat", "{0}: {1}\n{2}"),
		QuickSDFToolUI::GetPaintTargetModeLabel(Mode),
		bSelected ? LOCTEXT("PaintTargetModeSelected", "Selected") : LOCTEXT("PaintTargetModeClickToUse", "Click to use"),
		QuickSDFToolUI::GetPaintTargetModeDescription(Mode));
}

FText GetMaterialPreviewModeToolTip(UQuickSDFToolProperties* Properties, EQuickSDFMaterialPreviewMode Mode)
{
	const bool bSelected = QuickSDFToolUI::GetMaterialPreviewMode(Properties) == Mode;
	return FText::Format(
		LOCTEXT("MaterialPreviewModeTooltipFormat", "{0}: {1}\n{2}"),
		QuickSDFToolUI::GetMaterialPreviewModeLabel(Mode),
		bSelected ? LOCTEXT("MaterialPreviewModeSelected", "Selected") : LOCTEXT("MaterialPreviewModeClickToUse", "Click to use"),
		QuickSDFToolUI::GetMaterialPreviewModeDescription(Mode));
}
}

const TArray<EQuickSDFPaintToggle>& QuickSDFToolUI::GetPaintToggles()
{
	static const TArray<EQuickSDFPaintToggle> Toggles = {
		EQuickSDFPaintToggle::AutoSyncLight,
		EQuickSDFPaintToggle::ShowPreview,
		EQuickSDFPaintToggle::OverlayUV,
		EQuickSDFPaintToggle::OverlayOriginalShadow,
		EQuickSDFPaintToggle::OnionSkin,
		EQuickSDFPaintToggle::QuickLine,
		EQuickSDFPaintToggle::Symmetry,
		EQuickSDFPaintToggle::MonotonicGuard,
	};
	return Toggles;
}

const TArray<EQuickSDFMaterialPreviewMode>& QuickSDFToolUI::GetMaterialPreviewModes()
{
	static const TArray<EQuickSDFMaterialPreviewMode> Modes = {
		EQuickSDFMaterialPreviewMode::OriginalMaterial,
		EQuickSDFMaterialPreviewMode::Mask,
		EQuickSDFMaterialPreviewMode::UV,
		EQuickSDFMaterialPreviewMode::OriginalShadow,
	};
	return Modes;
}

const TArray<EQuickSDFPaintTargetMode>& QuickSDFToolUI::GetPaintTargetModes()
{
	static const TArray<EQuickSDFPaintTargetMode> Modes = {
		EQuickSDFPaintTargetMode::CurrentOnly,
		EQuickSDFPaintTargetMode::All,
		EQuickSDFPaintTargetMode::BeforeCurrent,
		EQuickSDFPaintTargetMode::AfterCurrent,
	};
	return Modes;
}

UQuickSDFPaintTool* QuickSDFToolUI::GetActivePaintTool()
{
	UQuickSDFEditorMode* Mode = Cast<UQuickSDFEditorMode>(GLevelEditorModeTools().GetActiveScriptableMode("EM_QuickSDFEditorMode"));
	return Mode && Mode->GetToolManager() ? Cast<UQuickSDFPaintTool>(Mode->GetToolManager()->GetActiveTool(EToolSide::Left)) : nullptr;
}

EQuickSDFMaterialPreviewMode QuickSDFToolUI::GetMaterialPreviewMode(const UQuickSDFToolProperties* Properties)
{
	return Properties ? Properties->MaterialPreviewMode : EQuickSDFMaterialPreviewMode::OriginalMaterial;
}

FText QuickSDFToolUI::GetMaterialPreviewModeLabel(EQuickSDFMaterialPreviewMode Mode)
{
	switch (Mode)
	{
	case EQuickSDFMaterialPreviewMode::OriginalMaterial:
		return LOCTEXT("MaterialPreviewOriginalLabel", "Orig+Paint");
	case EQuickSDFMaterialPreviewMode::Mask:
		return LOCTEXT("MaterialPreviewPaintedLabel", "Painted");
	case EQuickSDFMaterialPreviewMode::UV:
		return LOCTEXT("MaterialPreviewUVLabel", "Paint+UV");
	case EQuickSDFMaterialPreviewMode::OriginalShadow:
		return LOCTEXT("MaterialPreviewShadowLabel", "Paint+Shadow");
	default:
		return FText::GetEmpty();
	}
}

FText QuickSDFToolUI::GetMaterialPreviewModeDescription(EQuickSDFMaterialPreviewMode Mode)
{
	switch (Mode)
	{
	case EQuickSDFMaterialPreviewMode::OriginalMaterial:
		return LOCTEXT("MaterialPreviewOriginalDesc", "Overlays the active painted texture on the original material.");
	case EQuickSDFMaterialPreviewMode::Mask:
		return LOCTEXT("MaterialPreviewPaintedDesc", "Shows only the active painted texture with an opaque preview material.");
	case EQuickSDFMaterialPreviewMode::UV:
		return LOCTEXT("MaterialPreviewUVDesc", "Shows the active painted texture over the active UV channel with an opaque preview material.");
	case EQuickSDFMaterialPreviewMode::OriginalShadow:
		return LOCTEXT("MaterialPreviewShadowDesc", "Shows the active painted texture over the original baked shadow with an opaque preview material.");
	default:
		return FText::GetEmpty();
	}
}

FName QuickSDFToolUI::GetMaterialPreviewModeIconName(EQuickSDFMaterialPreviewMode Mode)
{
	switch (Mode)
	{
	case EQuickSDFMaterialPreviewMode::OriginalMaterial:
		return "QuickSDF.MaterialPreview.OriginalPaint";
	case EQuickSDFMaterialPreviewMode::Mask:
		return "QuickSDF.MaterialPreview.Painted";
	case EQuickSDFMaterialPreviewMode::UV:
		return "QuickSDF.MaterialPreview.PaintUV";
	case EQuickSDFMaterialPreviewMode::OriginalShadow:
		return "QuickSDF.MaterialPreview.PaintShadow";
	default:
		return NAME_None;
	}
}

void QuickSDFToolUI::SetMaterialPreviewMode(UQuickSDFPaintTool* Tool, UQuickSDFToolProperties* Properties, EQuickSDFMaterialPreviewMode Mode)
{
	if (!Properties || Properties->MaterialPreviewMode == Mode)
	{
		return;
	}

	Properties->Modify();
	Properties->MaterialPreviewMode = Mode;

	if (Tool)
	{
		FProperty* Prop = Properties->GetClass()->FindPropertyByName(GET_MEMBER_NAME_CHECKED(UQuickSDFToolProperties, MaterialPreviewMode));
		Tool->OnPropertyModified(Properties, Prop);
	}

	if (GEditor)
	{
		GEditor->RedrawAllViewports(false);
	}
}

void QuickSDFToolUI::CycleMaterialPreviewMode(UQuickSDFPaintTool* Tool, UQuickSDFToolProperties* Properties)
{
	if (!Properties)
	{
		return;
	}

	const TArray<EQuickSDFMaterialPreviewMode>& Modes = GetMaterialPreviewModes();
	const int32 CurrentIndex = Modes.IndexOfByKey(GetMaterialPreviewMode(Properties));
	const int32 NextIndex = CurrentIndex == INDEX_NONE ? 0 : (CurrentIndex + 1) % Modes.Num();
	SetMaterialPreviewMode(Tool, Properties, Modes[NextIndex]);
}

EQuickSDFPaintTargetMode QuickSDFToolUI::GetPaintTargetMode(const UQuickSDFToolProperties* Properties)
{
	if (!Properties)
	{
		return EQuickSDFPaintTargetMode::CurrentOnly;
	}

	if (Properties->bPaintAllAngles && Properties->PaintTargetMode == EQuickSDFPaintTargetMode::CurrentOnly)
	{
		return EQuickSDFPaintTargetMode::All;
	}

	return Properties->PaintTargetMode;
}

FText QuickSDFToolUI::GetPaintTargetModeLabel(EQuickSDFPaintTargetMode Mode)
{
	switch (Mode)
	{
	case EQuickSDFPaintTargetMode::CurrentOnly:
		return LOCTEXT("PaintCurrentLabel", "Current");
	case EQuickSDFPaintTargetMode::All:
		return LOCTEXT("PaintAllLabel", "All");
	case EQuickSDFPaintTargetMode::BeforeCurrent:
		return LOCTEXT("PaintBeforeLabel", "Before");
	case EQuickSDFPaintTargetMode::AfterCurrent:
		return LOCTEXT("PaintAfterLabel", "After");
	default:
		return FText::GetEmpty();
	}
}

FText QuickSDFToolUI::GetPaintTargetModeDescription(EQuickSDFPaintTargetMode Mode)
{
	switch (Mode)
	{
	case EQuickSDFPaintTargetMode::CurrentOnly:
		return LOCTEXT("PaintCurrentDesc", "Paints only the currently selected mask angle.");
	case EQuickSDFPaintTargetMode::All:
		return LOCTEXT("PaintAllDesc", "Paints the active stroke into every mask angle.");
	case EQuickSDFPaintTargetMode::BeforeCurrent:
		return LOCTEXT("PaintBeforeDesc", "Paints the current angle and every earlier angle on the timeline.");
	case EQuickSDFPaintTargetMode::AfterCurrent:
		return LOCTEXT("PaintAfterDesc", "Paints the current angle and every later angle on the timeline.");
	default:
		return FText::GetEmpty();
	}
}

FName QuickSDFToolUI::GetPaintTargetModeIconName(EQuickSDFPaintTargetMode Mode)
{
	switch (Mode)
	{
	case EQuickSDFPaintTargetMode::CurrentOnly:
		return "QuickSDF.PaintTarget.Current";
	case EQuickSDFPaintTargetMode::All:
		return "QuickSDF.PaintTarget.All";
	case EQuickSDFPaintTargetMode::BeforeCurrent:
		return "QuickSDF.PaintTarget.Before";
	case EQuickSDFPaintTargetMode::AfterCurrent:
		return "QuickSDF.PaintTarget.After";
	default:
		return NAME_None;
	}
}

void QuickSDFToolUI::SetPaintTargetMode(UQuickSDFPaintTool* Tool, UQuickSDFToolProperties* Properties, EQuickSDFPaintTargetMode Mode)
{
	if (!Properties)
	{
		return;
	}

	const bool bExpectedPaintAll = Mode == EQuickSDFPaintTargetMode::All;
	if (Properties->PaintTargetMode == Mode && Properties->bPaintAllAngles == bExpectedPaintAll)
	{
		return;
	}

	Properties->Modify();
	Properties->PaintTargetMode = Mode;
	Properties->bPaintAllAngles = bExpectedPaintAll;

	if (Tool)
	{
		FProperty* Prop = Properties->GetClass()->FindPropertyByName(GET_MEMBER_NAME_CHECKED(UQuickSDFToolProperties, PaintTargetMode));
		Tool->OnPropertyModified(Properties, Prop);
	}

	if (GEditor)
	{
		GEditor->RedrawAllViewports(false);
	}
}

void QuickSDFToolUI::CyclePaintTargetMode(UQuickSDFPaintTool* Tool, UQuickSDFToolProperties* Properties)
{
	if (!Properties)
	{
		return;
	}

	const TArray<EQuickSDFPaintTargetMode>& Modes = GetPaintTargetModes();
	const int32 CurrentIndex = Modes.IndexOfByKey(GetPaintTargetMode(Properties));
	const int32 NextIndex = CurrentIndex == INDEX_NONE ? 0 : (CurrentIndex + 1) % Modes.Num();
	SetPaintTargetMode(Tool, Properties, Modes[NextIndex]);
}

FName QuickSDFToolUI::GetTogglePropertyName(EQuickSDFPaintToggle Toggle)
{
	switch (Toggle)
	{
	case EQuickSDFPaintToggle::AutoSyncLight:
		return GET_MEMBER_NAME_CHECKED(UQuickSDFToolProperties, bAutoSyncLight);
	case EQuickSDFPaintToggle::ShowPreview:
		return GET_MEMBER_NAME_CHECKED(UQuickSDFToolProperties, bShowPreview);
	case EQuickSDFPaintToggle::OverlayUV:
		return GET_MEMBER_NAME_CHECKED(UQuickSDFToolProperties, bOverlayUV);
	case EQuickSDFPaintToggle::OverlayOriginalShadow:
		return GET_MEMBER_NAME_CHECKED(UQuickSDFToolProperties, bOverlayOriginalShadow);
	case EQuickSDFPaintToggle::OnionSkin:
		return GET_MEMBER_NAME_CHECKED(UQuickSDFToolProperties, bEnableOnionSkin);
	case EQuickSDFPaintToggle::QuickLine:
		return GET_MEMBER_NAME_CHECKED(UQuickSDFToolProperties, bEnableQuickLine);
	case EQuickSDFPaintToggle::Symmetry:
		return GET_MEMBER_NAME_CHECKED(UQuickSDFToolProperties, bSymmetryMode);
	case EQuickSDFPaintToggle::MonotonicGuard:
		return GET_MEMBER_NAME_CHECKED(UQuickSDFToolProperties, bEnableMonotonicGuard);
	default:
		return NAME_None;
	}
}

FText QuickSDFToolUI::GetToggleLabel(EQuickSDFPaintToggle Toggle)
{
	switch (Toggle)
	{
	case EQuickSDFPaintToggle::AutoSyncLight:
		return LOCTEXT("AutoSyncLightLabel", "Auto Light");
	case EQuickSDFPaintToggle::ShowPreview:
		return LOCTEXT("ShowPreviewLabel", "Preview");
	case EQuickSDFPaintToggle::OverlayUV:
		return LOCTEXT("OverlayUVLabel", "UV");
	case EQuickSDFPaintToggle::OverlayOriginalShadow:
		return LOCTEXT("OverlayOriginalShadowLabel", "Shadow");
	case EQuickSDFPaintToggle::OnionSkin:
		return LOCTEXT("OnionSkinLabel", "Onion");
	case EQuickSDFPaintToggle::QuickLine:
		return LOCTEXT("QuickLineLabel", "Quick Stroke");
	case EQuickSDFPaintToggle::Symmetry:
		return LOCTEXT("SymmetryLabel", "Symmetry");
	case EQuickSDFPaintToggle::MonotonicGuard:
		return LOCTEXT("MonotonicGuardLabel", "Guard");
	default:
		return FText::GetEmpty();
	}
}

FText QuickSDFToolUI::GetToggleDescription(EQuickSDFPaintToggle Toggle)
{
	switch (Toggle)
	{
	case EQuickSDFPaintToggle::AutoSyncLight:
		return LOCTEXT("AutoSyncLightDesc", "Moves the preview light when you select a timeline key.");
	case EQuickSDFPaintToggle::ShowPreview:
		return LOCTEXT("ShowPreviewDesc", "Shows the texture preview overlay while painting.");
	case EQuickSDFPaintToggle::OverlayUV:
		return LOCTEXT("OverlayUVDesc", "Draws UV guides over the target surface.");
	case EQuickSDFPaintToggle::OverlayOriginalShadow:
		return LOCTEXT("OverlayOriginalShadowDesc", "Overlays the original baked shading for comparison.");
	case EQuickSDFPaintToggle::OnionSkin:
		return LOCTEXT("OnionSkinDesc", "Shows neighboring mask context while editing.");
	case EQuickSDFPaintToggle::QuickLine:
		return LOCTEXT("QuickLineDesc", "Enables hold-to-line quick stroke drawing.");
	case EQuickSDFPaintToggle::Symmetry:
		return LOCTEXT("SymmetryDesc", "Uses the front half sweep and mirrors the result.");
	case EQuickSDFPaintToggle::MonotonicGuard:
		return LOCTEXT("MonotonicGuardDesc", "Clips brush strokes that would create repeated light/shadow flips in the active angle range.");
	default:
		return FText::GetEmpty();
	}
}

FName QuickSDFToolUI::GetToggleIconName(EQuickSDFPaintToggle Toggle)
{
	switch (Toggle)
	{
	case EQuickSDFPaintToggle::AutoSyncLight:
		return "QuickSDF.Toggle.AutoSyncLight";
	case EQuickSDFPaintToggle::ShowPreview:
		return "QuickSDF.Toggle.ShowPreview";
	case EQuickSDFPaintToggle::OverlayUV:
		return "QuickSDF.Toggle.OverlayUV";
	case EQuickSDFPaintToggle::OverlayOriginalShadow:
		return "QuickSDF.Toggle.OverlayOriginalShadow";
	case EQuickSDFPaintToggle::OnionSkin:
		return "QuickSDF.Toggle.OnionSkin";
	case EQuickSDFPaintToggle::QuickLine:
		return "QuickSDF.Toggle.QuickLine";
	case EQuickSDFPaintToggle::Symmetry:
		return "QuickSDF.Toggle.Symmetry";
	case EQuickSDFPaintToggle::MonotonicGuard:
		return "QuickSDF.Toggle.MonotonicGuard";
	default:
		return NAME_None;
	}
}

bool QuickSDFToolUI::GetToggleValue(const UQuickSDFToolProperties* Properties, EQuickSDFPaintToggle Toggle)
{
	if (!Properties)
	{
		return false;
	}

	switch (Toggle)
	{
	case EQuickSDFPaintToggle::AutoSyncLight:
		return Properties->bAutoSyncLight;
	case EQuickSDFPaintToggle::ShowPreview:
		return Properties->bShowPreview;
	case EQuickSDFPaintToggle::OverlayUV:
		return Properties->bOverlayUV;
	case EQuickSDFPaintToggle::OverlayOriginalShadow:
		return Properties->bOverlayOriginalShadow;
	case EQuickSDFPaintToggle::OnionSkin:
		return Properties->bEnableOnionSkin;
	case EQuickSDFPaintToggle::QuickLine:
		return Properties->bEnableQuickLine;
	case EQuickSDFPaintToggle::Symmetry:
		return Properties->bSymmetryMode;
	case EQuickSDFPaintToggle::MonotonicGuard:
		return Properties->bEnableMonotonicGuard;
	default:
		return false;
	}
}

void QuickSDFToolUI::SetToggleValue(UQuickSDFPaintTool* Tool, UQuickSDFToolProperties* Properties, EQuickSDFPaintToggle Toggle, bool bValue)
{
	if (!Properties || GetToggleValue(Properties, Toggle) == bValue)
	{
		return;
	}

	Properties->Modify();
	switch (Toggle)
	{
	case EQuickSDFPaintToggle::AutoSyncLight:
		Properties->bAutoSyncLight = bValue;
		break;
	case EQuickSDFPaintToggle::ShowPreview:
		Properties->bShowPreview = bValue;
		break;
	case EQuickSDFPaintToggle::OverlayUV:
		Properties->bOverlayUV = bValue;
		break;
	case EQuickSDFPaintToggle::OverlayOriginalShadow:
		Properties->bOverlayOriginalShadow = bValue;
		break;
	case EQuickSDFPaintToggle::OnionSkin:
		Properties->bEnableOnionSkin = bValue;
		break;
	case EQuickSDFPaintToggle::QuickLine:
		Properties->bEnableQuickLine = bValue;
		break;
	case EQuickSDFPaintToggle::Symmetry:
		Properties->bSymmetryMode = bValue;
		break;
	case EQuickSDFPaintToggle::MonotonicGuard:
		Properties->bEnableMonotonicGuard = bValue;
		break;
	default:
		break;
	}

	if (Tool)
	{
		FProperty* Prop = Properties->GetClass()->FindPropertyByName(GetTogglePropertyName(Toggle));
		Tool->OnPropertyModified(Properties, Prop);
	}

	if (GEditor)
	{
		GEditor->RedrawAllViewports(false);
	}
}

void QuickSDFToolUI::ToggleValue(UQuickSDFPaintTool* Tool, UQuickSDFToolProperties* Properties, EQuickSDFPaintToggle Toggle)
{
	SetToggleValue(Tool, Properties, Toggle, !GetToggleValue(Properties, Toggle));
}

TSharedRef<SWidget> QuickSDFToolUI::MakeIconLabelButton(const FName IconName, const FText& Label, const FText& ToolTip, FOnClicked OnClicked)
{
	return SNew(SButton)
		.HAlign(HAlign_Center)
		.ToolTipText(ToolTip)
		.OnClicked(OnClicked)
		.ContentPadding(FMargin(6.0f, 3.0f))
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(0.0f, 0.0f, 5.0f, 0.0f)
			[
				SNew(SImage)
				.Image(FQuickSDFToolStyle::GetBrush(IconName))
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Text(Label)
				.Font(FAppStyle::GetFontStyle("SmallFont"))
			]
		];
}

TSharedRef<SWidget> QuickSDFToolUI::MakeMaterialPreviewModeSelector(FGetPaintTool GetPaintTool, TWeakObjectPtr<UQuickSDFToolProperties> FallbackProperties)
{
	TSharedRef<SHorizontalBox> ModeRow = SNew(SHorizontalBox);
	for (EQuickSDFMaterialPreviewMode Mode : GetMaterialPreviewModes())
	{
		ModeRow->AddSlot()
		.AutoWidth()
		.Padding(0.5f, 0.0f)
		[
			SNew(SBox)
			.WidthOverride(26.0f)
			.HeightOverride(24.0f)
			[
				SNew(SCheckBox)
				.Style(FQuickSDFToolStyle::Get().Get(), "QuickSDF.Timeline.ToggleButton")
				.ToolTipText_Lambda([Mode, GetPaintTool, FallbackProperties]()
				{
					return GetMaterialPreviewModeToolTip(GetProperties(GetPaintTool(), FallbackProperties), Mode);
				})
				.IsChecked_Lambda([Mode, GetPaintTool, FallbackProperties]()
				{
					return GetMaterialPreviewMode(GetProperties(GetPaintTool(), FallbackProperties)) == Mode ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
				})
				.OnCheckStateChanged_Lambda([Mode, GetPaintTool, FallbackProperties](ECheckBoxState NewState)
				{
					if (NewState == ECheckBoxState::Checked)
					{
						UQuickSDFPaintTool* Tool = GetPaintTool();
						SetMaterialPreviewMode(Tool, GetProperties(Tool, FallbackProperties), Mode);
					}
				})
				.Padding(FMargin(3.0f, 2.0f))
				[
					SNew(SBox)
					.WidthOverride(16.0f)
					.HeightOverride(16.0f)
					.HAlign(HAlign_Center)
					.VAlign(VAlign_Center)
					[
						SNew(SImage)
						.Image(FQuickSDFToolStyle::GetBrush(GetMaterialPreviewModeIconName(Mode)))
						.ColorAndOpacity_Lambda([Mode, GetPaintTool, FallbackProperties]()
						{
							return GetMaterialPreviewMode(GetProperties(GetPaintTool(), FallbackProperties)) == Mode
								? FSlateColor(FLinearColor(0.35f, 0.82f, 1.0f, 1.0f))
								: FSlateColor(FLinearColor(0.62f, 0.62f, 0.62f, 1.0f));
						})
					]
				]
			]
		];
	}
	return ModeRow;
}

TSharedRef<SWidget> QuickSDFToolUI::MakePaintTargetModeSelector(FGetPaintTool GetPaintTool, TWeakObjectPtr<UQuickSDFToolProperties> FallbackProperties)
{
	TSharedRef<SHorizontalBox> ModeRow = SNew(SHorizontalBox);
	for (EQuickSDFPaintTargetMode Mode : GetPaintTargetModes())
	{
		ModeRow->AddSlot()
		.AutoWidth()
		.Padding(0.5f, 0.0f)
		[
			SNew(SBox)
			.WidthOverride(26.0f)
			.HeightOverride(24.0f)
			[
				SNew(SCheckBox)
				.Style(FQuickSDFToolStyle::Get().Get(), "QuickSDF.Timeline.ToggleButton")
				.ToolTipText_Lambda([Mode, GetPaintTool, FallbackProperties]()
				{
					return GetPaintTargetModeToolTip(GetProperties(GetPaintTool(), FallbackProperties), Mode);
				})
				.IsChecked_Lambda([Mode, GetPaintTool, FallbackProperties]()
				{
					return GetPaintTargetMode(GetProperties(GetPaintTool(), FallbackProperties)) == Mode ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
				})
				.OnCheckStateChanged_Lambda([Mode, GetPaintTool, FallbackProperties](ECheckBoxState NewState)
				{
					if (NewState == ECheckBoxState::Checked)
					{
						UQuickSDFPaintTool* Tool = GetPaintTool();
						SetPaintTargetMode(Tool, GetProperties(Tool, FallbackProperties), Mode);
					}
				})
				.Padding(FMargin(3.0f, 2.0f))
				[
					SNew(SBox)
					.WidthOverride(16.0f)
					.HeightOverride(16.0f)
					.HAlign(HAlign_Center)
					.VAlign(VAlign_Center)
					[
						SNew(SImage)
						.Image(FQuickSDFToolStyle::GetBrush(GetPaintTargetModeIconName(Mode)))
						.ColorAndOpacity_Lambda([Mode, GetPaintTool, FallbackProperties]()
						{
							return GetPaintTargetMode(GetProperties(GetPaintTool(), FallbackProperties)) == Mode
								? FSlateColor(FLinearColor(0.35f, 0.82f, 1.0f, 1.0f))
								: FSlateColor(FLinearColor(0.62f, 0.62f, 0.62f, 1.0f));
						})
					]
				]
			]
		];
	}
	return ModeRow;
}

TSharedRef<SWidget> QuickSDFToolUI::MakePaintToggleButton(EQuickSDFPaintToggle Toggle, FGetPaintTool GetPaintTool, TWeakObjectPtr<UQuickSDFToolProperties> FallbackProperties)
{
	return SNew(SBox)
		.WidthOverride(28.0f)
		.HeightOverride(24.0f)
		[
			SNew(SCheckBox)
			.Style(FQuickSDFToolStyle::Get().Get(), "QuickSDF.Timeline.ToggleButton")
			.ToolTipText_Lambda([Toggle, GetPaintTool, FallbackProperties]()
			{
				return GetToggleToolTip(GetProperties(GetPaintTool(), FallbackProperties), Toggle);
			})
			.IsChecked_Lambda([Toggle, GetPaintTool, FallbackProperties]()
			{
				return GetToggleValue(GetProperties(GetPaintTool(), FallbackProperties), Toggle) ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
			})
			.OnCheckStateChanged_Lambda([Toggle, GetPaintTool, FallbackProperties](ECheckBoxState NewState)
			{
				UQuickSDFPaintTool* Tool = GetPaintTool();
				SetToggleValue(Tool, GetProperties(Tool, FallbackProperties), Toggle, NewState == ECheckBoxState::Checked);
			})
			.Padding(FMargin(3.0f, 2.0f))
			[
				SNew(SBox)
				.WidthOverride(16.0f)
				.HeightOverride(16.0f)
				.HAlign(HAlign_Center)
				.VAlign(VAlign_Center)
				[
					SNew(SImage)
					.Image(FQuickSDFToolStyle::GetBrush(GetToggleIconName(Toggle)))
					.ColorAndOpacity_Lambda([Toggle, GetPaintTool, FallbackProperties]()
					{
						return GetToggleValue(GetProperties(GetPaintTool(), FallbackProperties), Toggle)
							? FSlateColor(FLinearColor(0.35f, 0.82f, 1.0f, 1.0f))
							: FSlateColor(FLinearColor(0.62f, 0.62f, 0.62f, 1.0f));
					})
				]
			]
		];
}

TSharedRef<SWidget> QuickSDFToolUI::MakePaintToggleBar(FGetPaintTool GetPaintTool, TWeakObjectPtr<UQuickSDFToolProperties> FallbackProperties)
{
	TSharedRef<SHorizontalBox> ToggleRow = SNew(SHorizontalBox);
	ToggleRow->AddSlot()
	.AutoWidth()
	.Padding(0.0f)
	[
		MakePaintTargetModeSelector(GetPaintTool, FallbackProperties)
	];

	ToggleRow->AddSlot()
	.AutoWidth()
	.Padding(3.0f, 0.0f)
	[
		SNew(SBox)
		.WidthOverride(1.0f)
		.HeightOverride(18.0f)
		[
			SNew(SBorder)
			.BorderImage(FAppStyle::GetBrush("WhiteBrush"))
			.BorderBackgroundColor(FLinearColor(1.0f, 1.0f, 1.0f, 0.10f))
		]
	];

	for (EQuickSDFPaintToggle Toggle : GetPaintToggles())
	{
		ToggleRow->AddSlot()
		.AutoWidth()
		.Padding(0.5f, 0.0f)
		[
			MakePaintToggleButton(Toggle, GetPaintTool, FallbackProperties)
		];
	}
	return ToggleRow;
}

TSharedRef<SWidget> QuickSDFToolUI::MakeQuickToggleMenu(FGetPaintTool GetPaintTool)
{
	TSharedRef<SUniformGridPanel> PreviewModeGrid = SNew(SUniformGridPanel)
		.SlotPadding(FMargin(3.0f));

	const TArray<EQuickSDFMaterialPreviewMode>& PreviewModes = GetMaterialPreviewModes();
	for (int32 Index = 0; Index < PreviewModes.Num(); ++Index)
	{
		const EQuickSDFMaterialPreviewMode Mode = PreviewModes[Index];
		PreviewModeGrid->AddSlot(Index, 0)
		[
			SNew(SBox)
			.WidthOverride(92.0f)
			.HeightOverride(58.0f)
			[
				SNew(SCheckBox)
				.Style(FAppStyle::Get(), "ToggleButtonCheckbox")
				.ToolTipText_Lambda([Mode, GetPaintTool]()
				{
					UQuickSDFPaintTool* Tool = GetPaintTool();
					return GetMaterialPreviewModeToolTip(Tool ? Tool->Properties : nullptr, Mode);
				})
				.IsChecked_Lambda([Mode, GetPaintTool]()
				{
					UQuickSDFPaintTool* Tool = GetPaintTool();
					return GetMaterialPreviewMode(Tool ? Tool->Properties : nullptr) == Mode ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
				})
				.OnCheckStateChanged_Lambda([Mode, GetPaintTool](ECheckBoxState NewState)
				{
					if (NewState == ECheckBoxState::Checked)
					{
						UQuickSDFPaintTool* Tool = GetPaintTool();
						SetMaterialPreviewMode(Tool, Tool ? Tool->Properties : nullptr, Mode);
					}
				})
				.Padding(FMargin(6.0f, 4.0f))
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot()
					.AutoHeight()
					.HAlign(HAlign_Center)
					[
						SNew(SImage)
						.Image(FQuickSDFToolStyle::GetBrush(GetMaterialPreviewModeIconName(Mode)))
						.ColorAndOpacity_Lambda([Mode, GetPaintTool]()
						{
							UQuickSDFPaintTool* Tool = GetPaintTool();
							return GetMaterialPreviewMode(Tool ? Tool->Properties : nullptr) == Mode
								? FSlateColor(FLinearColor(0.35f, 0.82f, 1.0f, 1.0f))
								: FSlateColor(FLinearColor(0.68f, 0.68f, 0.68f, 1.0f));
						})
					]
					+ SVerticalBox::Slot()
					.AutoHeight()
					.HAlign(HAlign_Center)
					.Padding(0.0f, 4.0f, 0.0f, 0.0f)
					[
						SNew(STextBlock)
						.Text(GetMaterialPreviewModeLabel(Mode))
						.Justification(ETextJustify::Center)
						.Font(FAppStyle::GetFontStyle("SmallFont"))
					]
				]
			]
		];
	}

	TSharedRef<SUniformGridPanel> PaintModeGrid = SNew(SUniformGridPanel)
		.SlotPadding(FMargin(3.0f));

	const TArray<EQuickSDFPaintTargetMode>& Modes = GetPaintTargetModes();
	for (int32 Index = 0; Index < Modes.Num(); ++Index)
	{
		const EQuickSDFPaintTargetMode Mode = Modes[Index];
		PaintModeGrid->AddSlot(Index, 0)
		[
			SNew(SBox)
			.WidthOverride(78.0f)
			.HeightOverride(58.0f)
			[
				SNew(SCheckBox)
				.Style(FAppStyle::Get(), "ToggleButtonCheckbox")
				.ToolTipText_Lambda([Mode, GetPaintTool]()
				{
					UQuickSDFPaintTool* Tool = GetPaintTool();
					return GetPaintTargetModeToolTip(Tool ? Tool->Properties : nullptr, Mode);
				})
				.IsChecked_Lambda([Mode, GetPaintTool]()
				{
					UQuickSDFPaintTool* Tool = GetPaintTool();
					return GetPaintTargetMode(Tool ? Tool->Properties : nullptr) == Mode ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
				})
				.OnCheckStateChanged_Lambda([Mode, GetPaintTool](ECheckBoxState NewState)
				{
					if (NewState == ECheckBoxState::Checked)
					{
						UQuickSDFPaintTool* Tool = GetPaintTool();
						SetPaintTargetMode(Tool, Tool ? Tool->Properties : nullptr, Mode);
					}
				})
				.Padding(FMargin(6.0f, 4.0f))
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot()
					.AutoHeight()
					.HAlign(HAlign_Center)
					[
						SNew(SImage)
						.Image(FQuickSDFToolStyle::GetBrush(GetPaintTargetModeIconName(Mode)))
						.ColorAndOpacity_Lambda([Mode, GetPaintTool]()
						{
							UQuickSDFPaintTool* Tool = GetPaintTool();
							return GetPaintTargetMode(Tool ? Tool->Properties : nullptr) == Mode
								? FSlateColor(FLinearColor(0.35f, 0.82f, 1.0f, 1.0f))
								: FSlateColor(FLinearColor(0.68f, 0.68f, 0.68f, 1.0f));
						})
					]
					+ SVerticalBox::Slot()
					.AutoHeight()
					.HAlign(HAlign_Center)
					.Padding(0.0f, 4.0f, 0.0f, 0.0f)
					[
						SNew(STextBlock)
						.Text(GetPaintTargetModeLabel(Mode))
						.Justification(ETextJustify::Center)
						.Font(FAppStyle::GetFontStyle("SmallFont"))
					]
				]
			]
		];
	}

	TSharedRef<SUniformGridPanel> Grid = SNew(SUniformGridPanel)
		.SlotPadding(FMargin(3.0f));

	const TArray<EQuickSDFPaintToggle>& Toggles = GetPaintToggles();
	for (int32 Index = 0; Index < Toggles.Num(); ++Index)
	{
		const EQuickSDFPaintToggle Toggle = Toggles[Index];
		Grid->AddSlot(Index % 4, Index / 4)
		[
			SNew(SBox)
			.WidthOverride(92.0f)
			.HeightOverride(58.0f)
			[
				SNew(SCheckBox)
				.Style(FAppStyle::Get(), "ToggleButtonCheckbox")
				.ToolTipText_Lambda([Toggle, GetPaintTool]()
				{
					UQuickSDFPaintTool* Tool = GetPaintTool();
					return GetToggleToolTip(Tool ? Tool->Properties : nullptr, Toggle);
				})
				.IsChecked_Lambda([Toggle, GetPaintTool]()
				{
					UQuickSDFPaintTool* Tool = GetPaintTool();
					return GetToggleValue(Tool ? Tool->Properties : nullptr, Toggle) ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
				})
				.OnCheckStateChanged_Lambda([Toggle, GetPaintTool](ECheckBoxState NewState)
				{
					UQuickSDFPaintTool* Tool = GetPaintTool();
					SetToggleValue(Tool, Tool ? Tool->Properties : nullptr, Toggle, NewState == ECheckBoxState::Checked);
				})
				.Padding(FMargin(6.0f, 4.0f))
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot()
					.AutoHeight()
					.HAlign(HAlign_Center)
					[
						SNew(SImage)
						.Image(FQuickSDFToolStyle::GetBrush(GetToggleIconName(Toggle)))
						.ColorAndOpacity_Lambda([Toggle, GetPaintTool]()
						{
							UQuickSDFPaintTool* Tool = GetPaintTool();
							return GetToggleValue(Tool ? Tool->Properties : nullptr, Toggle)
								? FSlateColor(FLinearColor(0.35f, 0.82f, 1.0f, 1.0f))
								: FSlateColor(FLinearColor(0.68f, 0.68f, 0.68f, 1.0f));
						})
					]
					+ SVerticalBox::Slot()
					.AutoHeight()
					.HAlign(HAlign_Center)
					.Padding(0.0f, 4.0f, 0.0f, 0.0f)
					[
						SNew(STextBlock)
						.Text(GetToggleLabel(Toggle))
						.Justification(ETextJustify::Center)
						.Font(FAppStyle::GetFontStyle("SmallFont"))
					]
				]
			]
		];
	}

	return SNew(SBorder)
		.BorderImage(FAppStyle::GetBrush("Menu.Background"))
		.Padding(8.0f)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				PreviewModeGrid
			]
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.0f, 6.0f, 0.0f, 0.0f)
			[
				PaintModeGrid
			]
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.0f, 6.0f, 0.0f, 0.0f)
			[
				Grid
			]
		];
}

void QuickSDFToolUI::ShowQuickToggleMenu(TSharedRef<SWidget> ParentWidget, const FVector2D& ScreenPosition, FGetPaintTool GetPaintTool)
{
	FSlateApplication::Get().PushMenu(
		ParentWidget,
		FWidgetPath(),
		MakeQuickToggleMenu(GetPaintTool),
		ScreenPosition,
		FPopupTransitionEffect(FPopupTransitionEffect::ContextMenu),
		true);
}

#undef LOCTEXT_NAMESPACE
