#include "SQuickSDFMaskImportPanel.h"

#include "AssetRegistry/AssetData.h"
#include "AssetToolsModule.h"
#include "ContentBrowserModule.h"
#include "DragAndDrop/AssetDragDropOp.h"
#include "DesktopPlatformModule.h"
#include "Editor.h"
#include "Engine/Texture2D.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Notifications/NotificationManager.h"
#include "IAssetTools.h"
#include "IContentBrowserSingleton.h"
#include "IDesktopPlatform.h"
#include "IImageWrapper.h"
#include "IImageWrapperModule.h"
#include "Misc/FileHelper.h"
#include "Misc/MessageDialog.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "ObjectTools.h"
#include "QuickSDFPaintTool.h"
#include "QuickSDFPaintToolPrivate.h"
#include "QuickSDFToolProperties.h"
#include "QuickSDFToolStyle.h"
#include "QuickSDFToolSubsystem.h"
#include "Styling/AppStyle.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SNumericEntryBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "Widgets/Views/SHeaderRow.h"
#include "Widgets/Views/STableRow.h"

#define LOCTEXT_NAMESPACE "SQuickSDFMaskImportPanel"

namespace
{
constexpr float QuickSDFImportPanelWidth = 800.0f;
constexpr float QuickSDFImportPanelListHeight = 210.0f;

bool IsSupportedImageFile(const FString& Filename)
{
	const FString Extension = FPaths::GetExtension(Filename).ToLower();
	return Extension == TEXT("png") ||
		Extension == TEXT("jpg") ||
		Extension == TEXT("jpeg") ||
		Extension == TEXT("bmp") ||
		Extension == TEXT("tga") ||
		Extension == TEXT("exr");
}

bool ReadImageDimensions(const FString& Filename, int32& OutWidth, int32& OutHeight)
{
	TArray<uint8> CompressedData;
	if (!FFileHelper::LoadFileToArray(CompressedData, *Filename))
	{
		return false;
	}

	IImageWrapperModule& ImageWrapperModule = FModuleManager::LoadModuleChecked<IImageWrapperModule>("ImageWrapper");
	const EImageFormat ImageFormat = ImageWrapperModule.DetectImageFormat(CompressedData.GetData(), CompressedData.Num());
	if (ImageFormat == EImageFormat::Invalid)
	{
		return false;
	}

	TSharedPtr<IImageWrapper> ImageWrapper = ImageWrapperModule.CreateImageWrapper(ImageFormat, *Filename);
	if (!ImageWrapper.IsValid() || !ImageWrapper->SetCompressed(CompressedData.GetData(), CompressedData.Num()))
	{
		return false;
	}

	OutWidth = static_cast<int32>(ImageWrapper->GetWidth());
	OutHeight = static_cast<int32>(ImageWrapper->GetHeight());
	return OutWidth > 0 && OutHeight > 0;
}

FString CleanContentFolder(FString Folder)
{
	while (Folder.EndsWith(TEXT("/")))
	{
		Folder.LeftChopInline(1);
	}
	return Folder.IsEmpty() ? FString(TEXT("/Game/QuickSDF_Imports")) : Folder;
}

FString GetSourceName(const FQuickSDFMaskImportSource& Source)
{
	if (!Source.DisplayName.IsEmpty())
	{
		return Source.DisplayName;
	}

	if (Source.bExternalFile)
	{
		return FPaths::GetBaseFilename(Source.SourcePath);
	}

	return Source.Texture.IsValid() ? Source.Texture->GetName() : FString(TEXT("Missing mask"));
}

FString GetSourceKey(const FQuickSDFMaskImportSource& Source)
{
	return Source.bExternalFile ? Source.SourcePath : (Source.Texture.IsValid() ? Source.Texture->GetPathName() : GetSourceName(Source));
}

bool DoesAssetPathExist(const FString& AssetPath)
{
	return StaticLoadObject(UObject::StaticClass(), nullptr, *AssetPath) != nullptr;
}

FString BuildExternalDestination(const FQuickSDFMaskImportSource& Source, const FString& FolderPath, TSet<FString>* ReservedDestinations = nullptr)
{
	const FString TextureName = ObjectTools::SanitizeObjectName(FPaths::GetBaseFilename(Source.SourcePath));
	if (TextureName.IsEmpty())
	{
		return FolderPath / TEXT("T_QuickSDF_Mask");
	}

	if (!FPackageName::IsValidLongPackageName(FolderPath))
	{
		return FolderPath / TextureName;
	}

	IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();
	FString UniquePackageName;
	FString UniqueAssetName;
	AssetTools.CreateUniqueAssetName(FolderPath / TextureName, TEXT(""), UniquePackageName, UniqueAssetName);
	const FString UniqueFolder = FPackageName::GetLongPackagePath(UniquePackageName);
	FString CandidatePath = UniqueFolder / UniqueAssetName;
	if (!ReservedDestinations)
	{
		return CandidatePath;
	}

	if (!ReservedDestinations->Contains(CandidatePath))
	{
		ReservedDestinations->Add(CandidatePath);
		return CandidatePath;
	}

	for (int32 Suffix = 1; Suffix < TNumericLimits<int32>::Max(); ++Suffix)
	{
		CandidatePath = UniqueFolder / FString::Printf(TEXT("%s_%d"), *TextureName, Suffix);
		if (!ReservedDestinations->Contains(CandidatePath) && !DoesAssetPathExist(CandidatePath))
		{
			ReservedDestinations->Add(CandidatePath);
			return CandidatePath;
		}
	}

	return UniqueFolder / UniqueAssetName;
}

FText FormatSize(int32 Width, int32 Height)
{
	return (Width > 0 && Height > 0)
		? FText::Format(LOCTEXT("SizeFormat", "{0} x {1}"), FText::AsNumber(Width), FText::AsNumber(Height))
		: LOCTEXT("UnknownSize", "Unknown");
}

TSharedRef<SWidget> MakeSmallButton(const FText& Label, const FText& ToolTip, FOnClicked OnClicked)
{
	return SNew(SButton)
		.ToolTipText(ToolTip)
		.OnClicked(OnClicked)
		.ContentPadding(FMargin(7.0f, 3.0f))
		[
			SNew(STextBlock)
			.Text(Label)
			.Font(FAppStyle::GetFontStyle("SmallFont"))
		];
}
}

struct FQuickSDFMaskImportRowData
{
	FQuickSDFMaskImportSource Source;
	bool bHasSource = false;
	bool bAngleFromName = false;
	bool bAngleInferred = false;
	int32 SlotIndex = 0;
	float Angle = 0.0f;
	int32 Width = 0;
	int32 Height = 0;
	FString Destination;
	FText WarningText;
};

class SQuickSDFMaskImportRow : public SMultiColumnTableRow<TSharedPtr<FQuickSDFMaskImportRowData>>
{
public:
	SLATE_BEGIN_ARGS(SQuickSDFMaskImportRow) {}
		SLATE_ARGUMENT(TSharedPtr<FQuickSDFMaskImportRowData>, Item)
		SLATE_ARGUMENT(TWeakPtr<SQuickSDFMaskImportPanel>, Panel)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, const TSharedRef<STableViewBase>& OwnerTable)
	{
		Item = InArgs._Item;
		Panel = InArgs._Panel;
		SMultiColumnTableRow<TSharedPtr<FQuickSDFMaskImportRowData>>::Construct(
			FSuperRowType::FArguments().Padding(FMargin(0.0f, 1.0f)),
			OwnerTable);
	}

	virtual TSharedRef<SWidget> GenerateWidgetForColumn(const FName& ColumnName) override
	{
		if (!Item.IsValid())
		{
			return SNew(STextBlock);
		}

		const FSlateColor MutedColor = FSlateColor(FLinearColor(0.58f, 0.58f, 0.58f, 1.0f));
		const FSlateColor WarningColor = FSlateColor(FLinearColor(1.0f, 0.74f, 0.32f, 1.0f));

		if (ColumnName == TEXT("Slot"))
		{
			return SNew(STextBlock)
				.Text(FText::AsNumber(Item->SlotIndex + 1))
				.ColorAndOpacity(Item->bHasSource ? FSlateColor::UseForeground() : MutedColor)
				.Font(FAppStyle::GetFontStyle("SmallFont"));
		}

		if (ColumnName == TEXT("Source"))
		{
			return SNew(STextBlock)
				.Text(Item->bHasSource ? FText::FromString(GetSourceName(Item->Source)) : LOCTEXT("MissingMaskSource", "Missing mask"))
				.ToolTipText(Item->bHasSource ? FText::FromString(GetSourceKey(Item->Source)) : LOCTEXT("MissingMaskTooltip", "This expected slot has no source texture."))
				.ColorAndOpacity(Item->bHasSource ? FSlateColor::UseForeground() : MutedColor)
				.Font(FAppStyle::GetFontStyle("SmallFont"));
		}

		if (ColumnName == TEXT("Angle"))
		{
			return SNew(SNumericEntryBox<float>)
				.AllowSpin(true)
				.MinValue(0.0f)
				.MaxValue(180.0f)
				.MinSliderValue(0.0f)
				.MaxSliderValue(180.0f)
				.Value_Lambda([Item = Item]() -> TOptional<float>
				{
					return Item.IsValid() ? TOptional<float>(Item->Angle) : TOptional<float>();
				})
				.OnValueChanged_Lambda([Item = Item, Panel = Panel](float NewValue)
				{
					if (Item.IsValid())
					{
						Item->Angle = FMath::Clamp(NewValue, 0.0f, 180.0f);
						Item->bAngleInferred = false;
						if (TSharedPtr<SQuickSDFMaskImportPanel> PinnedPanel = Panel.Pin())
						{
							PinnedPanel->RefreshValidation();
						}
					}
				})
				.Font(FAppStyle::GetFontStyle("SmallFont"));
		}

		if (ColumnName == TEXT("Size"))
		{
			return SNew(STextBlock)
				.Text(FormatSize(Item->Width, Item->Height))
				.ColorAndOpacity(Item->bHasSource ? FSlateColor::UseForeground() : MutedColor)
				.Font(FAppStyle::GetFontStyle("SmallFont"));
		}

		if (ColumnName == TEXT("Destination"))
		{
			return SNew(STextBlock)
				.Text(Item->bHasSource ? FText::FromString(Item->Destination) : FText::GetEmpty())
				.ToolTipText(Item->bHasSource ? FText::FromString(Item->Destination) : FText::GetEmpty())
				.ColorAndOpacity(MutedColor)
				.Font(FAppStyle::GetFontStyle("SmallFont"));
		}

		if (ColumnName == TEXT("Warning"))
		{
			return SNew(STextBlock)
				.Text(Item->WarningText)
				.ToolTipText(Item->WarningText)
				.ColorAndOpacity(WarningColor)
				.Font(FAppStyle::GetFontStyle("SmallFont"));
		}

		return SNew(STextBlock);
	}

private:
	TSharedPtr<FQuickSDFMaskImportRowData> Item;
	TWeakPtr<SQuickSDFMaskImportPanel> Panel;
};

void SQuickSDFMaskImportPanel::Construct(const FArguments& InArgs)
{
	PaintTool = InArgs._PaintTool;
	Sources = InArgs._Sources;
	OnClosed = InArgs._OnClosed;
	RebuildRows();

	ChildSlot
	[
		SNew(SBox)
		.WidthOverride(QuickSDFImportPanelWidth)
		[
			SNew(SBorder)
			.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
			.Padding(FMargin(8.0f))
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot()
				.AutoHeight()
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					[
						SNew(SImage)
						.Image(FQuickSDFToolStyle::GetBrush("QuickSDF.Action.ImportMasks"))
					]
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					.Padding(6.0f, 0.0f, 0.0f, 0.0f)
					[
						SNew(STextBlock)
						.Text(LOCTEXT("PanelTitle", "Import Masks"))
						.Font(FAppStyle::GetFontStyle("PropertyWindow.BoldFont"))
					]
					+ SHorizontalBox::Slot()
					.FillWidth(1.0f)
					.VAlign(VAlign_Center)
					.Padding(8.0f, 0.0f)
					[
						SNew(STextBlock)
						.Text(this, &SQuickSDFMaskImportPanel::GetSummaryText)
						.ColorAndOpacity(FLinearColor(0.68f, 0.68f, 0.68f, 1.0f))
						.Font(FAppStyle::GetFontStyle("SmallFont"))
					]
				]

				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0.0f, 6.0f, 0.0f, 4.0f)
				[
					SNew(STextBlock)
					.Text(this, &SQuickSDFMaskImportPanel::GetImportedFolderText)
					.ColorAndOpacity(FLinearColor(0.58f, 0.58f, 0.58f, 1.0f))
					.Font(FAppStyle::GetFontStyle("SmallFont"))
				]

				+ SVerticalBox::Slot()
				.AutoHeight()
				[
					SNew(SBox)
					.HeightOverride(QuickSDFImportPanelListHeight)
					[
						SAssignNew(RowListView, SListView<TSharedPtr<FQuickSDFMaskImportRowData>>)
						.ListItemsSource(&Rows)
						.SelectionMode(ESelectionMode::None)
						.OnGenerateRow(this, &SQuickSDFMaskImportPanel::GenerateRow)
						.HeaderRow
						(
							SNew(SHeaderRow)
							+ SHeaderRow::Column(TEXT("Slot")).FixedWidth(38.0f).DefaultLabel(LOCTEXT("SlotColumn", "#"))
							+ SHeaderRow::Column(TEXT("Source")).FillWidth(0.9f).DefaultLabel(LOCTEXT("SourceColumn", "Source"))
							+ SHeaderRow::Column(TEXT("Angle")).FixedWidth(112.0f).DefaultLabel(LOCTEXT("AngleColumn", "Angle"))
							+ SHeaderRow::Column(TEXT("Size")).FixedWidth(96.0f).DefaultLabel(LOCTEXT("SizeColumn", "Size"))
							+ SHeaderRow::Column(TEXT("Destination")).FillWidth(1.0f).DefaultLabel(LOCTEXT("DestinationColumn", "Destination"))
							+ SHeaderRow::Column(TEXT("Warning")).FillWidth(1.0f).DefaultLabel(LOCTEXT("WarningColumn", "Warning"))
						)
					]
				]

				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0.0f, 8.0f, 0.0f, 0.0f)
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().AutoWidth().Padding(0.0f, 0.0f, 4.0f, 0.0f)
					[
						MakeSmallButton(LOCTEXT("AddFiles", "Add Files"), LOCTEXT("AddFilesTooltip", "Add external image files to the import preview."), FOnClicked::CreateSP(this, &SQuickSDFMaskImportPanel::OnAddFilesClicked))
					]
					+ SHorizontalBox::Slot().AutoWidth().Padding(0.0f, 0.0f, 4.0f, 0.0f)
					[
						MakeSmallButton(LOCTEXT("AddAssets", "Add Assets"), LOCTEXT("AddAssetsTooltip", "Pick Texture2D assets from the Content Browser."), FOnClicked::CreateSP(this, &SQuickSDFMaskImportPanel::OnAddAssetsClicked))
					]
					+ SHorizontalBox::Slot().AutoWidth().Padding(0.0f, 0.0f, 4.0f, 0.0f)
					[
						MakeSmallButton(LOCTEXT("Complete", "Complete"), LOCTEXT("CompleteTooltip", "Show the default expected mask slots for the current symmetry mode."), FOnClicked::CreateSP(this, &SQuickSDFMaskImportPanel::OnCompleteClicked))
					]
					+ SHorizontalBox::Slot().AutoWidth()
					[
						MakeSmallButton(LOCTEXT("Even", "Even"), LOCTEXT("EvenTooltip", "Redistribute source angles evenly."), FOnClicked::CreateSP(this, &SQuickSDFMaskImportPanel::OnEvenClicked))
					]
					+ SHorizontalBox::Slot().FillWidth(1.0f)
					[
						SNew(SSpacer)
					]
					+ SHorizontalBox::Slot().AutoWidth().Padding(0.0f, 0.0f, 4.0f, 0.0f)
					[
						SNew(SButton)
						.IsEnabled(this, &SQuickSDFMaskImportPanel::HasImportableRows)
						.ToolTipText(this, &SQuickSDFMaskImportPanel::GetApplyToolTipText)
						.OnClicked(this, &SQuickSDFMaskImportPanel::OnApplyClicked)
						.ContentPadding(FMargin(9.0f, 3.0f))
						[
							SNew(STextBlock)
							.Text(LOCTEXT("ApplyImport", "Apply Import"))
							.Font(FAppStyle::GetFontStyle("PropertyWindow.BoldFont"))
						]
					]
					+ SHorizontalBox::Slot().AutoWidth()
					[
						MakeSmallButton(LOCTEXT("Cancel", "Cancel"), LOCTEXT("CancelTooltip", "Close this preview without changing masks or creating assets."), FOnClicked::CreateSP(this, &SQuickSDFMaskImportPanel::OnCancelClicked))
					]
				]
			]
		]
	];
}

TSharedRef<ITableRow> SQuickSDFMaskImportPanel::GenerateRow(TSharedPtr<FQuickSDFMaskImportRowData> Item, const TSharedRef<STableViewBase>& OwnerTable)
{
	return SNew(SQuickSDFMaskImportRow, OwnerTable)
		.Item(Item)
		.Panel(SharedThis(this));
}

void SQuickSDFMaskImportPanel::AddSources(const TArray<FQuickSDFMaskImportSource>& NewSources)
{
	Sources.Append(NewSources);
	RebuildRows();
	if (RowListView.IsValid())
	{
		RowListView->RequestListRefresh();
	}
}

void SQuickSDFMaskImportPanel::RebuildRows()
{
	Rows.Reset();

	UQuickSDFPaintTool* Tool = PaintTool.Get();
	UQuickSDFToolProperties* Properties = Tool ? Tool->Properties.Get() : nullptr;
	const bool bBaseSymmetry = !Properties || Properties->bSymmetryMode;

	bool bNeedsFullRange = false;
	TArray<TSharedPtr<FQuickSDFMaskImportRowData>> SourceRows;
	SourceRows.Reserve(Sources.Num());

	for (const FQuickSDFMaskImportSource& Source : Sources)
	{
		if (!Source.bExternalFile && !Source.Texture.IsValid())
		{
			continue;
		}

		TSharedPtr<FQuickSDFMaskImportRowData> Row = MakeShared<FQuickSDFMaskImportRowData>();
		Row->Source = Source;
		Row->bHasSource = true;
		Row->Width = Source.Width;
		Row->Height = Source.Height;
		if (Source.bExternalFile && (Row->Width <= 0 || Row->Height <= 0))
		{
			ReadImageDimensions(Source.SourcePath, Row->Width, Row->Height);
		}
		Row->bAngleFromName = QuickSDFPaintToolPrivate::TryExtractAngleFromName(GetSourceName(Source), Row->Angle);
		Row->bAngleInferred = !Row->bAngleFromName;
		bNeedsFullRange |= Row->bAngleFromName && Row->Angle > 90.01f;
		SourceRows.Add(Row);
	}

	const float MaxAngle = (bBaseSymmetry && !bNeedsFullRange) ? 90.0f : 180.0f;
	int32 AutoIndex = 0;
	int32 AutoCount = 0;
	for (const TSharedPtr<FQuickSDFMaskImportRowData>& Row : SourceRows)
	{
		if (Row.IsValid() && Row->bAngleInferred)
		{
			++AutoCount;
		}
	}

	for (TSharedPtr<FQuickSDFMaskImportRowData>& Row : SourceRows)
	{
		if (!Row.IsValid())
		{
			continue;
		}

		if (Row->bAngleInferred)
		{
			Row->Angle = AutoCount > 1
				? (static_cast<float>(AutoIndex) / static_cast<float>(AutoCount - 1)) * MaxAngle
				: 0.0f;
			++AutoIndex;
		}
		Row->Angle = FMath::Clamp(Row->Angle, 0.0f, MaxAngle);
	}

	SourceRows.Sort([](const TSharedPtr<FQuickSDFMaskImportRowData>& A, const TSharedPtr<FQuickSDFMaskImportRowData>& B)
	{
		if (!A.IsValid() || !B.IsValid())
		{
			return A.IsValid();
		}
		if (!FMath::IsNearlyEqual(A->Angle, B->Angle))
		{
			return A->Angle < B->Angle;
		}
		return GetSourceName(A->Source) < GetSourceName(B->Source);
	});

	bool bHasAssignedMasks = false;
	if (Properties)
	{
		for (const UTexture2D* Texture : Properties->TargetTextures)
		{
			if (Texture)
			{
				bHasAssignedMasks = true;
				break;
			}
		}
	}

	const int32 DefaultCount = QuickSDFPaintToolPrivate::GetQuickSDFDefaultAngleCount(bBaseSymmetry && !bNeedsFullRange);
	const int32 CurrentCount = Properties && Properties->NumAngles > 0 && bHasAssignedMasks ? Properties->NumAngles : DefaultCount;
	const int32 ExpectedCount = FMath::Max(ExpectedCountOverride != INDEX_NONE ? ExpectedCountOverride : CurrentCount, SourceRows.Num());

	Rows = MoveTemp(SourceRows);
	const int32 MissingCount = ExpectedCount - Rows.Num();
	int32 AddedMissingCount = 0;
	for (int32 Index = 0; Index < ExpectedCount && AddedMissingCount < MissingCount; ++Index)
	{
		const float ExpectedAngle = ExpectedCount > 1
			? (static_cast<float>(Index) / static_cast<float>(ExpectedCount - 1)) * MaxAngle
			: 0.0f;
		const bool bExpectedAngleOccupied = Rows.ContainsByPredicate([ExpectedAngle](const TSharedPtr<FQuickSDFMaskImportRowData>& Row)
		{
			return Row.IsValid() && Row->bHasSource && FMath::IsNearlyEqual(Row->Angle, ExpectedAngle, 0.05f);
		});
		if (bExpectedAngleOccupied)
		{
			continue;
		}

		TSharedPtr<FQuickSDFMaskImportRowData> MissingRow = MakeShared<FQuickSDFMaskImportRowData>();
		MissingRow->bHasSource = false;
		MissingRow->Angle = ExpectedAngle;
		Rows.Add(MissingRow);
		++AddedMissingCount;
	}

	Rows.Sort([](const TSharedPtr<FQuickSDFMaskImportRowData>& A, const TSharedPtr<FQuickSDFMaskImportRowData>& B)
	{
		if (!A.IsValid() || !B.IsValid())
		{
			return A.IsValid();
		}
		if (!FMath::IsNearlyEqual(A->Angle, B->Angle))
		{
			return A->Angle < B->Angle;
		}
		if (A->bHasSource != B->bHasSource)
		{
			return A->bHasSource;
		}
		return GetSourceName(A->Source) < GetSourceName(B->Source);
	});

	RefreshValidation();
}

void SQuickSDFMaskImportPanel::RefreshValidation()
{
	UQuickSDFPaintTool* Tool = PaintTool.Get();
	UQuickSDFToolProperties* Properties = Tool ? Tool->Properties.Get() : nullptr;
	const FString ImportFolder = CleanContentFolder(Properties ? Properties->ImportedMaskFolder : FString(TEXT("/Game/QuickSDF_Imports")));
	const bool bBaseSymmetry = !Properties || Properties->bSymmetryMode;
	const bool bNeedsFullRange = Rows.ContainsByPredicate([](const TSharedPtr<FQuickSDFMaskImportRowData>& Row)
	{
		return Row.IsValid() && Row->bHasSource && Row->Angle > 90.01f;
	});

	int32 FirstWidth = 0;
	int32 FirstHeight = 0;
	for (const TSharedPtr<FQuickSDFMaskImportRowData>& Row : Rows)
	{
		if (Row.IsValid() && Row->bHasSource && Row->Width > 0 && Row->Height > 0)
		{
			FirstWidth = Row->Width;
			FirstHeight = Row->Height;
			break;
		}
	}

	TSet<FString> ReservedExternalDestinations;
	for (int32 RowIndex = 0; RowIndex < Rows.Num(); ++RowIndex)
	{
		TSharedPtr<FQuickSDFMaskImportRowData> Row = Rows[RowIndex];
		if (!Row.IsValid())
		{
			continue;
		}

		Row->SlotIndex = RowIndex;
		TArray<FText> Warnings;
		if (!Row->bHasSource)
		{
			Warnings.Add(LOCTEXT("MissingSourceWarning", "Missing source"));
		}
		else
		{
			if (Row->bAngleInferred)
			{
				Warnings.Add(LOCTEXT("InferredAngleWarning", "Angle inferred"));
			}
			if (bBaseSymmetry && Row->Angle > 90.01f)
			{
				Warnings.Add(LOCTEXT("FullRangeWarning", "Switches to 0-180"));
			}
			if ((Row->Width <= 0 || Row->Height <= 0) && Row->Source.bExternalFile)
			{
				Warnings.Add(LOCTEXT("UnknownSizeWarning", "Size checked on import"));
			}
			if (FirstWidth > 0 && FirstHeight > 0 && Row->Width > 0 && Row->Height > 0 &&
				(Row->Width != FirstWidth || Row->Height != FirstHeight))
			{
				Warnings.Add(LOCTEXT("ResolutionMismatchWarning", "Resolution differs"));
			}

			for (const TSharedPtr<FQuickSDFMaskImportRowData>& Other : Rows)
			{
				if (Other != Row && Other.IsValid() && Other->bHasSource && FMath::IsNearlyEqual(Other->Angle, Row->Angle, 0.05f))
				{
					Warnings.Add(LOCTEXT("DuplicateAngleWarning", "Duplicate angle"));
					break;
				}
			}

			Row->Destination = Row->Source.bExternalFile
				? BuildExternalDestination(Row->Source, ImportFolder, &ReservedExternalDestinations)
				: (Row->Source.Texture.IsValid() ? Row->Source.Texture->GetPathName() : FString());
		}

		Row->WarningText = Warnings.Num() > 0 ? FText::Join(LOCTEXT("WarningSeparator", " / "), Warnings) : FText::GetEmpty();
	}

	if (RowListView.IsValid())
	{
		RowListView->RequestListRefresh();
	}
}

bool SQuickSDFMaskImportPanel::HasImportableRows() const
{
	for (const TSharedPtr<FQuickSDFMaskImportRowData>& Row : Rows)
	{
		if (Row.IsValid() && Row->bHasSource)
		{
			return true;
		}
	}
	return false;
}

FText SQuickSDFMaskImportPanel::GetSummaryText() const
{
	int32 SourceCount = 0;
	int32 WarningCount = 0;
	for (const TSharedPtr<FQuickSDFMaskImportRowData>& Row : Rows)
	{
		if (Row.IsValid() && Row->bHasSource)
		{
			++SourceCount;
		}
		if (Row.IsValid() && !Row->WarningText.IsEmpty())
		{
			++WarningCount;
		}
	}

	return FText::Format(
		LOCTEXT("ImportSummary", "{0} sources / {1} expected slots / {2} warnings"),
		FText::AsNumber(SourceCount),
		FText::AsNumber(Rows.Num()),
		FText::AsNumber(WarningCount));
}

FText SQuickSDFMaskImportPanel::GetApplyToolTipText() const
{
	return HasImportableRows()
		? LOCTEXT("ApplyTooltip", "Create needed Texture2D assets, then assign the listed masks to the timeline.")
		: LOCTEXT("ApplyDisabledTooltip", "Add at least one texture or image file before applying.");
}

FText SQuickSDFMaskImportPanel::GetImportedFolderText() const
{
	UQuickSDFPaintTool* Tool = PaintTool.Get();
	const UQuickSDFToolProperties* Properties = Tool ? Tool->Properties.Get() : nullptr;
	const FString ImportFolder = CleanContentFolder(Properties ? Properties->ImportedMaskFolder : FString(TEXT("/Game/QuickSDF_Imports")));
	return FText::Format(LOCTEXT("ImportFolderSummary", "External images will be created under {0} only after Apply Import."), FText::FromString(ImportFolder));
}

FReply SQuickSDFMaskImportPanel::OnAddFilesClicked()
{
	IDesktopPlatform* DesktopPlatform = FDesktopPlatformModule::Get();
	if (!DesktopPlatform)
	{
		return FReply::Handled();
	}

	TArray<FString> SourceFilenames;
	const void* ParentWindowHandle = FSlateApplication::Get().FindBestParentWindowHandleForDialogs(nullptr);
	const bool bSelectedFiles = DesktopPlatform->OpenFileDialog(
		ParentWindowHandle,
		TEXT("Add Quick SDF Mask Images"),
		FString(),
		FString(),
		TEXT("Image files|*.png;*.jpg;*.jpeg;*.bmp;*.tga;*.exr|All files|*.*"),
		EFileDialogFlags::Multiple,
		SourceFilenames);

	if (!bSelectedFiles || SourceFilenames.Num() == 0)
	{
		return FReply::Handled();
	}

	TArray<FQuickSDFMaskImportSource> NewSources;
	for (const FString& Filename : SourceFilenames)
	{
		if (!IsSupportedImageFile(Filename))
		{
			continue;
		}

		FQuickSDFMaskImportSource Source;
		Source.bExternalFile = true;
		Source.SourcePath = Filename;
		Source.DisplayName = FPaths::GetBaseFilename(Filename);
		ReadImageDimensions(Filename, Source.Width, Source.Height);
		NewSources.Add(Source);
	}
	AddSources(NewSources);
	return FReply::Handled();
}

FReply SQuickSDFMaskImportPanel::OnAddAssetsClicked()
{
	FContentBrowserModule& ContentBrowserModule = FModuleManager::LoadModuleChecked<FContentBrowserModule>("ContentBrowser");

	FAssetPickerConfig AssetPickerConfig;
	AssetPickerConfig.SelectionMode = ESelectionMode::Single;
	AssetPickerConfig.Filter.ClassPaths.Add(UTexture2D::StaticClass()->GetClassPathName());
	AssetPickerConfig.Filter.bRecursiveClasses = true;
	AssetPickerConfig.InitialAssetViewType = EAssetViewType::List;
	AssetPickerConfig.bAllowNullSelection = false;
	AssetPickerConfig.OnAssetSelected = FOnAssetSelected::CreateSP(this, &SQuickSDFMaskImportPanel::OnAssetPicked);

	TSharedRef<SWidget> MenuContent = SNew(SBox)
		.WidthOverride(460.0f)
		.HeightOverride(520.0f)
		[
			ContentBrowserModule.Get().CreateAssetPicker(AssetPickerConfig)
		];

	FSlateApplication::Get().PushMenu(
		AsShared(),
		FWidgetPath(),
		MenuContent,
		FSlateApplication::Get().GetCursorPos(),
		FPopupTransitionEffect(FPopupTransitionEffect::ContextMenu),
		true);

	return FReply::Handled();
}

void SQuickSDFMaskImportPanel::OnAssetPicked(const FAssetData& AssetData)
{
	if (UTexture2D* Texture = Cast<UTexture2D>(AssetData.GetAsset()))
	{
		FQuickSDFMaskImportSource Source;
		Source.Texture = Texture;
		Source.DisplayName = Texture->GetName();
		Source.Width = Texture->GetSizeX();
		Source.Height = Texture->GetSizeY();
		AddSources({ Source });
	}
	FSlateApplication::Get().DismissAllMenus();
}

FReply SQuickSDFMaskImportPanel::OnCompleteClicked()
{
	UQuickSDFPaintTool* Tool = PaintTool.Get();
	const UQuickSDFToolProperties* Properties = Tool ? Tool->Properties.Get() : nullptr;
	const bool bSymmetry = !Properties || Properties->bSymmetryMode;
	ExpectedCountOverride = QuickSDFPaintToolPrivate::GetQuickSDFDefaultAngleCount(bSymmetry);
	RebuildRows();
	if (RowListView.IsValid())
	{
		RowListView->RequestListRefresh();
	}
	return FReply::Handled();
}

FReply SQuickSDFMaskImportPanel::OnEvenClicked()
{
	TArray<TSharedPtr<FQuickSDFMaskImportRowData>> SourceRows;
	for (const TSharedPtr<FQuickSDFMaskImportRowData>& Row : Rows)
	{
		if (Row.IsValid() && Row->bHasSource)
		{
			SourceRows.Add(Row);
		}
	}

	UQuickSDFPaintTool* Tool = PaintTool.Get();
	const UQuickSDFToolProperties* Properties = Tool ? Tool->Properties.Get() : nullptr;
	const bool bSymmetry = !Properties || Properties->bSymmetryMode;
	const float MaxAngle = bSymmetry ? 90.0f : 180.0f;
	for (int32 Index = 0; Index < SourceRows.Num(); ++Index)
	{
		SourceRows[Index]->Angle = SourceRows.Num() > 1
			? (static_cast<float>(Index) / static_cast<float>(SourceRows.Num() - 1)) * MaxAngle
			: 0.0f;
		SourceRows[Index]->bAngleInferred = false;
	}
	RefreshValidation();
	return FReply::Handled();
}

FReply SQuickSDFMaskImportPanel::OnApplyClicked()
{
	UQuickSDFPaintTool* Tool = PaintTool.Get();
	UQuickSDFToolProperties* Properties = Tool ? Tool->Properties.Get() : nullptr;
	UQuickSDFToolSubsystem* Subsystem = GEditor ? GEditor->GetEditorSubsystem<UQuickSDFToolSubsystem>() : nullptr;
	if (!Tool || !Properties || !Subsystem)
	{
		return FReply::Handled();
	}

	TArray<UTexture2D*> Textures;
	TArray<float> Angles;
	int32 CreatedTextureCount = 0;
	const FString ImportFolder = CleanContentFolder(Properties->ImportedMaskFolder);

	for (const TSharedPtr<FQuickSDFMaskImportRowData>& Row : Rows)
	{
		if (!Row.IsValid() || !Row->bHasSource)
		{
			continue;
		}

		UTexture2D* Texture = Row->Source.Texture.Get();
		if (Row->Source.bExternalFile)
		{
			FText ImportError;
			Texture = Subsystem->ImportMaskFileAsTexture(Row->Source.SourcePath, ImportFolder, false, &ImportError);
			if (!Texture)
			{
				if (!ImportError.IsEmpty())
				{
					FMessageDialog::Open(EAppMsgType::Ok, ImportError);
				}
				return FReply::Handled();
			}
			++CreatedTextureCount;
		}

		if (Texture)
		{
			Textures.Add(Texture);
			Angles.Add(Row->Angle);
		}
	}

	if (Textures.Num() == 0)
	{
		return FReply::Handled();
	}

	if (Tool->ImportEditedMasksFromTexturesWithAngles(Textures, Angles))
	{
		FNotificationInfo Info(FText::Format(
			LOCTEXT("ImportCompleteNotification", "Imported {0} masks / Created {1} textures in {2}"),
			FText::AsNumber(Textures.Num()),
			FText::AsNumber(CreatedTextureCount),
			FText::FromString(ImportFolder)));
		Info.ExpireDuration = 4.0f;
		Info.bUseLargeFont = false;
		if (TSharedPtr<SNotificationItem> Notification = FSlateNotificationManager::Get().AddNotification(Info))
		{
			Notification->SetCompletionState(SNotificationItem::CS_Success);
		}

		OnClosed.ExecuteIfBound();
	}

	return FReply::Handled();
}

FReply SQuickSDFMaskImportPanel::OnCancelClicked()
{
	OnClosed.ExecuteIfBound();
	return FReply::Handled();
}

#undef LOCTEXT_NAMESPACE
