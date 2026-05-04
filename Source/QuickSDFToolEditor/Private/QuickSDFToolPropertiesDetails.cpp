#include "QuickSDFToolPropertiesDetails.h"

#include "Components/MeshComponent.h"
#include "DetailCategoryBuilder.h"
#include "DetailLayoutBuilder.h"
#include "DetailWidgetRow.h"
#include "Editor.h"
#include "GameFramework/Actor.h"
#include "InputCoreTypes.h"
#include "PropertyHandle.h"
#include "QuickSDFAsset.h"
#include "QuickSDFPaintTool.h"
#include "QuickSDFToolProperties.h"
#include "QuickSDFToolSubsystem.h"
#include "QuickSDFToolStyle.h"
#include "QuickSDFToolUI.h"
#include "Styling/AppStyle.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SSpacer.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "QuickSDFToolPropertiesDetails"

namespace
{
bool HasActiveSourceMasks();
bool CanCreateThresholdMap();

UMeshComponent* GetCurrentQuickSDFTarget()
{
	if (!GEditor)
	{
		return nullptr;
	}

	if (UQuickSDFToolSubsystem* Subsystem = GEditor->GetEditorSubsystem<UQuickSDFToolSubsystem>())
	{
		return Subsystem->GetTargetMeshComponent();
	}

	return nullptr;
}

FText GetTargetMeshText()
{
	if (UMeshComponent* TargetComponent = GetCurrentQuickSDFTarget())
	{
		if (AActor* Owner = TargetComponent->GetOwner())
		{
			return FText::FromString(FString::Printf(TEXT("%s / %s"), *Owner->GetActorLabel(), *TargetComponent->GetName()));
		}
		return FText::FromString(TargetComponent->GetName());
	}

	if (HasActiveSourceMasks())
	{
		return LOCTEXT("ImportedMasksReady", "Imported masks ready");
	}

	return LOCTEXT("NoTargetMesh", "No mesh selected");
}

FSlateColor GetTargetMeshColor()
{
	return CanCreateThresholdMap()
		? FSlateColor(FLinearColor(0.62f, 0.94f, 0.68f, 1.0f))
		: FSlateColor(FLinearColor(1.0f, 0.55f, 0.45f, 1.0f));
}

bool HasActiveSourceMasks()
{
	if (!GEditor)
	{
		return false;
	}

	UQuickSDFToolSubsystem* Subsystem = GEditor->GetEditorSubsystem<UQuickSDFToolSubsystem>();
	const UQuickSDFAsset* Asset = Subsystem ? Subsystem->GetActiveSDFAsset() : nullptr;
	if (!Asset)
	{
		return false;
	}

	for (const FQuickSDFAngleData& AngleData : Asset->GetActiveAngleDataList())
	{
		if (AngleData.TextureMask)
		{
			return true;
		}
	}

	return false;
}

bool CanCreateThresholdMap()
{
	return GetCurrentQuickSDFTarget() != nullptr || HasActiveSourceMasks();
}

void AddPropertyIfValid(IDetailCategoryBuilder& Category, const TSharedPtr<IPropertyHandle>& PropertyHandle)
{
	if (PropertyHandle.IsValid() && PropertyHandle->IsValidHandle())
	{
		Category.AddProperty(PropertyHandle);
	}
}

UQuickSDFPaintTool* GetActiveQuickSDFPaintTool()
{
	return QuickSDFToolUI::GetActivePaintTool();
}

const UQuickSDFAsset* GetActiveQuickSDFAsset()
{
	const UQuickSDFToolSubsystem* Subsystem = GEditor ? GEditor->GetEditorSubsystem<UQuickSDFToolSubsystem>() : nullptr;
	return Subsystem ? Subsystem->GetActiveSDFAsset() : nullptr;
}

const FQuickSDFTextureSetData* GetTextureSetData(int32 TextureSetIndex)
{
	const UQuickSDFAsset* Asset = GetActiveQuickSDFAsset();
	return Asset && Asset->TextureSets.IsValidIndex(TextureSetIndex)
		? &Asset->TextureSets[TextureSetIndex]
		: nullptr;
}

bool IsTextureSetActive(int32 TextureSetIndex)
{
	const UQuickSDFPaintTool* Tool = GetActiveQuickSDFPaintTool();
	const UQuickSDFToolProperties* Props = Tool ? Tool->Properties : nullptr;
	if (Props)
	{
		return Props->ActiveTextureSetIndex == TextureSetIndex;
	}

	const UQuickSDFAsset* Asset = GetActiveQuickSDFAsset();
	return Asset && Asset->ActiveTextureSetIndex == TextureSetIndex;
}

FText GetTextureSetIndexText(int32 TextureSetIndex)
{
	if (const FQuickSDFTextureSetData* TextureSet = GetTextureSetData(TextureSetIndex))
	{
		return FText::AsNumber(TextureSet->MaterialSlotIndex);
	}
	return FText::AsNumber(TextureSetIndex);
}

FText GetTextureSetSlotNameText(int32 TextureSetIndex)
{
	if (const FQuickSDFTextureSetData* TextureSet = GetTextureSetData(TextureSetIndex))
	{
		return FText::FromName(TextureSet->SlotName);
	}
	return LOCTEXT("MissingTextureSetRow", "Missing");
}

FText GetTextureSetMaterialText(int32 TextureSetIndex)
{
	if (const FQuickSDFTextureSetData* TextureSet = GetTextureSetData(TextureSetIndex))
	{
		return TextureSet->MaterialName.IsEmpty()
			? LOCTEXT("TextureSetNoMaterial", "No material")
			: FText::FromString(TextureSet->MaterialName);
	}
	return FText::GetEmpty();
}

FName GetTextureSetStatusBrushName(int32 TextureSetIndex)
{
	static const FName BakedBrushName(TEXT("QuickSDF.MaterialSlot.Status.Baked"));
	static const FName EmptyBrushName(TEXT("QuickSDF.MaterialSlot.Status.Empty"));
	static const FName DirtyBrushName(TEXT("QuickSDF.MaterialSlot.Status.Dirty"));
	static const FName WarningBrushName(TEXT("QuickSDF.MaterialSlot.Status.Warning"));
	static const FName MissingBrushName(TEXT("QuickSDF.MaterialSlot.Status.Missing"));

	const FQuickSDFTextureSetData* TextureSet = GetTextureSetData(TextureSetIndex);
	if (!TextureSet)
	{
		return MissingBrushName;
	}
	if (TextureSet->bHasWarning)
	{
		return WarningBrushName;
	}
	if (TextureSet->bDirty)
	{
		return DirtyBrushName;
	}
	if (TextureSet->bInitialBakeComplete)
	{
		return BakedBrushName;
	}
	return EmptyBrushName;
}

FName GetTextureSetIndexBadgeBrushName(int32 TextureSetIndex)
{
	return IsTextureSetActive(TextureSetIndex)
		? FName(TEXT("QuickSDF.MaterialSlot.IndexBadge.Active"))
		: FName(TEXT("QuickSDF.MaterialSlot.IndexBadge"));
}

FSlateColor GetTextureSetPrimaryTextColor(int32 TextureSetIndex)
{
	return IsTextureSetActive(TextureSetIndex)
		? FSlateColor(FLinearColor(0.96f, 0.98f, 1.0f, 1.0f))
		: FSlateColor(FLinearColor(0.82f, 0.84f, 0.86f, 1.0f));
}

FSlateColor GetTextureSetSecondaryTextColor(int32 TextureSetIndex)
{
	return IsTextureSetActive(TextureSetIndex)
		? FSlateColor(FLinearColor(0.64f, 0.82f, 0.90f, 1.0f))
		: FSlateColor(FLinearColor(0.55f, 0.56f, 0.58f, 1.0f));
}

FLinearColor GetTextureSetAccentColor(int32 TextureSetIndex)
{
	return IsTextureSetActive(TextureSetIndex)
		? FLinearColor(0.35f, 0.82f, 1.0f, 1.0f)
		: FLinearColor(0.0f, 0.0f, 0.0f, 0.0f);
}

void SyncPropertiesFromTextureSet(UQuickSDFToolProperties* Properties, const UQuickSDFAsset* Asset, int32 TextureSetIndex)
{
	if (!Properties || !Asset)
	{
		return;
	}

	const FQuickSDFTextureSetData* ActiveSet = Asset->GetActiveTextureSet();
	Properties->Modify();
	Properties->ActiveTextureSetIndex = TextureSetIndex;
	Properties->Resolution = Asset->GetActiveResolution();
	Properties->UVChannel = Asset->GetActiveUVChannel();
	Properties->TargetMaterialSlot = ActiveSet ? ActiveSet->MaterialSlotIndex : INDEX_NONE;
	Properties->NumAngles = Asset->GetActiveAngleDataList().Num();
	Properties->TargetAngles.SetNum(Properties->NumAngles);
	Properties->TargetTextures.SetNum(Properties->NumAngles);

	for (int32 Index = 0; Index < Properties->NumAngles; ++Index)
	{
		const FQuickSDFAngleData& AngleData = Asset->GetActiveAngleDataList()[Index];
		Properties->TargetAngles[Index] = AngleData.Angle;
		Properties->TargetTextures[Index] = AngleData.TextureMask;
	}
}

FReply SelectTextureSet(TWeakObjectPtr<UQuickSDFToolProperties> WeakProperties, int32 TextureSetIndex)
{
	if (UQuickSDFPaintTool* Tool = GetActiveQuickSDFPaintTool())
	{
		if (Tool->SelectTextureSet(TextureSetIndex))
		{
			return FReply::Handled();
		}
	}

	UQuickSDFToolSubsystem* Subsystem = GEditor ? GEditor->GetEditorSubsystem<UQuickSDFToolSubsystem>() : nullptr;
	UQuickSDFAsset* Asset = Subsystem ? Subsystem->GetActiveSDFAsset() : nullptr;
	if (Asset && Asset->TextureSets.IsValidIndex(TextureSetIndex))
	{
		Asset->Modify();
		if (Asset->SetActiveTextureSetIndex(TextureSetIndex))
		{
			SyncPropertiesFromTextureSet(WeakProperties.Get(), Asset, TextureSetIndex);
			if (GEditor)
			{
				GEditor->RedrawAllViewports(false);
			}
		}
	}
	return FReply::Handled();
}

FReply BakeTextureSet(int32 TextureSetIndex)
{
	if (UQuickSDFPaintTool* Tool = GetActiveQuickSDFPaintTool())
	{
		Tool->SelectTextureSet(TextureSetIndex);
		Tool->BakeSelectedTextureSet();
	}
	return FReply::Handled();
}

TSharedRef<SWidget> MakeTextureSetStatusPill(int32 TextureSetIndex)
{
	return SNew(SBorder)
		.BorderImage_Lambda([TextureSetIndex]()
		{
			return FQuickSDFToolStyle::GetBrush(GetTextureSetStatusBrushName(TextureSetIndex));
		})
		.Padding(FMargin(7.0f, 2.0f))
		[
			SNew(STextBlock)
			.Text_Lambda([TextureSetIndex]()
			{
				const UQuickSDFPaintTool* Tool = GetActiveQuickSDFPaintTool();
				return Tool ? Tool->GetTextureSetStatusText(TextureSetIndex) : LOCTEXT("TextureSetNoTool", "Idle");
			})
			.Font(FAppStyle::GetFontStyle("SmallFont"))
			.ColorAndOpacity(FLinearColor(0.92f, 0.94f, 0.95f, 1.0f))
		];
}

TSharedRef<SWidget> MakeTextureSetBakeButton(int32 TextureSetIndex)
{
	return SNew(SBox)
		.WidthOverride(30.0f)
		.HeightOverride(26.0f)
		[
			SNew(SButton)
			.ButtonStyle(FQuickSDFToolStyle::Get().Get(), "QuickSDF.MaterialSlot.ActionButton")
			.HAlign(HAlign_Center)
			.VAlign(VAlign_Center)
			.ContentPadding(FMargin(0.0f))
			.ToolTipText(LOCTEXT("BakeTextureSetTooltip", "Bake only this material slot."))
			.OnClicked_Lambda([TextureSetIndex]()
			{
				return BakeTextureSet(TextureSetIndex);
			})
			[
				SNew(SBox)
				.WidthOverride(16.0f)
				.HeightOverride(16.0f)
				[
					SNew(SImage)
					.Image(FQuickSDFToolStyle::GetBrush("QuickSDF.Action.Bake"))
					.ColorAndOpacity(FLinearColor(0.82f, 0.86f, 0.88f, 1.0f))
				]
			]
		];
}

TSharedRef<SWidget> MakeTextureSetRow(TWeakObjectPtr<UQuickSDFToolProperties> WeakProperties, int32 TextureSetIndex)
{
	return SNew(SCheckBox)
		.Style(FQuickSDFToolStyle::Get().Get(), "QuickSDF.MaterialSlot.Row")
		.ToolTipText(LOCTEXT("SelectTextureSetTooltip", "Make this material slot active for painting, baking, preview, and SDF generation."))
		.IsChecked_Lambda([TextureSetIndex]()
		{
			return IsTextureSetActive(TextureSetIndex) ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
		})
		.OnCheckStateChanged_Lambda([WeakProperties, TextureSetIndex](ECheckBoxState NewState)
		{
			if (NewState == ECheckBoxState::Checked)
			{
				SelectTextureSet(WeakProperties, TextureSetIndex);
			}
		})
		.Padding(FMargin(0.0f))
		[
			SNew(SBorder)
			.BorderImage(FAppStyle::GetBrush("NoBorder"))
			.Padding(FMargin(0.0f))
			.OnMouseButtonDown_Lambda([WeakProperties, TextureSetIndex](const FGeometry&, const FPointerEvent& MouseEvent)
			{
				return MouseEvent.GetEffectingButton() == EKeys::LeftMouseButton
					? SelectTextureSet(WeakProperties, TextureSetIndex)
					: FReply::Unhandled();
			})
			[
				SNew(SBox)
				.MinDesiredHeight(45.0f)
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Fill)
					[
						SNew(SBox)
						.WidthOverride(3.0f)
						[
							SNew(SBorder)
							.BorderImage(FAppStyle::GetBrush("WhiteBrush"))
							.BorderBackgroundColor_Lambda([TextureSetIndex]()
							{
								return GetTextureSetAccentColor(TextureSetIndex);
							})
						]
					]
					+ SHorizontalBox::Slot()
					.FillWidth(1.0f)
					.VAlign(VAlign_Center)
					.Padding(8.0f, 5.0f, 6.0f, 5.0f)
					[
						SNew(SVerticalBox)
						+ SVerticalBox::Slot()
						.AutoHeight()
						[
							SNew(SHorizontalBox)
							+ SHorizontalBox::Slot()
							.AutoWidth()
							.VAlign(VAlign_Center)
							.Padding(0.0f, 0.0f, 6.0f, 0.0f)
							[
								SNew(SBorder)
								.BorderImage_Lambda([TextureSetIndex]()
								{
									return FQuickSDFToolStyle::GetBrush(GetTextureSetIndexBadgeBrushName(TextureSetIndex));
								})
								.Padding(FMargin(6.0f, 1.0f))
								[
									SNew(STextBlock)
									.Text_Lambda([TextureSetIndex]()
									{
										return GetTextureSetIndexText(TextureSetIndex);
									})
									.Font(FAppStyle::GetFontStyle("SmallFont"))
									.ColorAndOpacity(FLinearColor(0.90f, 0.93f, 0.95f, 1.0f))
								]
							]
							+ SHorizontalBox::Slot()
							.FillWidth(1.0f)
							.VAlign(VAlign_Center)
							[
								SNew(STextBlock)
								.Text_Lambda([TextureSetIndex]()
								{
									return GetTextureSetSlotNameText(TextureSetIndex);
								})
								.Font(FAppStyle::GetFontStyle("PropertyWindow.NormalFont"))
								.ColorAndOpacity_Lambda([TextureSetIndex]()
								{
									return GetTextureSetPrimaryTextColor(TextureSetIndex);
								})
							]
						]
						+ SVerticalBox::Slot()
						.AutoHeight()
						.Padding(0.0f, 2.0f, 0.0f, 0.0f)
						[
							SNew(STextBlock)
							.Text_Lambda([TextureSetIndex]()
							{
								return GetTextureSetMaterialText(TextureSetIndex);
							})
							.Font(FAppStyle::GetFontStyle("SmallFont"))
							.ColorAndOpacity_Lambda([TextureSetIndex]()
							{
								return GetTextureSetSecondaryTextColor(TextureSetIndex);
							})
						]
					]
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					.Padding(0.0f, 0.0f, 6.0f, 0.0f)
					[
						MakeTextureSetStatusPill(TextureSetIndex)
					]
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					.Padding(0.0f, 0.0f, 6.0f, 0.0f)
					[
						MakeTextureSetBakeButton(TextureSetIndex)
					]
				]
			]
		];
}

TSharedRef<SWidget> MakeTextureSetList(TWeakObjectPtr<UQuickSDFToolProperties> WeakProperties)
{
	TSharedRef<SVerticalBox> Rows = SNew(SVerticalBox);
	const UQuickSDFToolSubsystem* Subsystem = GEditor ? GEditor->GetEditorSubsystem<UQuickSDFToolSubsystem>() : nullptr;
	const UQuickSDFAsset* Asset = Subsystem ? Subsystem->GetActiveSDFAsset() : nullptr;
	const int32 TextureSetCount = Asset ? Asset->TextureSets.Num() : 0;

	if (TextureSetCount == 0)
	{
		Rows->AddSlot()
		.AutoHeight()
		.Padding(0.0f, 2.0f)
		[
			SNew(STextBlock)
			.Text(LOCTEXT("NoTextureSets", "No Texture Sets"))
			.Font(FAppStyle::GetFontStyle("SmallFont"))
		];
	}
	else
	{
		for (int32 TextureSetIndex = 0; TextureSetIndex < TextureSetCount; ++TextureSetIndex)
		{
			Rows->AddSlot()
			.AutoHeight()
			.Padding(0.0f, 1.0f)
			[
				MakeTextureSetRow(WeakProperties, TextureSetIndex)
			];
		}
	}

	return SNew(SBox)
		.MaxDesiredHeight(244.0f)
		[
			SNew(SScrollBox)
			+ SScrollBox::Slot()
			[
				Rows
			]
		];
}
}

TSharedRef<IDetailCustomization> FQuickSDFToolPropertiesDetails::MakeInstance()
{
	return MakeShareable(new FQuickSDFToolPropertiesDetails);
}

void FQuickSDFToolPropertiesDetails::CustomizeDetails(IDetailLayoutBuilder& DetailBuilder)
{
	TArray<TWeakObjectPtr<UObject>> CustomizedObjects;
	DetailBuilder.GetObjectsBeingCustomized(CustomizedObjects);

	UQuickSDFToolProperties* Properties = nullptr;
	if (CustomizedObjects.Num() > 0)
	{
		Properties = Cast<UQuickSDFToolProperties>(CustomizedObjects[0].Get());
	}

	const TWeakObjectPtr<UQuickSDFToolProperties> WeakProperties(Properties);

	DetailBuilder.HideCategory(FName(TEXT("Quick Start")));
	DetailBuilder.HideCategory(FName(TEXT("Asset Settings")));
	DetailBuilder.HideCategory(FName(TEXT("Paint Settings")));
	DetailBuilder.HideCategory(FName(TEXT("Target Settings")));
	DetailBuilder.HideCategory(FName(TEXT("Texture Sets")));
	DetailBuilder.HideCategory(FName(TEXT("Export Settings")));
	DetailBuilder.HideCategory(FName(TEXT("Actions")));
	DetailBuilder.HideProperty(DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UQuickSDFToolProperties, TargetTextures)));
	DetailBuilder.HideProperty(DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UQuickSDFToolProperties, TargetAngles)));
	DetailBuilder.HideProperty(DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UQuickSDFToolProperties, ActiveTextureSetIndex)));

	IDetailCategoryBuilder& QuickCategory = DetailBuilder.EditCategory(FName(TEXT("Quick SDF")), LOCTEXT("QuickSDFCategory", "Quick SDF"), ECategoryPriority::Important);
	IDetailCategoryBuilder& MasksCategory = DetailBuilder.EditCategory(FName(TEXT("Masks")), LOCTEXT("MasksCategory", "Masks"), ECategoryPriority::Important);
	IDetailCategoryBuilder& OutputCategory = DetailBuilder.EditCategory(FName(TEXT("Output")), LOCTEXT("OutputCategory", "Output"), ECategoryPriority::Important);
	IDetailCategoryBuilder& AdvancedCategory = DetailBuilder.EditCategory(FName(TEXT("Advanced")), LOCTEXT("AdvancedCategory", "Advanced"), ECategoryPriority::Uncommon);
	AdvancedCategory.InitiallyCollapsed(true);

	QuickCategory.AddCustomRow(LOCTEXT("TargetMeshFilter", "Target Mesh"))
	.NameContent()
	[
		SNew(STextBlock)
		.Text(LOCTEXT("TargetMeshLabel", "Target Mesh"))
		.Font(FAppStyle::GetFontStyle("PropertyWindow.NormalFont"))
	]
	.ValueContent()
	.MinDesiredWidth(180.0f)
	[
		SNew(STextBlock)
		.Text_Lambda([]()
		{
			return GetTargetMeshText();
		})
		.ColorAndOpacity_Lambda([]()
		{
			return GetTargetMeshColor();
		})
		.Font(FAppStyle::GetFontStyle("PropertyWindow.NormalFont"))
	];

	QuickCategory.AddCustomRow(LOCTEXT("TextureSetsFilter", "Material Slots Texture Sets Bake"))
	.WholeRowContent()
	[
		SNew(SVerticalBox)
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0.0f, 4.0f, 0.0f, 2.0f)
		[
			SNew(STextBlock)
			.Text(LOCTEXT("TextureSetsLabel", "Material Slots"))
			.Font(FAppStyle::GetFontStyle("PropertyWindow.BoldFont"))
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			MakeTextureSetList(WeakProperties)
		]
	];

	QuickCategory.AddCustomRow(LOCTEXT("MaterialPreviewFilter", "Material Preview"))
	.WholeRowContent()
	[
		SNew(SVerticalBox)
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0.0f, 4.0f, 0.0f, 2.0f)
		[
			SNew(STextBlock)
			.Text(LOCTEXT("MaterialPreviewLabel", "Material Preview"))
			.Font(FAppStyle::GetFontStyle("PropertyWindow.BoldFont"))
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			[
				QuickSDFToolUI::MakeMaterialPreviewModeSelector([]()
				{
					return QuickSDFToolUI::GetActivePaintTool();
				}, WeakProperties)
			]
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0.0f, 4.0f, 0.0f, 0.0f)
		[
			QuickSDFToolUI::MakeAutoSDFPreviewToggle([]()
			{
				return QuickSDFToolUI::GetActivePaintTool();
			}, WeakProperties)
		]
	];

	QuickCategory.AddCustomRow(LOCTEXT("PaintTogglesFilter", "Paint Toggles"))
	.WholeRowContent()
	[
		SNew(SVerticalBox)
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0.0f, 4.0f, 0.0f, 2.0f)
		[
			SNew(STextBlock)
			.Text(LOCTEXT("PaintTogglesLabel", "Paint Toggles"))
			.Font(FAppStyle::GetFontStyle("PropertyWindow.BoldFont"))
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			QuickSDFToolUI::MakePaintToggleBar([]()
			{
				return QuickSDFToolUI::GetActivePaintTool();
			}, WeakProperties)
		]
	];

	AddPropertyIfValid(QuickCategory, DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UQuickSDFToolProperties, QualityPreset)));

	QuickCategory.AddCustomRow(LOCTEXT("QuickActionsFilter", "Create Threshold Map Import Assets Export Assets Export Files Overwrite Source Fill White Fill Black"))
	.WholeRowContent()
	[
		SNew(SVerticalBox)
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0.0f, 4.0f, 0.0f, 2.0f)
		[
			SNew(SBox)
			.IsEnabled_Lambda([]()
			{
				return CanCreateThresholdMap();
			})
			[
				QuickSDFToolUI::MakeIconLabelButton(
					"QuickSDF.Action.CreateThresholdMap",
					LOCTEXT("CreateThresholdMapButton", "Generate Selected SDF"),
					LOCTEXT("CreateThresholdMapTooltip", "Create the SDF threshold map from the active Texture Set masks"),
					FOnClicked::CreateLambda([WeakProperties]()
					{
						if (UQuickSDFToolProperties* Props = WeakProperties.Get())
						{
							Props->CreateQuickThresholdMap();
						}
						return FReply::Handled();
					}))
			]
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0.0f, 2.0f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			.Padding(0.0f, 0.0f, 2.0f, 0.0f)
			[
				QuickSDFToolUI::MakeIconLabelButton(
					"QuickSDF.Action.ImportMasks",
					LOCTEXT("ImportEditedMasksButton", "Import Assets"),
					LOCTEXT("ImportEditedMasksTooltip", "Open the mask import panel. Choose Texture2D assets in the panel or drag them onto it."),
					FOnClicked::CreateLambda([WeakProperties]()
					{
						if (UQuickSDFToolProperties* Props = WeakProperties.Get())
						{
							Props->ImportEditedMasks();
						}
						return FReply::Handled();
					}))
			]
			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			.Padding(2.0f, 0.0f, 0.0f, 0.0f)
			[
				QuickSDFToolUI::MakeIconLabelButton(
					"QuickSDF.Action.ExportMasks",
					LOCTEXT("OverwriteSourceTexturesButton", "Overwrite Source"),
					LOCTEXT("OverwriteSourceTexturesTooltip", "Write the current masks back to Texture2D assets that were explicitly marked writable during import."),
					FOnClicked::CreateLambda([WeakProperties]()
					{
						if (UQuickSDFToolProperties* Props = WeakProperties.Get())
						{
							Props->OverwriteSourceTextures();
						}
						return FReply::Handled();
					}))
			]
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0.0f, 2.0f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			.Padding(0.0f, 0.0f, 2.0f, 0.0f)
			[
				QuickSDFToolUI::MakeIconLabelButton(
					"QuickSDF.Action.ExportMasks",
					LOCTEXT("ExportMaskAssetsButton", "Export Assets"),
					LOCTEXT("ExportMaskAssetsTooltip", "Export the active Texture Set masks as Texture2D assets."),
					FOnClicked::CreateLambda([WeakProperties]()
					{
						if (UQuickSDFToolProperties* Props = WeakProperties.Get())
						{
							Props->ExportMaskTexturesToAssets();
						}
						return FReply::Handled();
					}))
			]
			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			.Padding(2.0f, 0.0f, 0.0f, 0.0f)
			[
				QuickSDFToolUI::MakeIconLabelButton(
					"QuickSDF.Action.ExportMasks",
					LOCTEXT("ExportMaskFilesButton", "Export Files"),
					LOCTEXT("ExportMaskFilesTooltip", "Export the active Texture Set masks as PNG files."),
					FOnClicked::CreateLambda([WeakProperties]()
					{
						if (UQuickSDFToolProperties* Props = WeakProperties.Get())
						{
							Props->ExportMaskTexturesToFiles();
						}
						return FReply::Handled();
					}))
			]
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0.0f, 6.0f, 0.0f, 2.0f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			.Padding(0.0f, 0.0f, 2.0f, 0.0f)
			[
				QuickSDFToolUI::MakeIconLabelButton(
					"QuickSDF.Action.FillWhite",
					LOCTEXT("FillCurrentWhiteButton", "Fill White"),
					LOCTEXT("FillCurrentWhiteTooltip", "Fill the current mask with white"),
					FOnClicked::CreateLambda([WeakProperties]()
					{
						if (UQuickSDFToolProperties* Props = WeakProperties.Get())
						{
							Props->FillCurrentMaskWhite();
						}
						return FReply::Handled();
					}))
			]
			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			.Padding(2.0f, 0.0f, 0.0f, 0.0f)
			[
				QuickSDFToolUI::MakeIconLabelButton(
					"QuickSDF.Action.FillBlack",
					LOCTEXT("FillCurrentBlackButton", "Fill Black"),
					LOCTEXT("FillCurrentBlackTooltip", "Fill the current mask with black"),
					FOnClicked::CreateLambda([WeakProperties]()
					{
						if (UQuickSDFToolProperties* Props = WeakProperties.Get())
						{
							Props->FillCurrentMaskBlack();
						}
						return FReply::Handled();
					}))
			]
		]
	];

	AddPropertyIfValid(MasksCategory, DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UQuickSDFToolProperties, TargetAsset)));
	AddPropertyIfValid(MasksCategory, DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UQuickSDFToolProperties, QuickSDFAssetFolder)));
	AddPropertyIfValid(MasksCategory, DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UQuickSDFToolProperties, QuickSDFAssetName)));
	AddPropertyIfValid(MasksCategory, DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UQuickSDFToolProperties, bSaveMaskTexturesWithAsset)));
	MasksCategory.AddCustomRow(LOCTEXT("SaveQuickSDFAssetFilter", "Save QuickSDF Asset"))
	.WholeRowContent()
	[
		QuickSDFToolUI::MakeIconLabelButton(
			"QuickSDF.Action.SaveAsset",
			LOCTEXT("SaveQuickSDFAssetButton", "Save QuickSDF Asset"),
			LOCTEXT("SaveQuickSDFAssetTooltip", "Save the active QuickSDF asset"),
			FOnClicked::CreateLambda([WeakProperties]()
			{
				if (UQuickSDFToolProperties* Props = WeakProperties.Get())
				{
					Props->SaveQuickSDFAsset();
				}
				return FReply::Handled();
			}))
	];

	AddPropertyIfValid(OutputCategory, DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UQuickSDFToolProperties, SDFOutputFolder)));
	AddPropertyIfValid(OutputCategory, DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UQuickSDFToolProperties, SDFTextureName)));
	AddPropertyIfValid(OutputCategory, DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UQuickSDFToolProperties, bOverwriteExistingSDF)));
	AddPropertyIfValid(OutputCategory, DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UQuickSDFToolProperties, SDFOutputFormat)));

	AddPropertyIfValid(AdvancedCategory, DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UQuickSDFToolProperties, Resolution)));
	AddPropertyIfValid(AdvancedCategory, DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UQuickSDFToolProperties, UVChannel)));
	AddPropertyIfValid(AdvancedCategory, DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UQuickSDFToolProperties, bShowPreview)));
	AddPropertyIfValid(AdvancedCategory, DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UQuickSDFToolProperties, bOverlayUV)));
	AddPropertyIfValid(AdvancedCategory, DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UQuickSDFToolProperties, bAutoSyncLight)));
	AddPropertyIfValid(AdvancedCategory, DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UQuickSDFToolProperties, PaintTargetMode)));
	AddPropertyIfValid(AdvancedCategory, DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UQuickSDFToolProperties, BrushProjectionMode)));
	AddPropertyIfValid(AdvancedCategory, DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UQuickSDFToolProperties, bEnableQuickLine)));
	AddPropertyIfValid(AdvancedCategory, DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UQuickSDFToolProperties, QuickLineHoldTime)));
	AddPropertyIfValid(AdvancedCategory, DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UQuickSDFToolProperties, QuickLineMoveTolerance)));
	AddPropertyIfValid(AdvancedCategory, DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UQuickSDFToolProperties, bEnableStrokeStabilizer)));
	AddPropertyIfValid(AdvancedCategory, DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UQuickSDFToolProperties, StrokeStabilizerRadius)));
	AddPropertyIfValid(AdvancedCategory, DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UQuickSDFToolProperties, StrokeSpacingRatio)));
	AddPropertyIfValid(AdvancedCategory, DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UQuickSDFToolProperties, bEnableBrushAntialiasing)));
	AddPropertyIfValid(AdvancedCategory, DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UQuickSDFToolProperties, BrushAntialiasingWidth)));
	AddPropertyIfValid(AdvancedCategory, DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UQuickSDFToolProperties, bEnableOnionSkin)));
	AddPropertyIfValid(AdvancedCategory, DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UQuickSDFToolProperties, SymmetryMode)));
	AddPropertyIfValid(AdvancedCategory, DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UQuickSDFToolProperties, UpscaleFactor)));
	AddPropertyIfValid(AdvancedCategory, DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UQuickSDFToolProperties, MaskExportFolder)));
	AddPropertyIfValid(AdvancedCategory, DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UQuickSDFToolProperties, bCreateMaskFolderPerExport)));
	AddPropertyIfValid(AdvancedCategory, DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UQuickSDFToolProperties, MaskExportFolderPrefix)));
	AddPropertyIfValid(AdvancedCategory, DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UQuickSDFToolProperties, MaskTextureNamePrefix)));
	AddPropertyIfValid(AdvancedCategory, DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UQuickSDFToolProperties, MaskFileNameMode)));
	AddPropertyIfValid(AdvancedCategory, DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UQuickSDFToolProperties, bOverwriteExistingMasks)));
	AddPropertyIfValid(AdvancedCategory, DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UQuickSDFToolProperties, EditAngleIndex)));
	AddPropertyIfValid(AdvancedCategory, DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UQuickSDFToolProperties, NumAngles)));

	AdvancedCategory.AddCustomRow(LOCTEXT("AdvancedActionsFilter", "Rebake Fill Generate SDF"))
	.WholeRowContent()
	[
		SNew(SVerticalBox)
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0.0f, 4.0f, 0.0f, 2.0f)
		[
			QuickSDFToolUI::MakeIconLabelButton(
				"QuickSDF.Action.Rebake",
				LOCTEXT("FillCurrentButton", "Rebake Current"),
				LOCTEXT("FillCurrentTooltip", "Rebake the original shading into the current mask"),
				FOnClicked::CreateLambda([WeakProperties]()
				{
					if (UQuickSDFToolProperties* Props = WeakProperties.Get())
					{
						Props->FillOriginalShadingToCurrentAngle();
					}
					return FReply::Handled();
				}))
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0.0f, 2.0f)
		[
			QuickSDFToolUI::MakeIconLabelButton(
				"QuickSDF.Action.Rebake",
				LOCTEXT("FillAllButton", "Rebake All"),
				LOCTEXT("FillAllTooltip", "Rebake original shading into all masks"),
				FOnClicked::CreateLambda([WeakProperties]()
				{
					if (UQuickSDFToolProperties* Props = WeakProperties.Get())
					{
						Props->FillOriginalShadingToAllAngles();
					}
					return FReply::Handled();
				}))
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0.0f, 2.0f)
		[
			QuickSDFToolUI::MakeIconLabelButton(
				"QuickSDF.Action.FillWhite",
				LOCTEXT("FillAllWhiteButton", "Fill All White"),
				LOCTEXT("FillAllWhiteTooltip", "Fill all masks with white"),
				FOnClicked::CreateLambda([WeakProperties]()
				{
					if (UQuickSDFToolProperties* Props = WeakProperties.Get())
					{
						Props->FillAllMasksWhite();
					}
					return FReply::Handled();
				}))
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0.0f, 2.0f)
		[
			QuickSDFToolUI::MakeIconLabelButton(
				"QuickSDF.Action.FillBlack",
				LOCTEXT("FillAllBlackButton", "Fill All Black"),
				LOCTEXT("FillAllBlackTooltip", "Fill all masks with black"),
				FOnClicked::CreateLambda([WeakProperties]()
				{
					if (UQuickSDFToolProperties* Props = WeakProperties.Get())
					{
						Props->FillAllMasksBlack();
					}
					return FReply::Handled();
				}))
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0.0f, 2.0f)
		[
			QuickSDFToolUI::MakeIconLabelButton(
				"QuickSDF.Action.CreateThresholdMap",
				LOCTEXT("GenerateAdvancedButton", "Generate SDF Threshold Map"),
				LOCTEXT("GenerateAdvancedTooltip", "Generate the SDF threshold texture"),
				FOnClicked::CreateLambda([WeakProperties]()
				{
					if (UQuickSDFToolProperties* Props = WeakProperties.Get())
					{
						Props->GenerateSDFThresholdMap();
					}
					return FReply::Handled();
				}))
		]
	];
}

#undef LOCTEXT_NAMESPACE
