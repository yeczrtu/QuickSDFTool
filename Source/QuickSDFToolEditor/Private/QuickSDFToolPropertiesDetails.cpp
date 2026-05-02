#include "QuickSDFToolPropertiesDetails.h"

#include "Components/MeshComponent.h"
#include "DetailCategoryBuilder.h"
#include "DetailLayoutBuilder.h"
#include "DetailWidgetRow.h"
#include "Editor.h"
#include "GameFramework/Actor.h"
#include "PropertyHandle.h"
#include "QuickSDFAsset.h"
#include "QuickSDFPaintTool.h"
#include "QuickSDFToolProperties.h"
#include "QuickSDFToolSubsystem.h"
#include "QuickSDFToolUI.h"
#include "Styling/AppStyle.h"
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

	for (const FQuickSDFAngleData& AngleData : Asset->AngleDataList)
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

FText GetTextureSetSlotText(const FQuickSDFTextureSetData& TextureSet)
{
	return FText::Format(
		LOCTEXT("TextureSetSlotFormat", "{0}: {1}"),
		FText::AsNumber(TextureSet.MaterialSlotIndex),
		FText::FromName(TextureSet.SlotName));
}

TSharedRef<SWidget> MakeTextureSetRow(TWeakObjectPtr<UQuickSDFToolProperties> WeakProperties, int32 TextureSetIndex)
{
	return SNew(SBorder)
		.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
		.BorderBackgroundColor_Lambda([TextureSetIndex]()
		{
			const UQuickSDFPaintTool* Tool = GetActiveQuickSDFPaintTool();
			const UQuickSDFToolProperties* Props = Tool ? Tool->Properties : nullptr;
			return Props && Props->ActiveTextureSetIndex == TextureSetIndex
				? FLinearColor(0.08f, 0.16f, 0.22f, 1.0f)
				: FLinearColor(0.025f, 0.025f, 0.025f, 1.0f);
		})
		.Padding(5.0f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.FillWidth(0.38f)
			.VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Text_Lambda([TextureSetIndex]()
				{
					const UQuickSDFToolSubsystem* Subsystem = GEditor ? GEditor->GetEditorSubsystem<UQuickSDFToolSubsystem>() : nullptr;
					const UQuickSDFAsset* Asset = Subsystem ? Subsystem->GetActiveSDFAsset() : nullptr;
					return Asset && Asset->TextureSets.IsValidIndex(TextureSetIndex)
						? GetTextureSetSlotText(Asset->TextureSets[TextureSetIndex])
						: LOCTEXT("MissingTextureSetRow", "Missing");
				})
				.Font(FAppStyle::GetFontStyle("PropertyWindow.NormalFont"))
			]
			+ SHorizontalBox::Slot()
			.FillWidth(0.34f)
			.VAlign(VAlign_Center)
			.Padding(4.0f, 0.0f)
			[
				SNew(STextBlock)
				.Text_Lambda([TextureSetIndex]()
				{
					const UQuickSDFToolSubsystem* Subsystem = GEditor ? GEditor->GetEditorSubsystem<UQuickSDFToolSubsystem>() : nullptr;
					const UQuickSDFAsset* Asset = Subsystem ? Subsystem->GetActiveSDFAsset() : nullptr;
					return Asset && Asset->TextureSets.IsValidIndex(TextureSetIndex)
						? FText::FromString(Asset->TextureSets[TextureSetIndex].MaterialName)
						: FText::GetEmpty();
				})
				.Font(FAppStyle::GetFontStyle("SmallFont"))
				.ColorAndOpacity(FLinearColor(0.72f, 0.72f, 0.72f, 1.0f))
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(3.0f, 0.0f)
			[
				SNew(SBorder)
				.BorderImage(FAppStyle::GetBrush("WhiteBrush"))
				.BorderBackgroundColor_Lambda([TextureSetIndex]()
				{
					const UQuickSDFPaintTool* Tool = GetActiveQuickSDFPaintTool();
					return Tool ? Tool->GetTextureSetStatusColor(TextureSetIndex) : FLinearColor(0.45f, 0.45f, 0.45f, 1.0f);
				})
				.Padding(FMargin(6.0f, 2.0f))
				[
					SNew(STextBlock)
					.Text_Lambda([TextureSetIndex]()
					{
						const UQuickSDFPaintTool* Tool = GetActiveQuickSDFPaintTool();
						return Tool ? Tool->GetTextureSetStatusText(TextureSetIndex) : LOCTEXT("TextureSetNoTool", "Idle");
					})
					.Font(FAppStyle::GetFontStyle("SmallFont"))
					.ColorAndOpacity(FLinearColor::Black)
				]
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(2.0f, 0.0f)
			[
				SNew(SButton)
				.Text(LOCTEXT("SelectTextureSetButton", "Select"))
				.ToolTipText(LOCTEXT("SelectTextureSetTooltip", "Make this material slot the active Texture Set for painting, baking, preview, and SDF generation."))
				.OnClicked_Lambda([TextureSetIndex]()
				{
					if (UQuickSDFPaintTool* Tool = GetActiveQuickSDFPaintTool())
					{
						Tool->SelectTextureSet(TextureSetIndex);
					}
					return FReply::Handled();
				})
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(2.0f, 0.0f)
			[
				SNew(SButton)
				.Text(LOCTEXT("BakeTextureSetButton", "Bake"))
				.ToolTipText(LOCTEXT("BakeTextureSetTooltip", "Select this Texture Set and bake only its material slot."))
				.OnClicked_Lambda([TextureSetIndex]()
				{
					if (UQuickSDFPaintTool* Tool = GetActiveQuickSDFPaintTool())
					{
						Tool->SelectTextureSet(TextureSetIndex);
						Tool->BakeSelectedTextureSet();
					}
					return FReply::Handled();
				})
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

	return SNew(SVerticalBox)
		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			Rows
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0.0f, 5.0f, 0.0f, 0.0f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			.Padding(0.0f, 0.0f, 2.0f, 0.0f)
			[
				QuickSDFToolUI::MakeIconLabelButton(
					"QuickSDF.Action.Rebake",
					LOCTEXT("BakeSelectedSetButton", "Bake Selected"),
					LOCTEXT("BakeSelectedSetTooltip", "Bake only the active Texture Set."),
					FOnClicked::CreateLambda([WeakProperties]()
					{
						if (UQuickSDFToolProperties* Props = WeakProperties.Get())
						{
							Props->BakeSelectedTextureSet();
						}
						return FReply::Handled();
					}))
			]
			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			.Padding(2.0f, 0.0f)
			[
				QuickSDFToolUI::MakeIconLabelButton(
					"QuickSDF.Action.CompleteToEight",
					LOCTEXT("BakeMissingSetsButton", "Bake Missing"),
					LOCTEXT("BakeMissingSetsTooltip", "Bake every Texture Set that is still empty."),
					FOnClicked::CreateLambda([WeakProperties]()
					{
						if (UQuickSDFToolProperties* Props = WeakProperties.Get())
						{
							Props->BakeMissingTextureSets();
						}
						return FReply::Handled();
					}))
			]
			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			.Padding(2.0f, 0.0f, 0.0f, 0.0f)
			[
				QuickSDFToolUI::MakeIconLabelButton(
					"QuickSDF.Action.CreateThresholdMap",
					LOCTEXT("GenerateAllBakedSetsButton", "Generate All"),
					LOCTEXT("GenerateAllBakedSetsTooltip", "Generate SDF threshold textures for every baked Texture Set."),
					FOnClicked::CreateLambda([WeakProperties]()
					{
						if (UQuickSDFToolProperties* Props = WeakProperties.Get())
						{
							Props->GenerateAllBakedTextureSets();
						}
						return FReply::Handled();
					}))
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

	QuickCategory.AddCustomRow(LOCTEXT("TextureSetsFilter", "Texture Sets Material Slots Bake Selected Bake Missing"))
	.WholeRowContent()
	[
		SNew(SVerticalBox)
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0.0f, 4.0f, 0.0f, 2.0f)
		[
			SNew(STextBlock)
			.Text(LOCTEXT("TextureSetsLabel", "Texture Sets"))
			.Font(FAppStyle::GetFontStyle("PropertyWindow.BoldFont"))
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			MakeTextureSetList(WeakProperties)
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

	QuickCategory.AddCustomRow(LOCTEXT("QuickActionsFilter", "Create Threshold Map Import Mask Assets Overwrite Source Textures Fill White Fill Black"))
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
			QuickSDFToolUI::MakeIconLabelButton(
				"QuickSDF.Action.ImportMasks",
				LOCTEXT("ImportEditedMasksButton", "Import Mask Assets"),
				LOCTEXT("ImportEditedMasksTooltip", "Assign selected Content Browser Texture2D assets to timeline mask slots"),
				FOnClicked::CreateLambda([WeakProperties]()
				{
					if (UQuickSDFToolProperties* Props = WeakProperties.Get())
					{
						Props->ImportEditedMasks();
					}
					return FReply::Handled();
				}))
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0.0f, 2.0f)
		[
			QuickSDFToolUI::MakeIconLabelButton(
				"QuickSDF.Action.ExportMasks",
				LOCTEXT("OverwriteSourceTexturesButton", "Overwrite Source Textures"),
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

	AddPropertyIfValid(AdvancedCategory, DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UQuickSDFToolProperties, Resolution)));
	AddPropertyIfValid(AdvancedCategory, DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UQuickSDFToolProperties, UVChannel)));
	AddPropertyIfValid(AdvancedCategory, DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UQuickSDFToolProperties, bShowPreview)));
	AddPropertyIfValid(AdvancedCategory, DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UQuickSDFToolProperties, bOverlayOriginalShadow)));
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
	AddPropertyIfValid(AdvancedCategory, DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UQuickSDFToolProperties, bSymmetryMode)));
	AddPropertyIfValid(AdvancedCategory, DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UQuickSDFToolProperties, UpscaleFactor)));
	AddPropertyIfValid(AdvancedCategory, DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UQuickSDFToolProperties, MaskExportFolder)));
	AddPropertyIfValid(AdvancedCategory, DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UQuickSDFToolProperties, bCreateMaskFolderPerExport)));
	AddPropertyIfValid(AdvancedCategory, DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UQuickSDFToolProperties, MaskExportFolderPrefix)));
	AddPropertyIfValid(AdvancedCategory, DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UQuickSDFToolProperties, MaskTextureNamePrefix)));
	AddPropertyIfValid(AdvancedCategory, DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UQuickSDFToolProperties, bOverwriteExistingMasks)));
	AddPropertyIfValid(AdvancedCategory, DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UQuickSDFToolProperties, EditAngleIndex)));
	AddPropertyIfValid(AdvancedCategory, DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UQuickSDFToolProperties, NumAngles)));

	AdvancedCategory.AddCustomRow(LOCTEXT("AdvancedActionsFilter", "Rebake Fill Export Masks Generate SDF"))
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
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0.0f, 2.0f)
		[
			QuickSDFToolUI::MakeIconLabelButton(
				"QuickSDF.Action.ExportMasks",
				LOCTEXT("ExportMasksButton", "Export Mask Textures"),
				LOCTEXT("ExportMasksTooltip", "Export the edited masks as textures"),
				FOnClicked::CreateLambda([WeakProperties]()
				{
					if (UQuickSDFToolProperties* Props = WeakProperties.Get())
					{
						Props->ExportToTexture();
					}
					return FReply::Handled();
				}))
		]
	];
}

#undef LOCTEXT_NAMESPACE
