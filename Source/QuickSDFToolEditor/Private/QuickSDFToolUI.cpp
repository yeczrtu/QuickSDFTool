#include "QuickSDFToolUI.h"

#include "Editor.h"
#include "EditorModeManager.h"
#include "Framework/Application/SlateApplication.h"
#include "InteractiveToolManager.h"
#include "ISinglePropertyView.h"
#include "Modules/ModuleManager.h"
#include "PropertyEditorModule.h"
#include "QuickSDFEditorMode.h"
#include "QuickSDFPaintTool.h"
#include "QuickSDFSelectTool.h"
#include "QuickSDFToolProperties.h"
#include "QuickSDFToolStyle.h"
#include "Styling/AppStyle.h"
#include "Textures/SlateIcon.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SGridPanel.h"
#include "Widgets/Layout/SUniformGridPanel.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SLeafWidget.h"
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
	if (Toggle == EQuickSDFPaintToggle::Symmetry && Properties)
	{
		if (Properties->AutoSymmetryStatus.IsEmpty())
		{
			return FText::Format(
				LOCTEXT("SymmetryToggleTooltipNoStatusFormat", "{0}: {1}\nMode: {2}\n{3}"),
				QuickSDFToolUI::GetToggleLabel(Toggle),
				bEnabled ? LOCTEXT("ToggleOn", "On") : LOCTEXT("ToggleOff", "Off"),
				QuickSDFToolUI::GetSymmetryModeLabel(Properties->SymmetryMode),
				QuickSDFToolUI::GetToggleDescription(Toggle));
		}
		return FText::Format(
			LOCTEXT("SymmetryToggleTooltipFormat", "{0}: {1}\nMode: {2}\n{3}\n{4}"),
			QuickSDFToolUI::GetToggleLabel(Toggle),
			bEnabled ? LOCTEXT("ToggleOn", "On") : LOCTEXT("ToggleOff", "Off"),
			QuickSDFToolUI::GetSymmetryModeLabel(Properties->SymmetryMode),
			Properties->AutoSymmetryStatus,
			QuickSDFToolUI::GetToggleDescription(Toggle));
	}

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

FText GetApplyModeToolTip(UQuickSDFToolProperties* Properties, EQuickSDFApplyMode Mode)
{
	const bool bSelected = QuickSDFToolUI::GetApplyMode(Properties) == Mode;
	return FText::Format(
		LOCTEXT("ApplyModeTooltipFormat", "Apply Mode: {0}\n{1}\n{2}"),
		QuickSDFToolUI::GetApplyModeLabel(Mode),
		bSelected ? LOCTEXT("ApplyModeSelected", "Selected") : LOCTEXT("ApplyModeClickToUse", "Click to use"),
		QuickSDFToolUI::GetApplyModeDescription(Mode));
}

FText GetApplyDirectionToolTip(UQuickSDFToolProperties* Properties, EQuickSDFApplyDirection Direction)
{
	const bool bSelected = QuickSDFToolUI::GetApplyDirection(Properties) == Direction;
	return FText::Format(
		LOCTEXT("ApplyDirectionTooltipFormat", "Direction: {0}\n{1}\n{2}"),
		QuickSDFToolUI::GetApplyDirectionLabel(Direction),
		bSelected ? LOCTEXT("ApplyDirectionSelected", "Selected") : LOCTEXT("ApplyDirectionClickToUse", "Click to use"),
		QuickSDFToolUI::GetApplyDirectionDescription(Direction));
}

class SQuickSDFGradientCurvePreview final : public SLeafWidget
{
public:
	SLATE_BEGIN_ARGS(SQuickSDFGradientCurvePreview) {}
		SLATE_ATTRIBUTE(UQuickSDFToolProperties*, Properties)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs)
	{
		Properties = InArgs._Properties;
	}

	virtual FVector2D ComputeDesiredSize(float) const override
	{
		return FVector2D(42.0f, 16.0f);
	}

	virtual int32 OnPaint(
		const FPaintArgs& Args,
		const FGeometry& AllottedGeometry,
		const FSlateRect& MyCullingRect,
		FSlateWindowElementList& OutDrawElements,
		int32 LayerId,
		const FWidgetStyle& InWidgetStyle,
		bool bParentEnabled) const override
	{
		const FVector2D LocalSize = AllottedGeometry.GetLocalSize();
		FSlateDrawElement::MakeBox(
			OutDrawElements,
			LayerId,
			AllottedGeometry.ToPaintGeometry(),
			FAppStyle::GetBrush("WhiteBrush"),
			ESlateDrawEffect::None,
			FLinearColor(0.02f, 0.02f, 0.02f, 0.70f));

		TArray<FVector2D> Points;
		constexpr int32 SampleCount = 14;
		Points.Reserve(SampleCount);
		const UQuickSDFToolProperties* Props = Properties.Get();
		for (int32 Index = 0; Index < SampleCount; ++Index)
		{
			const float Alpha = static_cast<float>(Index) / static_cast<float>(SampleCount - 1);
			const float Scale = Props
				? Props->EvaluateGradientRadiusScale(Alpha)
				: 1.0f - Alpha;
			Points.Add(FVector2D(
				Alpha * LocalSize.X,
				(1.0f - FMath::Clamp(Scale, 0.0f, 1.0f)) * LocalSize.Y));
		}

		FSlateDrawElement::MakeLines(
			OutDrawElements,
			LayerId + 1,
			AllottedGeometry.ToPaintGeometry(),
			Points,
			ESlateDrawEffect::None,
			FLinearColor(0.74f, 0.58f, 1.0f, 1.0f),
			true,
			1.5f);
		return LayerId + 1;
	}

private:
	TAttribute<UQuickSDFToolProperties*> Properties;
};

TSharedRef<SWidget> MakeGradientCurveQuickEditor(
	QuickSDFToolUI::FGetPaintTool GetPaintTool,
	TWeakObjectPtr<UQuickSDFToolProperties> FallbackProperties)
{
	return SNew(SBox)
		.Visibility_Lambda([GetPaintTool, FallbackProperties]()
		{
			return QuickSDFToolUI::GetApplyMode(GetProperties(GetPaintTool(), FallbackProperties)) == EQuickSDFApplyMode::GradientRange
				? EVisibility::Visible
				: EVisibility::Collapsed;
		})
		[
			SNew(SComboButton)
			.ButtonStyle(FQuickSDFToolStyle::Get().Get(), "QuickSDF.Timeline.Button")
			.ContentPadding(FMargin(4.0f, 2.0f))
			.ToolTipText(LOCTEXT("GradientCurveQuickEditTooltip", "Gradient Curve"))
			.OnGetMenuContent_Lambda([GetPaintTool, FallbackProperties]()
			{
				UQuickSDFPaintTool* Tool = GetPaintTool();
				UQuickSDFToolProperties* Properties = GetProperties(Tool, FallbackProperties);
				if (!Properties)
				{
					return SNew(SBox)
						.Padding(8.0f)
						[
							SNew(STextBlock)
							.Text(LOCTEXT("GradientCurveMissingProperties", "No active paint properties."))
						];
				}

				FPropertyEditorModule& PropertyEditorModule = FModuleManager::LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");
				FSinglePropertyParams Params;
				Params.NameOverride = LOCTEXT("GradientCurveSinglePropertyName", "Gradient Curve");
				Params.NamePlacement = EPropertyNamePlacement::Left;
				Params.Font = FAppStyle::GetFontStyle("PropertyWindow.NormalFont");
				TSharedPtr<ISinglePropertyView> PropertyView = PropertyEditorModule.CreateSingleProperty(
					Properties,
					GET_MEMBER_NAME_CHECKED(UQuickSDFToolProperties, GradientCurve),
					Params);

				if (!PropertyView.IsValid() || !PropertyView->HasValidProperty())
				{
					return SNew(SBox)
						.Padding(8.0f)
						[
							SNew(STextBlock)
							.Text(LOCTEXT("GradientCurveInvalidProperty", "Gradient Curve is unavailable."))
						];
				}

				TWeakObjectPtr<UQuickSDFToolProperties> WeakProperties(Properties);
				TWeakObjectPtr<UQuickSDFPaintTool> WeakTool(Tool);
				PropertyView->SetOnPropertyValueChanged(FSimpleDelegate::CreateLambda([WeakProperties, WeakTool]()
				{
					if (UQuickSDFToolProperties* Props = WeakProperties.Get())
					{
						Props->EnsureGradientCurveDefaults();
						if (UQuickSDFPaintTool* PaintTool = WeakTool.Get())
						{
							FProperty* Prop = Props->GetClass()->FindPropertyByName(GET_MEMBER_NAME_CHECKED(UQuickSDFToolProperties, GradientCurve));
							PaintTool->OnPropertyModified(Props, Prop);
						}
					}
					if (GEditor)
					{
						GEditor->RedrawAllViewports(false);
					}
				}));

				return SNew(SBox)
					.WidthOverride(380.0f)
					.Padding(8.0f)
					[
						PropertyView.ToSharedRef()
					];
			})
			.ButtonContent()
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				[
					SNew(SQuickSDFGradientCurvePreview)
					.Properties_Lambda([GetPaintTool, FallbackProperties]()
					{
						return GetProperties(GetPaintTool(), FallbackProperties);
					})
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				.Padding(5.0f, 0.0f, 0.0f, 0.0f)
				[
					SNew(STextBlock)
					.Text(LOCTEXT("GradientCurveQuickEditLabel", "Edit..."))
					.Font(FAppStyle::GetFontStyle("SmallFont"))
				]
			]
		];
}

FText GetMeshPaintModeToolTip(UQuickSDFToolProperties* Properties, EQuickSDFMeshPaintMode Mode)
{
	const bool bSelected = QuickSDFToolUI::GetMeshPaintMode(Properties) == Mode;
	return FText::Format(
		LOCTEXT("MeshPaintModeTooltipFormat", "{0}: {1}\n{2}"),
		QuickSDFToolUI::GetMeshPaintModeLabel(Mode),
		bSelected ? LOCTEXT("MeshPaintModeSelected", "Selected") : LOCTEXT("MeshPaintModeClickToUse", "Click to use"),
		QuickSDFToolUI::GetMeshPaintModeDescription(Mode));
}

bool IsMaterialPreviewModeEnabled(UQuickSDFPaintTool* Tool, EQuickSDFMaterialPreviewMode Mode)
{
	if (Mode == EQuickSDFMaterialPreviewMode::GeneratedSDF)
	{
		return Tool && Tool->CanUseGeneratedSDFPreview();
	}
	if (Mode == EQuickSDFMaterialPreviewMode::LiveSDF)
	{
		return Tool && Tool->CanUseLiveSDFPreview();
	}
	return true;
}

FText GetMaterialPreviewModeToolTip(UQuickSDFPaintTool* Tool, UQuickSDFToolProperties* Properties, EQuickSDFMaterialPreviewMode Mode)
{
	if (Mode == EQuickSDFMaterialPreviewMode::GeneratedSDF && Tool && !Tool->CanUseGeneratedSDFPreview())
	{
		return Tool->GetGeneratedSDFPreviewUnavailableText();
	}
	if (Mode == EQuickSDFMaterialPreviewMode::LiveSDF && (!Tool || !Tool->CanUseLiveSDFPreview()))
	{
		return LOCTEXT("LiveSDFPreviewUnavailable", "Live SDF preview is unavailable until a paint target is ready.");
	}

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
		EQuickSDFPaintToggle::OnionSkin,
		EQuickSDFPaintToggle::QuickLine,
		EQuickSDFPaintToggle::Symmetry,
		EQuickSDFPaintToggle::MonotonicGuard,
		EQuickSDFPaintToggle::IsolateSlot,
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
		EQuickSDFMaterialPreviewMode::LiveSDF,
		EQuickSDFMaterialPreviewMode::GeneratedSDF,
	};
	return Modes;
}

const TArray<EQuickSDFMeshPaintMode>& QuickSDFToolUI::GetMeshPaintModes()
{
	static const TArray<EQuickSDFMeshPaintMode> Modes = {
		EQuickSDFMeshPaintMode::UVSpaceLegacy,
		EQuickSDFMeshPaintMode::ProjectedSurface,
		EQuickSDFMeshPaintMode::ScreenProjection,
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

const TArray<EQuickSDFApplyMode>& QuickSDFToolUI::GetApplyModes()
{
	static const TArray<EQuickSDFApplyMode> Modes = {
		EQuickSDFApplyMode::Single,
		EQuickSDFApplyMode::SolidRange,
		EQuickSDFApplyMode::GradientRange,
	};
	return Modes;
}

const TArray<EQuickSDFApplyDirection>& QuickSDFToolUI::GetApplyDirections()
{
	static const TArray<EQuickSDFApplyDirection> Directions = {
		EQuickSDFApplyDirection::Both,
		EQuickSDFApplyDirection::Before,
		EQuickSDFApplyDirection::After,
	};
	return Directions;
}

const TArray<EQuickSDFSymmetryMode>& QuickSDFToolUI::GetSymmetryModes()
{
	static const TArray<EQuickSDFSymmetryMode> Modes = {
		EQuickSDFSymmetryMode::Auto,
		EQuickSDFSymmetryMode::WholeTextureFlip90,
		EQuickSDFSymmetryMode::UVIslandChannelFlip90,
		EQuickSDFSymmetryMode::OverlappedUVSplit90,
		EQuickSDFSymmetryMode::None180,
	};
	return Modes;
}

UQuickSDFPaintTool* QuickSDFToolUI::GetActivePaintTool()
{
	UQuickSDFEditorMode* Mode = Cast<UQuickSDFEditorMode>(GLevelEditorModeTools().GetActiveScriptableMode("EM_QuickSDFEditorMode"));
	return Mode && Mode->GetToolManager() ? Cast<UQuickSDFPaintTool>(Mode->GetToolManager()->GetActiveTool(EToolSide::Left)) : nullptr;
}

UQuickSDFSelectTool* QuickSDFToolUI::GetActiveSelectTool()
{
	UQuickSDFEditorMode* Mode = Cast<UQuickSDFEditorMode>(GLevelEditorModeTools().GetActiveScriptableMode(UQuickSDFEditorMode::EM_QuickSDFEditorModeId));
	return Mode && Mode->GetToolManager() ? Cast<UQuickSDFSelectTool>(Mode->GetToolManager()->GetActiveTool(EToolSide::Left)) : nullptr;
}

bool QuickSDFToolUI::StartPaintTool()
{
	UQuickSDFEditorMode* Mode = Cast<UQuickSDFEditorMode>(GLevelEditorModeTools().GetActiveScriptableMode(UQuickSDFEditorMode::EM_QuickSDFEditorModeId));
	if (!Mode)
	{
		return false;
	}

	if (UQuickSDFSelectTool* SelectTool = QuickSDFToolUI::GetActiveSelectTool())
	{
		SelectTool->RestoreActiveMaterialSlotHighlight();
	}
	Mode->StartQuickSDFPaintTool();
	return QuickSDFToolUI::GetActivePaintTool() != nullptr;
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
	case EQuickSDFMaterialPreviewMode::LiveSDF:
		return LOCTEXT("MaterialPreviewLiveSDFLabel", "Live SDF");
	case EQuickSDFMaterialPreviewMode::GeneratedSDF:
		return LOCTEXT("MaterialPreviewGeneratedSDFLabel", "SDF Result");
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
	case EQuickSDFMaterialPreviewMode::LiveSDF:
		return LOCTEXT("MaterialPreviewLiveSDFDesc", "Shows a low-resolution GPU approximation of the SDF for fast shape checks.");
	case EQuickSDFMaterialPreviewMode::GeneratedSDF:
		return LOCTEXT("MaterialPreviewGeneratedSDFDesc", "Shows the generated SDF threshold map through M_SDFToon for the active texture set.");
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
	case EQuickSDFMaterialPreviewMode::LiveSDF:
		return "QuickSDF.MaterialPreview.LiveSDF";
	case EQuickSDFMaterialPreviewMode::GeneratedSDF:
		return "QuickSDF.Action.CreateThresholdMap";
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

	if (Mode == EQuickSDFMaterialPreviewMode::GeneratedSDF && (!Tool || !Tool->CanUseGeneratedSDFPreview()))
	{
		return;
	}
	if (Mode == EQuickSDFMaterialPreviewMode::LiveSDF && (!Tool || !Tool->CanUseLiveSDFPreview()))
	{
		return;
	}

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
	for (int32 Step = 1; Step <= Modes.Num(); ++Step)
	{
		const int32 NextIndex = CurrentIndex == INDEX_NONE ? 0 : (CurrentIndex + Step) % Modes.Num();
		if (IsMaterialPreviewModeEnabled(Tool, Modes[NextIndex]))
		{
			SetMaterialPreviewMode(Tool, Properties, Modes[NextIndex]);
			return;
		}
	}
}

EQuickSDFMeshPaintMode QuickSDFToolUI::GetMeshPaintMode(const UQuickSDFToolProperties* Properties)
{
	if (!Properties)
	{
		return EQuickSDFMeshPaintMode::ScreenProjection;
	}

	if (Properties->bUseSurfaceSpacePaint &&
		Properties->MeshPaintMode == EQuickSDFMeshPaintMode::UVSpaceLegacy)
	{
		return EQuickSDFMeshPaintMode::ProjectedSurface;
	}

	return Properties->MeshPaintMode;
}

FText QuickSDFToolUI::GetMeshPaintModeLabel(EQuickSDFMeshPaintMode Mode)
{
	switch (Mode)
	{
	case EQuickSDFMeshPaintMode::UVSpaceLegacy:
		return LOCTEXT("MeshPaintUVSpaceLabel", "UV Space");
	case EQuickSDFMeshPaintMode::ProjectedSurface:
		return LOCTEXT("MeshPaintSurfaceProjectionLabel", "Surface Projection");
	case EQuickSDFMeshPaintMode::ScreenProjection:
		return LOCTEXT("MeshPaintScreenProjectionLabel", "Screen Projection");
	default:
		return FText::GetEmpty();
	}
}

FText QuickSDFToolUI::GetMeshPaintModeShortLabel(EQuickSDFMeshPaintMode Mode)
{
	switch (Mode)
	{
	case EQuickSDFMeshPaintMode::UVSpaceLegacy:
		return LOCTEXT("MeshPaintUVSpaceShortLabel", "UV");
	case EQuickSDFMeshPaintMode::ProjectedSurface:
		return LOCTEXT("MeshPaintSurfaceProjectionShortLabel", "Surface");
	case EQuickSDFMeshPaintMode::ScreenProjection:
		return LOCTEXT("MeshPaintScreenProjectionShortLabel", "Screen");
	default:
		return FText::GetEmpty();
	}
}

FText QuickSDFToolUI::GetMeshPaintModeDescription(EQuickSDFMeshPaintMode Mode)
{
	switch (Mode)
	{
	case EQuickSDFMeshPaintMode::UVSpaceLegacy:
		return LOCTEXT("MeshPaintUVSpaceDesc", "Paints directly through the active UV channel into the mask texture.");
	case EQuickSDFMeshPaintMode::ProjectedSurface:
		return LOCTEXT("MeshPaintSurfaceProjectionDesc", "Projects brush strokes across the mesh surface and writes them back into UV texture space.");
	case EQuickSDFMeshPaintMode::ScreenProjection:
		return LOCTEXT("MeshPaintScreenProjectionDesc", "Uses a screen-space brush footprint projected onto the visible mesh surface.");
	default:
		return FText::GetEmpty();
	}
}

FName QuickSDFToolUI::GetMeshPaintModeIconName(EQuickSDFMeshPaintMode Mode)
{
	switch (Mode)
	{
	case EQuickSDFMeshPaintMode::UVSpaceLegacy:
		return "QuickSDF.Toggle.OverlayUV";
	case EQuickSDFMeshPaintMode::ProjectedSurface:
		return "QuickSDF.PaintTextureColor";
	case EQuickSDFMeshPaintMode::ScreenProjection:
		return "QuickSDF.Toggle.ShowPreview";
	default:
		return NAME_None;
	}
}

void QuickSDFToolUI::SetMeshPaintMode(UQuickSDFPaintTool* Tool, UQuickSDFToolProperties* Properties, EQuickSDFMeshPaintMode Mode)
{
	if (!Properties)
	{
		return;
	}

	const bool bExpectedSurfacePaint = Mode == EQuickSDFMeshPaintMode::ProjectedSurface;
	if (Properties->MeshPaintMode == Mode && Properties->bUseSurfaceSpacePaint == bExpectedSurfacePaint)
	{
		return;
	}

	Properties->MeshPaintMode = Mode;
	Properties->bUseSurfaceSpacePaint = bExpectedSurfacePaint;

	if (Tool)
	{
		FProperty* Prop = Properties->GetClass()->FindPropertyByName(GET_MEMBER_NAME_CHECKED(UQuickSDFToolProperties, MeshPaintMode));
		Tool->OnPropertyModified(Properties, Prop);
	}

	if (GEditor)
	{
		GEditor->RedrawAllViewports(false);
	}
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

	Properties->PaintTargetMode = Mode;
	Properties->bPaintAllAngles = bExpectedPaintAll;
	Properties->SyncApplyModeFromLegacyPaintTarget();

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

EQuickSDFApplyMode QuickSDFToolUI::GetApplyMode(const UQuickSDFToolProperties* Properties)
{
	return Properties ? Properties->ApplyMode : EQuickSDFApplyMode::Single;
}

FText QuickSDFToolUI::GetApplyModeLabel(EQuickSDFApplyMode Mode)
{
	switch (Mode)
	{
	case EQuickSDFApplyMode::Single:
		return LOCTEXT("ApplyModeSingleLabel", "Single");
	case EQuickSDFApplyMode::SolidRange:
		return LOCTEXT("ApplyModeSolidRangeLabel", "Solid Range");
	case EQuickSDFApplyMode::GradientRange:
		return LOCTEXT("ApplyModeGradientRangeLabel", "Gradient Range");
	default:
		return FText::GetEmpty();
	}
}

FText QuickSDFToolUI::GetApplyModeShortLabel(EQuickSDFApplyMode Mode)
{
	switch (Mode)
	{
	case EQuickSDFApplyMode::Single:
		return LOCTEXT("ApplyModeSingleShortLabel", "Single");
	case EQuickSDFApplyMode::SolidRange:
		return LOCTEXT("ApplyModeSolidRangeShortLabel", "Solid");
	case EQuickSDFApplyMode::GradientRange:
		return LOCTEXT("ApplyModeGradientRangeShortLabel", "Gradient");
	default:
		return FText::GetEmpty();
	}
}

FText QuickSDFToolUI::GetApplyModeDescription(EQuickSDFApplyMode Mode)
{
	switch (Mode)
	{
	case EQuickSDFApplyMode::Single:
		return LOCTEXT("ApplyModeSingleDesc", "Paints only the current angle.");
	case EQuickSDFApplyMode::SolidRange:
		return LOCTEXT("ApplyModeSolidRangeDesc", "Paints the selected angle range with the same brush radius.");
	case EQuickSDFApplyMode::GradientRange:
		return LOCTEXT("ApplyModeGradientRangeDesc", "Paints the selected angle range with radius scaled by distance from the current angle.");
	default:
		return FText::GetEmpty();
	}
}

void QuickSDFToolUI::SetApplyMode(UQuickSDFPaintTool* Tool, UQuickSDFToolProperties* Properties, EQuickSDFApplyMode Mode)
{
	if (!Properties || Properties->ApplyMode == Mode)
	{
		return;
	}

	Properties->ApplyMode = Mode;
	Properties->EnsureGradientCurveDefaults();
	Properties->SyncLegacyPaintTargetFromApplyMode();

	if (Tool)
	{
		FProperty* Prop = Properties->GetClass()->FindPropertyByName(GET_MEMBER_NAME_CHECKED(UQuickSDFToolProperties, ApplyMode));
		Tool->OnPropertyModified(Properties, Prop);
	}

	if (GEditor)
	{
		GEditor->RedrawAllViewports(false);
	}
}

void QuickSDFToolUI::CycleApplyMode(UQuickSDFPaintTool* Tool, UQuickSDFToolProperties* Properties)
{
	if (!Properties)
	{
		return;
	}

	const TArray<EQuickSDFApplyMode>& Modes = GetApplyModes();
	const int32 CurrentIndex = Modes.IndexOfByKey(GetApplyMode(Properties));
	const int32 NextIndex = CurrentIndex == INDEX_NONE ? 0 : (CurrentIndex + 1) % Modes.Num();
	SetApplyMode(Tool, Properties, Modes[NextIndex]);
}

EQuickSDFApplyDirection QuickSDFToolUI::GetApplyDirection(const UQuickSDFToolProperties* Properties)
{
	return Properties ? Properties->ApplyDirection : EQuickSDFApplyDirection::Both;
}

FText QuickSDFToolUI::GetApplyDirectionLabel(EQuickSDFApplyDirection Direction)
{
	switch (Direction)
	{
	case EQuickSDFApplyDirection::Both:
		return LOCTEXT("ApplyDirectionBothLabel", "Both");
	case EQuickSDFApplyDirection::Before:
		return LOCTEXT("ApplyDirectionBeforeLabel", "Before");
	case EQuickSDFApplyDirection::After:
		return LOCTEXT("ApplyDirectionAfterLabel", "After");
	default:
		return FText::GetEmpty();
	}
}

FText QuickSDFToolUI::GetApplyDirectionDescription(EQuickSDFApplyDirection Direction)
{
	switch (Direction)
	{
	case EQuickSDFApplyDirection::Both:
		return LOCTEXT("ApplyDirectionBothDesc", "Targets both smaller and larger angles around the current angle.");
	case EQuickSDFApplyDirection::Before:
		return LOCTEXT("ApplyDirectionBeforeDesc", "Targets the current angle and smaller angles.");
	case EQuickSDFApplyDirection::After:
		return LOCTEXT("ApplyDirectionAfterDesc", "Targets the current angle and larger angles.");
	default:
		return FText::GetEmpty();
	}
}

void QuickSDFToolUI::SetApplyDirection(UQuickSDFPaintTool* Tool, UQuickSDFToolProperties* Properties, EQuickSDFApplyDirection Direction)
{
	if (!Properties || Properties->ApplyDirection == Direction)
	{
		return;
	}

	Properties->ApplyDirection = Direction;
	Properties->SyncLegacyPaintTargetFromApplyMode();

	if (Tool)
	{
		FProperty* Prop = Properties->GetClass()->FindPropertyByName(GET_MEMBER_NAME_CHECKED(UQuickSDFToolProperties, ApplyDirection));
		Tool->OnPropertyModified(Properties, Prop);
	}

	if (GEditor)
	{
		GEditor->RedrawAllViewports(false);
	}
}

void QuickSDFToolUI::CycleApplyDirection(UQuickSDFPaintTool* Tool, UQuickSDFToolProperties* Properties)
{
	if (!Properties)
	{
		return;
	}

	const TArray<EQuickSDFApplyDirection>& Directions = GetApplyDirections();
	const int32 CurrentIndex = Directions.IndexOfByKey(GetApplyDirection(Properties));
	const int32 NextIndex = CurrentIndex == INDEX_NONE ? 0 : (CurrentIndex + 1) % Directions.Num();
	SetApplyDirection(Tool, Properties, Directions[NextIndex]);
}

FText QuickSDFToolUI::GetSymmetryModeLabel(EQuickSDFSymmetryMode Mode)
{
	switch (Mode)
	{
	case EQuickSDFSymmetryMode::Auto:
		return LOCTEXT("SymmetryAutoLabel", "Auto");
	case EQuickSDFSymmetryMode::WholeTextureFlip90:
		return LOCTEXT("SymmetryTextureLabel", "Texture");
	case EQuickSDFSymmetryMode::UVIslandChannelFlip90:
		return LOCTEXT("SymmetryIslandLabel", "Island");
	case EQuickSDFSymmetryMode::OverlappedUVSplit90:
		return LOCTEXT("SymmetryOverlappedUVLabel", "Overlap");
	case EQuickSDFSymmetryMode::None180:
	default:
		return LOCTEXT("SymmetryOffLabel", "Off");
	}
}

FText QuickSDFToolUI::GetSymmetryModeDescription(EQuickSDFSymmetryMode Mode)
{
	switch (Mode)
	{
	case EQuickSDFSymmetryMode::Auto:
		return LOCTEXT("SymmetryAutoDesc", "Automatically chooses Texture, Island, or Overlap symmetry from the active UV layout.");
	case EQuickSDFSymmetryMode::WholeTextureFlip90:
		return LOCTEXT("SymmetryTextureDesc", "Paints 0-90 degrees and mirrors the texture for 90-180 degrees.");
	case EQuickSDFSymmetryMode::UVIslandChannelFlip90:
		return LOCTEXT("SymmetryIslandDesc", "Paints 0-90 degrees and mirrors 90-180 degrees per UV island.");
	case EQuickSDFSymmetryMode::OverlappedUVSplit90:
		return LOCTEXT("SymmetryOverlappedUVDesc", "Paints 0-90 degrees and stores right/left values in separate RGBA channels for overlapped mirrored UVs.");
	case EQuickSDFSymmetryMode::None180:
	default:
		return LOCTEXT("SymmetryOffDesc", "Paints the full 0-180 degree range.");
	}
}

void QuickSDFToolUI::SetSymmetryMode(UQuickSDFPaintTool* Tool, UQuickSDFToolProperties* Properties, EQuickSDFSymmetryMode Mode)
{
	if (!Properties || Properties->SymmetryMode == Mode)
	{
		return;
	}

	Properties->SetSymmetryMode(Mode);
	if (Tool)
	{
		FProperty* Prop = Properties->GetClass()->FindPropertyByName(GET_MEMBER_NAME_CHECKED(UQuickSDFToolProperties, SymmetryMode));
		Tool->OnPropertyModified(Properties, Prop);
	}

	if (GEditor)
	{
		GEditor->RedrawAllViewports(false);
	}
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
	case EQuickSDFPaintToggle::IsolateSlot:
		return GET_MEMBER_NAME_CHECKED(UQuickSDFToolProperties, bIsolateTargetMaterialSlot);
	case EQuickSDFPaintToggle::OnionSkin:
		return GET_MEMBER_NAME_CHECKED(UQuickSDFToolProperties, bEnableOnionSkin);
	case EQuickSDFPaintToggle::QuickLine:
		return GET_MEMBER_NAME_CHECKED(UQuickSDFToolProperties, bEnableQuickLine);
	case EQuickSDFPaintToggle::Symmetry:
		return GET_MEMBER_NAME_CHECKED(UQuickSDFToolProperties, SymmetryMode);
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
	case EQuickSDFPaintToggle::IsolateSlot:
		return LOCTEXT("IsolateSlotLabel", "Isolate Slot");
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
	case EQuickSDFPaintToggle::IsolateSlot:
		return LOCTEXT("IsolateSlotDesc", "Shows only the active material slot in the viewport. Turn off to view the full mesh while keeping the same paint target.");
	case EQuickSDFPaintToggle::OnionSkin:
		return LOCTEXT("OnionSkinDesc", "Shows neighboring mask context while editing.");
	case EQuickSDFPaintToggle::QuickLine:
		return LOCTEXT("QuickLineDesc", "Enables hold-to-line quick stroke drawing.");
	case EQuickSDFPaintToggle::Symmetry:
		return LOCTEXT("SymmetryDesc", "Uses a 0-90 degree front-half sweep. Auto chooses Texture, Island, or Overlap symmetry from the active UV layout.");
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
	case EQuickSDFPaintToggle::IsolateSlot:
		return "QuickSDF.MaterialPreview.OriginalPaint";
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
	case EQuickSDFPaintToggle::IsolateSlot:
		return Properties->bIsolateTargetMaterialSlot;
	case EQuickSDFPaintToggle::OnionSkin:
		return Properties->bEnableOnionSkin;
	case EQuickSDFPaintToggle::QuickLine:
		return Properties->bEnableQuickLine;
	case EQuickSDFPaintToggle::Symmetry:
		return Properties->UsesFrontHalfAngles();
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
	case EQuickSDFPaintToggle::IsolateSlot:
		Properties->bIsolateTargetMaterialSlot = bValue;
		break;
	case EQuickSDFPaintToggle::OnionSkin:
		Properties->bEnableOnionSkin = bValue;
		break;
	case EQuickSDFPaintToggle::QuickLine:
		Properties->bEnableQuickLine = bValue;
		break;
	case EQuickSDFPaintToggle::Symmetry:
		Properties->SetSymmetryEnabled(bValue);
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
					UQuickSDFPaintTool* Tool = GetPaintTool();
					return GetMaterialPreviewModeToolTip(Tool, GetProperties(Tool, FallbackProperties), Mode);
				})
				.IsEnabled_Lambda([Mode, GetPaintTool]()
				{
					return IsMaterialPreviewModeEnabled(GetPaintTool(), Mode);
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

TSharedRef<SWidget> QuickSDFToolUI::MakeAutoSDFPreviewToggle(FGetPaintTool GetPaintTool, TWeakObjectPtr<UQuickSDFToolProperties> FallbackProperties)
{
	return SNew(SCheckBox)
		.ToolTipText(LOCTEXT("AutoSDFPreviewTooltip", "Automatically switch Material Preview to Generated SDF after Generate Selected SDF succeeds."))
		.IsChecked_Lambda([GetPaintTool, FallbackProperties]()
		{
			const UQuickSDFToolProperties* Properties = GetProperties(GetPaintTool(), FallbackProperties);
			return Properties && Properties->bAutoPreviewGeneratedSDF ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
		})
		.OnCheckStateChanged_Lambda([GetPaintTool, FallbackProperties](ECheckBoxState NewState)
		{
			UQuickSDFPaintTool* Tool = GetPaintTool();
			UQuickSDFToolProperties* Properties = GetProperties(Tool, FallbackProperties);
			if (!Properties)
			{
				return;
			}

			Properties->bAutoPreviewGeneratedSDF = NewState == ECheckBoxState::Checked;
			if (Tool)
			{
				FProperty* Prop = Properties->GetClass()->FindPropertyByName(GET_MEMBER_NAME_CHECKED(UQuickSDFToolProperties, bAutoPreviewGeneratedSDF));
				Tool->OnPropertyModified(Properties, Prop);
			}
		})
		[
			SNew(STextBlock)
			.Text(LOCTEXT("AutoSDFPreviewLabel", "Auto SDF Preview"))
			.Font(FAppStyle::GetFontStyle("SmallFont"))
		];
}

TSharedRef<SWidget> QuickSDFToolUI::MakeMeshPaintModeSelector(FGetPaintTool GetPaintTool, TWeakObjectPtr<UQuickSDFToolProperties> FallbackProperties, bool bUseCompactLayout)
{
	if (bUseCompactLayout)
	{
		TSharedRef<SHorizontalBox> ModeRow = SNew(SHorizontalBox);
		for (EQuickSDFMeshPaintMode Mode : GetMeshPaintModes())
		{
			ModeRow->AddSlot()
			.AutoWidth()
			.Padding(0.5f, 0.0f)
			[
				SNew(SBox)
				.WidthOverride(58.0f)
				.HeightOverride(24.0f)
				[
					SNew(SCheckBox)
					.Style(FQuickSDFToolStyle::Get().Get(), "QuickSDF.Timeline.ToggleButton")
					.ToolTipText_Lambda([Mode, GetPaintTool, FallbackProperties]()
					{
						return GetMeshPaintModeToolTip(GetProperties(GetPaintTool(), FallbackProperties), Mode);
					})
					.IsChecked_Lambda([Mode, GetPaintTool, FallbackProperties]()
					{
						return GetMeshPaintMode(GetProperties(GetPaintTool(), FallbackProperties)) == Mode
							? ECheckBoxState::Checked
							: ECheckBoxState::Unchecked;
					})
					.OnCheckStateChanged_Lambda([Mode, GetPaintTool, FallbackProperties](ECheckBoxState NewState)
					{
						if (NewState == ECheckBoxState::Checked)
						{
							UQuickSDFPaintTool* Tool = GetPaintTool();
							SetMeshPaintMode(Tool, GetProperties(Tool, FallbackProperties), Mode);
						}
					})
					.Padding(FMargin(5.0f, 2.0f))
					[
						SNew(SBox)
						.HAlign(HAlign_Center)
						.VAlign(VAlign_Center)
						[
							SNew(STextBlock)
							.Text(GetMeshPaintModeShortLabel(Mode))
							.Justification(ETextJustify::Center)
							.Font(FAppStyle::GetFontStyle("SmallFont"))
							.ColorAndOpacity_Lambda([Mode, GetPaintTool, FallbackProperties]()
							{
								return GetMeshPaintMode(GetProperties(GetPaintTool(), FallbackProperties)) == Mode
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

	TSharedRef<SUniformGridPanel> ModeGrid = SNew(SUniformGridPanel)
		.SlotPadding(FMargin(3.0f));

	const TArray<EQuickSDFMeshPaintMode>& Modes = GetMeshPaintModes();
	for (int32 Index = 0; Index < Modes.Num(); ++Index)
	{
		const EQuickSDFMeshPaintMode Mode = Modes[Index];
		ModeGrid->AddSlot(Index, 0)
		[
			SNew(SBox)
			.WidthOverride(118.0f)
			.HeightOverride(58.0f)
			[
				SNew(SCheckBox)
				.Style(FAppStyle::Get(), "ToggleButtonCheckbox")
				.ToolTipText_Lambda([Mode, GetPaintTool, FallbackProperties]()
				{
					return GetMeshPaintModeToolTip(GetProperties(GetPaintTool(), FallbackProperties), Mode);
				})
				.IsChecked_Lambda([Mode, GetPaintTool, FallbackProperties]()
				{
					return GetMeshPaintMode(GetProperties(GetPaintTool(), FallbackProperties)) == Mode
						? ECheckBoxState::Checked
						: ECheckBoxState::Unchecked;
				})
				.OnCheckStateChanged_Lambda([Mode, GetPaintTool, FallbackProperties](ECheckBoxState NewState)
				{
					if (NewState == ECheckBoxState::Checked)
					{
						UQuickSDFPaintTool* Tool = GetPaintTool();
						SetMeshPaintMode(Tool, GetProperties(Tool, FallbackProperties), Mode);
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
						.Image(FQuickSDFToolStyle::GetBrush(GetMeshPaintModeIconName(Mode)))
						.ColorAndOpacity_Lambda([Mode, GetPaintTool, FallbackProperties]()
						{
							return GetMeshPaintMode(GetProperties(GetPaintTool(), FallbackProperties)) == Mode
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
						.Text(GetMeshPaintModeLabel(Mode))
						.Justification(ETextJustify::Center)
						.Font(FAppStyle::GetFontStyle("SmallFont"))
						.ColorAndOpacity_Lambda([Mode, GetPaintTool, FallbackProperties]()
						{
							return GetMeshPaintMode(GetProperties(GetPaintTool(), FallbackProperties)) == Mode
								? FSlateColor(FLinearColor(0.35f, 0.82f, 1.0f, 1.0f))
								: FSlateColor(FLinearColor(0.68f, 0.68f, 0.68f, 1.0f));
						})
					]
				]
			]
		];
	}

	return ModeGrid;
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

TSharedRef<SWidget> QuickSDFToolUI::MakeApplyModeSelector(FGetPaintTool GetPaintTool, TWeakObjectPtr<UQuickSDFToolProperties> FallbackProperties, bool bUseCompactLayout)
{
	if (bUseCompactLayout)
	{
		TSharedRef<SHorizontalBox> ModeRow = SNew(SHorizontalBox);
		for (EQuickSDFApplyMode Mode : GetApplyModes())
		{
			ModeRow->AddSlot()
			.AutoWidth()
			.Padding(0.5f, 0.0f)
			[
				SNew(SBox)
				.WidthOverride(72.0f)
				.HeightOverride(24.0f)
				[
					SNew(SCheckBox)
					.Style(FQuickSDFToolStyle::Get().Get(), "QuickSDF.Timeline.ToggleButton")
					.ToolTipText_Lambda([Mode, GetPaintTool, FallbackProperties]()
					{
						return GetApplyModeToolTip(GetProperties(GetPaintTool(), FallbackProperties), Mode);
					})
					.IsChecked_Lambda([Mode, GetPaintTool, FallbackProperties]()
					{
						return GetApplyMode(GetProperties(GetPaintTool(), FallbackProperties)) == Mode
							? ECheckBoxState::Checked
							: ECheckBoxState::Unchecked;
					})
					.OnCheckStateChanged_Lambda([Mode, GetPaintTool, FallbackProperties](ECheckBoxState NewState)
					{
						if (NewState == ECheckBoxState::Checked)
						{
							UQuickSDFPaintTool* Tool = GetPaintTool();
							SetApplyMode(Tool, GetProperties(Tool, FallbackProperties), Mode);
						}
					})
					.Padding(FMargin(5.0f, 2.0f))
					[
						SNew(STextBlock)
						.Text(GetApplyModeShortLabel(Mode))
						.Justification(ETextJustify::Center)
						.Font(FAppStyle::GetFontStyle("SmallFont"))
						.ColorAndOpacity_Lambda([Mode, GetPaintTool, FallbackProperties]()
						{
							return GetApplyMode(GetProperties(GetPaintTool(), FallbackProperties)) == Mode
								? FSlateColor(FLinearColor(0.35f, 0.82f, 1.0f, 1.0f))
								: FSlateColor(FLinearColor(0.62f, 0.62f, 0.62f, 1.0f));
						})
					]
				]
			];
		}
		return ModeRow;
	}

	TSharedRef<SUniformGridPanel> ModeGrid = SNew(SUniformGridPanel)
		.SlotPadding(FMargin(3.0f));

	const TArray<EQuickSDFApplyMode>& Modes = GetApplyModes();
	for (int32 Index = 0; Index < Modes.Num(); ++Index)
	{
		const EQuickSDFApplyMode Mode = Modes[Index];
		ModeGrid->AddSlot(Index, 0)
		[
			SNew(SBox)
			.WidthOverride(112.0f)
			.HeightOverride(42.0f)
			[
				SNew(SCheckBox)
				.Style(FAppStyle::Get(), "ToggleButtonCheckbox")
				.ToolTipText_Lambda([Mode, GetPaintTool, FallbackProperties]()
				{
					return GetApplyModeToolTip(GetProperties(GetPaintTool(), FallbackProperties), Mode);
				})
				.IsChecked_Lambda([Mode, GetPaintTool, FallbackProperties]()
				{
					return GetApplyMode(GetProperties(GetPaintTool(), FallbackProperties)) == Mode
						? ECheckBoxState::Checked
						: ECheckBoxState::Unchecked;
				})
				.OnCheckStateChanged_Lambda([Mode, GetPaintTool, FallbackProperties](ECheckBoxState NewState)
				{
					if (NewState == ECheckBoxState::Checked)
					{
						UQuickSDFPaintTool* Tool = GetPaintTool();
						SetApplyMode(Tool, GetProperties(Tool, FallbackProperties), Mode);
					}
				})
				.Padding(FMargin(6.0f, 4.0f))
				[
					SNew(STextBlock)
					.Text(GetApplyModeLabel(Mode))
					.Justification(ETextJustify::Center)
					.Font(FAppStyle::GetFontStyle("SmallFont"))
					.ColorAndOpacity_Lambda([Mode, GetPaintTool, FallbackProperties]()
					{
						return GetApplyMode(GetProperties(GetPaintTool(), FallbackProperties)) == Mode
							? FSlateColor(FLinearColor(0.35f, 0.82f, 1.0f, 1.0f))
							: FSlateColor(FLinearColor(0.68f, 0.68f, 0.68f, 1.0f));
					})
				]
			]
		];
	}

	return ModeGrid;
}

TSharedRef<SWidget> QuickSDFToolUI::MakeApplyDirectionSelector(FGetPaintTool GetPaintTool, TWeakObjectPtr<UQuickSDFToolProperties> FallbackProperties, bool bUseCompactLayout)
{
	if (bUseCompactLayout)
	{
		TSharedRef<SHorizontalBox> DirectionRow = SNew(SHorizontalBox);
		for (EQuickSDFApplyDirection Direction : GetApplyDirections())
		{
			DirectionRow->AddSlot()
			.AutoWidth()
			.Padding(0.5f, 0.0f)
			[
				SNew(SBox)
				.WidthOverride(54.0f)
				.HeightOverride(24.0f)
				[
					SNew(SCheckBox)
					.Style(FQuickSDFToolStyle::Get().Get(), "QuickSDF.Timeline.ToggleButton")
					.ToolTipText_Lambda([Direction, GetPaintTool, FallbackProperties]()
					{
						return GetApplyDirectionToolTip(GetProperties(GetPaintTool(), FallbackProperties), Direction);
					})
					.IsChecked_Lambda([Direction, GetPaintTool, FallbackProperties]()
					{
						return GetApplyDirection(GetProperties(GetPaintTool(), FallbackProperties)) == Direction
							? ECheckBoxState::Checked
							: ECheckBoxState::Unchecked;
					})
					.OnCheckStateChanged_Lambda([Direction, GetPaintTool, FallbackProperties](ECheckBoxState NewState)
					{
						if (NewState == ECheckBoxState::Checked)
						{
							UQuickSDFPaintTool* Tool = GetPaintTool();
							SetApplyDirection(Tool, GetProperties(Tool, FallbackProperties), Direction);
						}
					})
					.Padding(FMargin(5.0f, 2.0f))
					[
						SNew(STextBlock)
						.Text(GetApplyDirectionLabel(Direction))
						.Justification(ETextJustify::Center)
						.Font(FAppStyle::GetFontStyle("SmallFont"))
						.ColorAndOpacity_Lambda([Direction, GetPaintTool, FallbackProperties]()
						{
							return GetApplyDirection(GetProperties(GetPaintTool(), FallbackProperties)) == Direction
								? FSlateColor(FLinearColor(0.35f, 0.82f, 1.0f, 1.0f))
								: FSlateColor(FLinearColor(0.62f, 0.62f, 0.62f, 1.0f));
						})
					]
				]
			];
		}
		return DirectionRow;
	}

	TSharedRef<SUniformGridPanel> DirectionGrid = SNew(SUniformGridPanel)
		.SlotPadding(FMargin(3.0f));

	const TArray<EQuickSDFApplyDirection>& Directions = GetApplyDirections();
	for (int32 Index = 0; Index < Directions.Num(); ++Index)
	{
		const EQuickSDFApplyDirection Direction = Directions[Index];
		DirectionGrid->AddSlot(Index, 0)
		[
			SNew(SBox)
			.WidthOverride(92.0f)
			.HeightOverride(38.0f)
			[
				SNew(SCheckBox)
				.Style(FAppStyle::Get(), "ToggleButtonCheckbox")
				.ToolTipText_Lambda([Direction, GetPaintTool, FallbackProperties]()
				{
					return GetApplyDirectionToolTip(GetProperties(GetPaintTool(), FallbackProperties), Direction);
				})
				.IsChecked_Lambda([Direction, GetPaintTool, FallbackProperties]()
				{
					return GetApplyDirection(GetProperties(GetPaintTool(), FallbackProperties)) == Direction
						? ECheckBoxState::Checked
						: ECheckBoxState::Unchecked;
				})
				.OnCheckStateChanged_Lambda([Direction, GetPaintTool, FallbackProperties](ECheckBoxState NewState)
				{
					if (NewState == ECheckBoxState::Checked)
					{
						UQuickSDFPaintTool* Tool = GetPaintTool();
						SetApplyDirection(Tool, GetProperties(Tool, FallbackProperties), Direction);
					}
				})
				.Padding(FMargin(6.0f, 4.0f))
				[
					SNew(STextBlock)
					.Text(GetApplyDirectionLabel(Direction))
					.Justification(ETextJustify::Center)
					.Font(FAppStyle::GetFontStyle("SmallFont"))
					.ColorAndOpacity_Lambda([Direction, GetPaintTool, FallbackProperties]()
					{
						return GetApplyDirection(GetProperties(GetPaintTool(), FallbackProperties)) == Direction
							? FSlateColor(FLinearColor(0.35f, 0.82f, 1.0f, 1.0f))
							: FSlateColor(FLinearColor(0.68f, 0.68f, 0.68f, 1.0f));
					})
				]
			]
		];
	}

	return DirectionGrid;
}

TSharedRef<SWidget> QuickSDFToolUI::MakeApplyControls(FGetPaintTool GetPaintTool, TWeakObjectPtr<UQuickSDFToolProperties> FallbackProperties, bool bUseCompactLayout)
{
	if (bUseCompactLayout)
	{
		return SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(0.0f, 0.0f, 4.0f, 0.0f)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("ApplyCompactLabel", "Apply"))
				.Font(FAppStyle::GetFontStyle("SmallFont"))
				.ColorAndOpacity(FLinearColor(0.62f, 0.62f, 0.62f, 1.0f))
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			[
				MakeApplyModeSelector(GetPaintTool, FallbackProperties, true)
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(6.0f, 0.0f, 4.0f, 0.0f)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("ApplyDirectionCompactLabel", "Dir"))
				.Font(FAppStyle::GetFontStyle("SmallFont"))
				.ColorAndOpacity(FLinearColor(0.62f, 0.62f, 0.62f, 1.0f))
				.Visibility_Lambda([GetPaintTool, FallbackProperties]()
				{
					return GetApplyMode(GetProperties(GetPaintTool(), FallbackProperties)) == EQuickSDFApplyMode::Single
						? EVisibility::Collapsed
						: EVisibility::Visible;
				})
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			[
				SNew(SBox)
				.Visibility_Lambda([GetPaintTool, FallbackProperties]()
				{
					return GetApplyMode(GetProperties(GetPaintTool(), FallbackProperties)) == EQuickSDFApplyMode::Single
						? EVisibility::Collapsed
						: EVisibility::Visible;
				})
				[
					MakeApplyDirectionSelector(GetPaintTool, FallbackProperties, true)
				]
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(6.0f, 0.0f, 0.0f, 0.0f)
			[
				MakeGradientCurveQuickEditor(GetPaintTool, FallbackProperties)
			];
	}

	return SNew(SVerticalBox)
		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			MakeApplyModeSelector(GetPaintTool, FallbackProperties, false)
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0.0f, 5.0f, 0.0f, 0.0f)
		[
			SNew(SBox)
			.Visibility_Lambda([GetPaintTool, FallbackProperties]()
			{
				return GetApplyMode(GetProperties(GetPaintTool(), FallbackProperties)) == EQuickSDFApplyMode::Single
					? EVisibility::Collapsed
					: EVisibility::Visible;
			})
			[
				MakeApplyDirectionSelector(GetPaintTool, FallbackProperties, false)
			]
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0.0f, 5.0f, 0.0f, 0.0f)
		[
			MakeGradientCurveQuickEditor(GetPaintTool, FallbackProperties)
		];
}

TSharedRef<SWidget> QuickSDFToolUI::MakeSymmetryModeSelector(FGetPaintTool GetPaintTool, TWeakObjectPtr<UQuickSDFToolProperties> FallbackProperties)
{
	TSharedRef<SUniformGridPanel> SymmetryGrid = SNew(SUniformGridPanel)
		.SlotPadding(FMargin(3.0f));

	const TArray<EQuickSDFSymmetryMode>& Modes = GetSymmetryModes();
	for (int32 Index = 0; Index < Modes.Num(); ++Index)
	{
		const EQuickSDFSymmetryMode Mode = Modes[Index];
		SymmetryGrid->AddSlot(Index, 0)
		[
			SNew(SBox)
			.WidthOverride(78.0f)
			.HeightOverride(34.0f)
			[
				SNew(SCheckBox)
				.Style(FAppStyle::Get(), "ToggleButtonCheckbox")
				.ToolTipText_Lambda([Mode, GetPaintTool, FallbackProperties]()
				{
					UQuickSDFPaintTool* Tool = GetPaintTool();
					if (Tool && Tool->Properties && Tool->Properties->SymmetryMode == EQuickSDFSymmetryMode::Auto)
					{
						Tool->ResolveEffectiveSymmetryMode();
					}
					UQuickSDFToolProperties* Properties = GetProperties(Tool, FallbackProperties);
					if (!Properties || Mode != EQuickSDFSymmetryMode::Auto || Properties->AutoSymmetryStatus.IsEmpty())
					{
						return GetSymmetryModeDescription(Mode);
					}
					return FText::Format(
						LOCTEXT("SymmetryModeTooltipFormat", "{0}\n{1}"),
						GetSymmetryModeDescription(Mode),
						Properties->AutoSymmetryStatus);
				})
				.IsChecked_Lambda([Mode, GetPaintTool, FallbackProperties]()
				{
					const UQuickSDFToolProperties* Properties = GetProperties(GetPaintTool(), FallbackProperties);
					return Properties && Properties->SymmetryMode == Mode
							? ECheckBoxState::Checked
							: ECheckBoxState::Unchecked;
				})
				.OnCheckStateChanged_Lambda([Mode, GetPaintTool, FallbackProperties](ECheckBoxState NewState)
				{
					if (NewState == ECheckBoxState::Checked)
					{
						UQuickSDFPaintTool* Tool = GetPaintTool();
						SetSymmetryMode(Tool, GetProperties(Tool, FallbackProperties), Mode);
					}
				})
				.Padding(FMargin(6.0f, 4.0f))
				[
					SNew(STextBlock)
					.Text(GetSymmetryModeLabel(Mode))
					.Justification(ETextJustify::Center)
					.Font(FAppStyle::GetFontStyle("SmallFont"))
					.ColorAndOpacity_Lambda([Mode, GetPaintTool, FallbackProperties]()
					{
						const UQuickSDFToolProperties* Properties = GetProperties(GetPaintTool(), FallbackProperties);
						return Properties && Properties->SymmetryMode == Mode
							? FSlateColor(FLinearColor(0.35f, 0.82f, 1.0f, 1.0f))
							: FSlateColor(FLinearColor(0.68f, 0.68f, 0.68f, 1.0f));
					})
				]
			]
		];
	}

	return SymmetryGrid;
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
				UQuickSDFPaintTool* Tool = GetPaintTool();
				if (Toggle == EQuickSDFPaintToggle::Symmetry && Tool)
				{
					Tool->ResolveEffectiveSymmetryMode();
				}
				return GetToggleToolTip(GetProperties(Tool, FallbackProperties), Toggle);
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
		MakeApplyControls(GetPaintTool, FallbackProperties, true)
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
					return GetMaterialPreviewModeToolTip(Tool, Tool ? Tool->Properties : nullptr, Mode);
				})
				.IsEnabled_Lambda([Mode, GetPaintTool]()
				{
					return IsMaterialPreviewModeEnabled(GetPaintTool(), Mode);
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

	TSharedRef<SUniformGridPanel> Grid = SNew(SUniformGridPanel)
		.SlotPadding(FMargin(3.0f));

	const TArray<EQuickSDFPaintToggle>& Toggles = GetPaintToggles();
	int32 VisibleToggleIndex = 0;
	for (int32 Index = 0; Index < Toggles.Num(); ++Index)
	{
		const EQuickSDFPaintToggle Toggle = Toggles[Index];
		if (Toggle == EQuickSDFPaintToggle::Symmetry)
		{
			continue;
		}

		Grid->AddSlot(VisibleToggleIndex % 4, VisibleToggleIndex / 4)
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
		++VisibleToggleIndex;
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
			.Padding(3.0f, 4.0f, 0.0f, 0.0f)
			[
				MakeAutoSDFPreviewToggle(GetPaintTool)
			]
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.0f, 6.0f, 0.0f, 0.0f)
			[
				MakeMeshPaintModeSelector(GetPaintTool, TWeakObjectPtr<UQuickSDFToolProperties>(), false)
			]
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.0f, 6.0f, 0.0f, 0.0f)
			[
				MakeApplyControls(GetPaintTool, TWeakObjectPtr<UQuickSDFToolProperties>(), false)
			]
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.0f, 6.0f, 0.0f, 0.0f)
			[
				MakeSymmetryModeSelector(GetPaintTool)
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
