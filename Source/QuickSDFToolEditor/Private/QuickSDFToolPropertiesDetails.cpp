#include "QuickSDFToolPropertiesDetails.h"

#include "Components/MeshComponent.h"
#include "DetailCategoryBuilder.h"
#include "DetailLayoutBuilder.h"
#include "DetailWidgetRow.h"
#include "Editor.h"
#include "GameFramework/Actor.h"
#include "PropertyHandle.h"
#include "QuickSDFAsset.h"
#include "QuickSDFToolProperties.h"
#include "QuickSDFToolSubsystem.h"
#include "Styling/AppStyle.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Layout/SBox.h"
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
	DetailBuilder.HideCategory(FName(TEXT("Export Settings")));
	DetailBuilder.HideCategory(FName(TEXT("Actions")));

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

	AddPropertyIfValid(QuickCategory, DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UQuickSDFToolProperties, QualityPreset)));

	QuickCategory.AddCustomRow(LOCTEXT("QuickActionsFilter", "Create Threshold Map Import Edited Masks Fill White Fill Black"))
	.WholeRowContent()
	[
		SNew(SVerticalBox)
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0.0f, 4.0f, 0.0f, 2.0f)
		[
			SNew(SButton)
			.Text(LOCTEXT("CreateThresholdMapButton", "Create Threshold Map"))
			.HAlign(HAlign_Center)
			.IsEnabled_Lambda([]()
			{
				return CanCreateThresholdMap();
			})
			.OnClicked_Lambda([WeakProperties]()
			{
				if (UQuickSDFToolProperties* Props = WeakProperties.Get())
				{
					Props->CreateQuickThresholdMap();
				}
				return FReply::Handled();
			})
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0.0f, 2.0f)
		[
			SNew(SButton)
			.Text(LOCTEXT("ImportEditedMasksButton", "Import Edited Masks"))
			.HAlign(HAlign_Center)
			.OnClicked_Lambda([WeakProperties]()
			{
				if (UQuickSDFToolProperties* Props = WeakProperties.Get())
				{
					Props->ImportEditedMasks();
				}
				return FReply::Handled();
			})
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
				SNew(SButton)
				.Text(LOCTEXT("FillCurrentWhiteButton", "Fill White"))
				.HAlign(HAlign_Center)
				.OnClicked_Lambda([WeakProperties]()
				{
					if (UQuickSDFToolProperties* Props = WeakProperties.Get())
					{
						Props->FillCurrentMaskWhite();
					}
					return FReply::Handled();
				})
			]
			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			.Padding(2.0f, 0.0f, 0.0f, 0.0f)
			[
				SNew(SButton)
				.Text(LOCTEXT("FillCurrentBlackButton", "Fill Black"))
				.HAlign(HAlign_Center)
				.OnClicked_Lambda([WeakProperties]()
				{
					if (UQuickSDFToolProperties* Props = WeakProperties.Get())
					{
						Props->FillCurrentMaskBlack();
					}
					return FReply::Handled();
				})
			]
		]
	];

	AddPropertyIfValid(MasksCategory, DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UQuickSDFToolProperties, TargetAsset)));
	AddPropertyIfValid(MasksCategory, DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UQuickSDFToolProperties, TargetTextures)));
	AddPropertyIfValid(MasksCategory, DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UQuickSDFToolProperties, QuickSDFAssetFolder)));
	AddPropertyIfValid(MasksCategory, DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UQuickSDFToolProperties, QuickSDFAssetName)));
	AddPropertyIfValid(MasksCategory, DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UQuickSDFToolProperties, bSaveMaskTexturesWithAsset)));
	MasksCategory.AddCustomRow(LOCTEXT("SaveQuickSDFAssetFilter", "Save QuickSDF Asset"))
	.WholeRowContent()
	[
		SNew(SButton)
		.Text(LOCTEXT("SaveQuickSDFAssetButton", "Save QuickSDF Asset"))
		.HAlign(HAlign_Center)
		.OnClicked_Lambda([WeakProperties]()
		{
			if (UQuickSDFToolProperties* Props = WeakProperties.Get())
			{
				Props->SaveQuickSDFAsset();
			}
			return FReply::Handled();
		})
	];

	AddPropertyIfValid(OutputCategory, DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UQuickSDFToolProperties, SDFOutputFolder)));
	AddPropertyIfValid(OutputCategory, DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UQuickSDFToolProperties, SDFTextureName)));
	AddPropertyIfValid(OutputCategory, DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UQuickSDFToolProperties, bOverwriteExistingSDF)));

	AddPropertyIfValid(AdvancedCategory, DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UQuickSDFToolProperties, ImportedMaskFolder)));
	AddPropertyIfValid(AdvancedCategory, DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UQuickSDFToolProperties, TargetAngles)));
	AddPropertyIfValid(AdvancedCategory, DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UQuickSDFToolProperties, Resolution)));
	AddPropertyIfValid(AdvancedCategory, DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UQuickSDFToolProperties, UVChannel)));
	AddPropertyIfValid(AdvancedCategory, DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UQuickSDFToolProperties, bShowPreview)));
	AddPropertyIfValid(AdvancedCategory, DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UQuickSDFToolProperties, bOverlayOriginalShadow)));
	AddPropertyIfValid(AdvancedCategory, DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UQuickSDFToolProperties, bOverlayUV)));
	AddPropertyIfValid(AdvancedCategory, DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UQuickSDFToolProperties, bAutoSyncLight)));
	AddPropertyIfValid(AdvancedCategory, DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UQuickSDFToolProperties, bPaintAllAngles)));
	AddPropertyIfValid(AdvancedCategory, DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UQuickSDFToolProperties, bEnableQuickLine)));
	AddPropertyIfValid(AdvancedCategory, DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UQuickSDFToolProperties, QuickLineHoldTime)));
	AddPropertyIfValid(AdvancedCategory, DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UQuickSDFToolProperties, QuickLineMoveTolerance)));
	AddPropertyIfValid(AdvancedCategory, DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UQuickSDFToolProperties, bEnableOnionSkin)));
	AddPropertyIfValid(AdvancedCategory, DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UQuickSDFToolProperties, bSymmetryMode)));
	AddPropertyIfValid(AdvancedCategory, DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UQuickSDFToolProperties, TargetMaterialSlot)));
	AddPropertyIfValid(AdvancedCategory, DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UQuickSDFToolProperties, bIsolateTargetMaterialSlot)));
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
			SNew(SButton)
			.Text(LOCTEXT("FillCurrentButton", "Rebake Current"))
			.OnClicked_Lambda([WeakProperties]()
			{
				if (UQuickSDFToolProperties* Props = WeakProperties.Get())
				{
					Props->FillOriginalShadingToCurrentAngle();
				}
				return FReply::Handled();
			})
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0.0f, 2.0f)
		[
			SNew(SButton)
			.Text(LOCTEXT("FillAllButton", "Rebake All"))
			.OnClicked_Lambda([WeakProperties]()
			{
				if (UQuickSDFToolProperties* Props = WeakProperties.Get())
				{
					Props->FillOriginalShadingToAllAngles();
				}
				return FReply::Handled();
			})
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0.0f, 2.0f)
		[
			SNew(SButton)
			.Text(LOCTEXT("FillAllWhiteButton", "Fill All White"))
			.OnClicked_Lambda([WeakProperties]()
			{
				if (UQuickSDFToolProperties* Props = WeakProperties.Get())
				{
					Props->FillAllMasksWhite();
				}
				return FReply::Handled();
			})
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0.0f, 2.0f)
		[
			SNew(SButton)
			.Text(LOCTEXT("FillAllBlackButton", "Fill All Black"))
			.OnClicked_Lambda([WeakProperties]()
			{
				if (UQuickSDFToolProperties* Props = WeakProperties.Get())
				{
					Props->FillAllMasksBlack();
				}
				return FReply::Handled();
			})
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0.0f, 2.0f)
		[
			SNew(SButton)
			.Text(LOCTEXT("GenerateAdvancedButton", "Generate SDF Threshold Map"))
			.OnClicked_Lambda([WeakProperties]()
			{
				if (UQuickSDFToolProperties* Props = WeakProperties.Get())
				{
					Props->GenerateSDFThresholdMap();
				}
				return FReply::Handled();
			})
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0.0f, 2.0f)
		[
			SNew(SButton)
			.Text(LOCTEXT("ExportMasksButton", "Export Mask Textures"))
			.OnClicked_Lambda([WeakProperties]()
			{
				if (UQuickSDFToolProperties* Props = WeakProperties.Get())
				{
					Props->ExportToTexture();
				}
				return FReply::Handled();
			})
		]
	];
}

#undef LOCTEXT_NAMESPACE
