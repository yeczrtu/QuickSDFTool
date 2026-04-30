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
}

const TArray<EQuickSDFPaintToggle>& QuickSDFToolUI::GetPaintToggles()
{
	static const TArray<EQuickSDFPaintToggle> Toggles = {
		EQuickSDFPaintToggle::PaintAllAngles,
		EQuickSDFPaintToggle::AutoSyncLight,
		EQuickSDFPaintToggle::ShowPreview,
		EQuickSDFPaintToggle::OverlayUV,
		EQuickSDFPaintToggle::OverlayOriginalShadow,
		EQuickSDFPaintToggle::OnionSkin,
		EQuickSDFPaintToggle::QuickLine,
		EQuickSDFPaintToggle::Symmetry,
	};
	return Toggles;
}

UQuickSDFPaintTool* QuickSDFToolUI::GetActivePaintTool()
{
	UQuickSDFEditorMode* Mode = Cast<UQuickSDFEditorMode>(GLevelEditorModeTools().GetActiveScriptableMode("EM_QuickSDFEditorMode"));
	return Mode && Mode->GetToolManager() ? Cast<UQuickSDFPaintTool>(Mode->GetToolManager()->GetActiveTool(EToolSide::Left)) : nullptr;
}

FName QuickSDFToolUI::GetTogglePropertyName(EQuickSDFPaintToggle Toggle)
{
	switch (Toggle)
	{
	case EQuickSDFPaintToggle::PaintAllAngles:
		return GET_MEMBER_NAME_CHECKED(UQuickSDFToolProperties, bPaintAllAngles);
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
	default:
		return NAME_None;
	}
}

FText QuickSDFToolUI::GetToggleLabel(EQuickSDFPaintToggle Toggle)
{
	switch (Toggle)
	{
	case EQuickSDFPaintToggle::PaintAllAngles:
		return LOCTEXT("PaintAllAnglesLabel", "Paint All");
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
	default:
		return FText::GetEmpty();
	}
}

FText QuickSDFToolUI::GetToggleDescription(EQuickSDFPaintToggle Toggle)
{
	switch (Toggle)
	{
	case EQuickSDFPaintToggle::PaintAllAngles:
		return LOCTEXT("PaintAllAnglesDesc", "Paints the active stroke into every mask angle.");
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
	default:
		return FText::GetEmpty();
	}
}

FName QuickSDFToolUI::GetToggleIconName(EQuickSDFPaintToggle Toggle)
{
	switch (Toggle)
	{
	case EQuickSDFPaintToggle::PaintAllAngles:
		return "QuickSDF.Toggle.PaintAllAngles";
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
	case EQuickSDFPaintToggle::PaintAllAngles:
		return Properties->bPaintAllAngles;
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
	case EQuickSDFPaintToggle::PaintAllAngles:
		Properties->bPaintAllAngles = bValue;
		break;
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

TSharedRef<SWidget> QuickSDFToolUI::MakePaintToggleButton(EQuickSDFPaintToggle Toggle, FGetPaintTool GetPaintTool, TWeakObjectPtr<UQuickSDFToolProperties> FallbackProperties)
{
	return SNew(SCheckBox)
		.Style(FAppStyle::Get(), "ToggleButtonCheckbox")
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
		.Padding(FMargin(5.0f, 3.0f))
		[
			SNew(SImage)
			.Image(FQuickSDFToolStyle::GetBrush(GetToggleIconName(Toggle)))
			.ColorAndOpacity_Lambda([Toggle, GetPaintTool, FallbackProperties]()
			{
				return GetToggleValue(GetProperties(GetPaintTool(), FallbackProperties), Toggle)
					? FSlateColor(FLinearColor(0.35f, 0.82f, 1.0f, 1.0f))
					: FSlateColor(FLinearColor(0.62f, 0.62f, 0.62f, 1.0f));
			})
		];
}

TSharedRef<SWidget> QuickSDFToolUI::MakePaintToggleBar(FGetPaintTool GetPaintTool, TWeakObjectPtr<UQuickSDFToolProperties> FallbackProperties)
{
	TSharedRef<SHorizontalBox> ToggleRow = SNew(SHorizontalBox);
	for (EQuickSDFPaintToggle Toggle : GetPaintToggles())
	{
		ToggleRow->AddSlot()
		.AutoWidth()
		.Padding(1.0f, 0.0f)
		[
			MakePaintToggleButton(Toggle, GetPaintTool, FallbackProperties)
		];
	}
	return ToggleRow;
}

TSharedRef<SWidget> QuickSDFToolUI::MakeQuickToggleMenu(FGetPaintTool GetPaintTool)
{
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
			Grid
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
