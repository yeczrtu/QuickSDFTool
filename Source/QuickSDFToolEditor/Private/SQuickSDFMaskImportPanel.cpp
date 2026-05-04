#include "SQuickSDFMaskImportPanel.h"

#include "AssetRegistry/AssetData.h"
#include "DragAndDrop/AssetDragDropOp.h"
#include "Editor.h"
#include "Engine/Texture2D.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Notifications/NotificationManager.h"
#include "InputCoreTypes.h"
#include "Misc/MessageDialog.h"
#include "PropertyCustomizationHelpers.h"
#include "QuickSDFPaintTool.h"
#include "QuickSDFPaintToolPrivate.h"
#include "QuickSDFMaskImportModel.h"
#include "QuickSDFToolProperties.h"
#include "QuickSDFToolStyle.h"
#include "Styling/AppStyle.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SNumericEntryBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "Widgets/Views/SHeaderRow.h"
#include "Widgets/Views/STableRow.h"

#define LOCTEXT_NAMESPACE "SQuickSDFMaskImportPanel"

namespace
{
constexpr float QuickSDFImportPanelWidth = 920.0f;
constexpr float QuickSDFImportPanelListHeight = 210.0f;
constexpr float QuickSDFImportAngleMatchTolerance = 0.05f;

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

using namespace QuickSDFMaskImportModel;

struct FQuickSDFMaskImportRowData
{
	FQuickSDFMaskImportSource Source;
	bool bHasSource = false;
	bool bAngleFromName = false;
	bool bAngleInferred = false;
	bool bAngleFromCurrentSelection = false;
	int32 SlotIndex = 0;
	int32 ExistingSlotIndex = INDEX_NONE;
	float Angle = 0.0f;
	int32 Width = 0;
	int32 Height = 0;
	FText AssignmentText;
	FText AssignmentToolTipText;
	FText WriteText;
	FText WriteToolTipText;
	FString WriteTargetPath;
	FText WarningText;
	bool bCanToggleWrite = false;
	bool bAllowSourceTextureOverwrite = false;
	bool bWriteConflict = false;
};

bool IsApplicableImportRow(const TSharedPtr<FQuickSDFMaskImportRowData>& Row)
{
	return Row.IsValid() && Row->bHasSource && Row->ExistingSlotIndex != INDEX_NONE;
}

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
			return SNew(SObjectPropertyEntryBox)
				.AllowedClass(UTexture2D::StaticClass())
				.AllowClear(true)
				.DisplayBrowse(true)
				.DisplayUseSelected(true)
				.DisplayCompactSize(true)
				.DisplayThumbnail(false)
				.ObjectPath_Lambda([Item = Item]() -> FString
				{
					return Item.IsValid() && Item->Source.Texture.IsValid()
						? Item->Source.Texture->GetPathName()
						: FString();
				})
				.OnObjectChanged_Lambda([Item = Item, Panel = Panel](const FAssetData& AssetData)
				{
					if (TSharedPtr<SQuickSDFMaskImportPanel> PinnedPanel = Panel.Pin())
					{
						PinnedPanel->SetRowTexture(Item, AssetData);
					}
				})
				.ToolTipText(Item->bHasSource
					? FText::FromString(GetSourceKey(Item->Source))
					: LOCTEXT("MissingMaskTooltip", "Choose a Texture2D asset for this slot, use the selected Content Browser asset, or drag a Texture2D here."));
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

		if (ColumnName == TEXT("Assignment"))
		{
			return SNew(STextBlock)
				.Text(Item->AssignmentText)
				.ToolTipText(Item->AssignmentToolTipText)
				.ColorAndOpacity(Item->bHasSource ? FSlateColor::UseForeground() : MutedColor)
				.Font(FAppStyle::GetFontStyle("SmallFont"));
		}

		if (ColumnName == TEXT("Size"))
		{
			return SNew(STextBlock)
				.Text(FormatSize(Item->Width, Item->Height))
				.ColorAndOpacity(Item->bHasSource ? FSlateColor::UseForeground() : MutedColor)
				.Font(FAppStyle::GetFontStyle("SmallFont"));
		}

		if (ColumnName == TEXT("Write"))
		{
			if (!Item->bHasSource)
			{
				return SNew(STextBlock);
			}

			if (Item->bCanToggleWrite)
			{
				return SNew(SCheckBox)
					.IsChecked_Lambda([Item = Item]()
					{
						const bool bChecked = Item.IsValid() && Item->Source.bAllowSourceTextureOverwrite;
						return bChecked ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
					})
					.OnCheckStateChanged_Lambda([Item = Item, Panel = Panel](ECheckBoxState NewState)
					{
						if (TSharedPtr<SQuickSDFMaskImportPanel> PinnedPanel = Panel.Pin())
						{
							PinnedPanel->SetRowWriteChecked(Item, NewState == ECheckBoxState::Checked);
						}
					})
					.ToolTipText(Item->WriteToolTipText)
					[
						SNew(STextBlock)
						.Text(Item->WriteText)
						.Font(FAppStyle::GetFontStyle("SmallFont"))
					];
			}

			return SNew(STextBlock)
				.Text(Item->WriteText)
				.ToolTipText(Item->WriteToolTipText)
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
				.Padding(0.0f, 6.0f, 0.0f, 6.0f)
				[
					SNew(STextBlock)
					.Text(this, &SQuickSDFMaskImportPanel::GetSelectedTargetText)
					.ColorAndOpacity(FLinearColor(0.35f, 0.82f, 1.0f, 1.0f))
					.Font(FAppStyle::GetFontStyle("SmallFont"))
				]

				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0.0f, 0.0f, 0.0f, 6.0f)
				[
					SNew(STextBlock)
					.Text(this, &SQuickSDFMaskImportPanel::GetWriteSummaryText)
					.ColorAndOpacity(this, &SQuickSDFMaskImportPanel::GetWriteSummaryColor)
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
						.SelectionMode(ESelectionMode::Single)
						.OnGenerateRow(this, &SQuickSDFMaskImportPanel::GenerateRow)
						.OnSelectionChanged(this, &SQuickSDFMaskImportPanel::OnRowSelectionChanged)
						.HeaderRow
						(
							SNew(SHeaderRow)
							+ SHeaderRow::Column(TEXT("Slot")).FixedWidth(38.0f).DefaultLabel(LOCTEXT("SlotColumn", "#"))
							+ SHeaderRow::Column(TEXT("Source")).FillWidth(1.35f).DefaultLabel(LOCTEXT("SourceColumn", "Texture"))
							+ SHeaderRow::Column(TEXT("Angle")).FixedWidth(112.0f).DefaultLabel(LOCTEXT("AngleColumn", "Angle"))
							+ SHeaderRow::Column(TEXT("Assignment")).FixedWidth(132.0f).DefaultLabel(LOCTEXT("AssignmentColumn", "Assignment"))
							+ SHeaderRow::Column(TEXT("Size")).FixedWidth(96.0f).DefaultLabel(LOCTEXT("SizeColumn", "Size"))
							+ SHeaderRow::Column(TEXT("Write")).FixedWidth(148.0f).DefaultLabel(LOCTEXT("WriteColumn", "Write"))
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
						MakeSmallButton(LOCTEXT("Complete", "Complete"), LOCTEXT("CompleteTooltip", "Show the default expected mask slots for the current symmetry mode."), FOnClicked::CreateSP(this, &SQuickSDFMaskImportPanel::OnCompleteClicked))
					]
					+ SHorizontalBox::Slot().AutoWidth()
					[
						MakeSmallButton(LOCTEXT("Even", "Even"), LOCTEXT("EvenTooltip", "Redistribute source angles evenly."), FOnClicked::CreateSP(this, &SQuickSDFMaskImportPanel::OnEvenClicked))
					]
					+ SHorizontalBox::Slot().AutoWidth().Padding(4.0f, 0.0f, 0.0f, 0.0f)
					[
						SNew(SButton)
						.IsEnabled(this, &SQuickSDFMaskImportPanel::CanUndoPreviewChange)
						.ToolTipText(this, &SQuickSDFMaskImportPanel::GetUndoToolTipText)
						.OnClicked(this, &SQuickSDFMaskImportPanel::OnUndoClicked)
						.ContentPadding(FMargin(7.0f, 3.0f))
						[
							SNew(STextBlock)
							.Text(LOCTEXT("UndoPreviewEdit", "Undo"))
							.Font(FAppStyle::GetFontStyle("SmallFont"))
						]
					]
					+ SHorizontalBox::Slot().FillWidth(1.0f)
					[
						SNew(SSpacer)
					]
					+ SHorizontalBox::Slot().AutoWidth().Padding(0.0f, 0.0f, 4.0f, 0.0f)
					[
						SNew(SButton)
						.IsEnabled(this, &SQuickSDFMaskImportPanel::CanApplyImport)
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

	SelectDefaultRow();
}

FReply SQuickSDFMaskImportPanel::OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (MouseEvent.GetEffectingButton() == EKeys::LeftMouseButton ||
		MouseEvent.GetEffectingButton() == EKeys::MiddleMouseButton ||
		MouseEvent.GetEffectingButton() == EKeys::RightMouseButton)
	{
		FSlateApplication::Get().SetKeyboardFocus(AsShared(), EFocusCause::Mouse);
		return FReply::Handled().CaptureMouse(AsShared());
	}

	return FReply::Handled();
}

FReply SQuickSDFMaskImportPanel::OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	return HasMouseCapture()
		? FReply::Handled().ReleaseMouseCapture()
		: FReply::Handled();
}

FReply SQuickSDFMaskImportPanel::OnMouseMove(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	return FReply::Handled();
}

FReply SQuickSDFMaskImportPanel::OnMouseWheel(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	return FReply::Handled();
}

FReply SQuickSDFMaskImportPanel::OnDragOver(const FGeometry& MyGeometry, const FDragDropEvent& DragDropEvent)
{
	return FReply::Handled();
}

FReply SQuickSDFMaskImportPanel::OnDrop(const FGeometry& MyGeometry, const FDragDropEvent& DragDropEvent)
{
	if (TSharedPtr<FAssetDragDropOp> AssetDragDropOp = DragDropEvent.GetOperationAs<FAssetDragDropOp>())
	{
		TArray<FQuickSDFMaskImportSource> NewSources;
		for (const FAssetData& AssetData : AssetDragDropOp->GetAssets())
		{
			if (UTexture2D* Texture = Cast<UTexture2D>(AssetData.GetAsset()))
			{
				NewSources.Add(MakeImportSourceFromTexture(Texture));
			}
		}
		AddSourcesFromUserAction(NewSources);
		return FReply::Handled();
	}

	return FReply::Handled();
}

bool SQuickSDFMaskImportPanel::SupportsKeyboardFocus() const
{
	return true;
}

FReply SQuickSDFMaskImportPanel::OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)
{
	if (InKeyEvent.GetKey() == EKeys::Z && (InKeyEvent.IsControlDown() || InKeyEvent.IsCommandDown()) && CanUndoPreviewChange())
	{
		return OnUndoClicked();
	}

	return FReply::Unhandled();
}

TSharedRef<ITableRow> SQuickSDFMaskImportPanel::GenerateRow(TSharedPtr<FQuickSDFMaskImportRowData> Item, const TSharedRef<STableViewBase>& OwnerTable)
{
	return SNew(SQuickSDFMaskImportRow, OwnerTable)
		.Item(Item)
		.Panel(SharedThis(this));
}

void SQuickSDFMaskImportPanel::SetRowTexture(TSharedPtr<FQuickSDFMaskImportRowData> Row, const FAssetData& AssetData)
{
	if (!Row.IsValid())
	{
		return;
	}

	UTexture2D* Texture = AssetData.IsValid() ? Cast<UTexture2D>(AssetData.GetAsset()) : nullptr;
	if (!Texture)
	{
		ClearSourceFromRow(Row);
		return;
	}

	if (Row->bHasSource && Row->Source.Texture.Get() == Texture)
	{
		return;
	}

	PushUndoState();
	AddSingleSourceToSelectedRow(MakeImportSourceFromTexture(Texture), Row);
}

void SQuickSDFMaskImportPanel::SetRowWriteChecked(TSharedPtr<FQuickSDFMaskImportRowData> Row, bool bChecked)
{
	if (!Row.IsValid() || !Row->bHasSource || !Row->bCanToggleWrite)
	{
		return;
	}

	const int32 SourceIndex = FindSourceIndex(Row->Source);
	if (!Sources.IsValidIndex(SourceIndex))
	{
		return;
	}

	PushUndoState();
	Sources[SourceIndex].bAllowSourceTextureOverwrite = bChecked;

	RebuildRows();
	SelectRowClosestToAngle(Row->Angle);
	if (RowListView.IsValid())
	{
		RowListView->RequestListRefresh();
	}
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

void SQuickSDFMaskImportPanel::AddSourcesFromUserAction(const TArray<FQuickSDFMaskImportSource>& NewSources)
{
	if (NewSources.Num() == 0)
	{
		return;
	}

	const TSharedPtr<FQuickSDFMaskImportRowData> SelectedRow = GetSelectedRow();
	PushUndoState();
	if (NewSources.Num() == 1 && SelectedRow.IsValid())
	{
		AddSingleSourceToSelectedRow(NewSources[0], SelectedRow);
		return;
	}
	if (NewSources.Num() > 1 && SelectedRow.IsValid())
	{
		AddMultipleSourcesFromSelectedRow(NewSources, SelectedRow);
		return;
	}

	AddSources(NewSources);
}

void SQuickSDFMaskImportPanel::AddSingleSourceToSelectedRow(FQuickSDFMaskImportSource Source, const TSharedPtr<FQuickSDFMaskImportRowData>& SelectedRow)
{
	if (!SelectedRow.IsValid())
	{
		AddSources({ Source });
		return;
	}

	if (SelectedRow->bHasSource)
	{
		const int32 ExistingSourceIndex = FindSourceIndex(SelectedRow->Source);
		if (ExistingSourceIndex != INDEX_NONE)
		{
			Sources.RemoveAt(ExistingSourceIndex);
		}
	}

	const float TargetAngle = SelectedRow->Angle;
	Source.bHasForcedAngle = true;
	Source.ForcedAngle = TargetAngle;
	Sources.Add(Source);

	RebuildRows();
	SelectRowClosestToAngle(TargetAngle);
	if (RowListView.IsValid())
	{
		RowListView->RequestListRefresh();
	}
}

void SQuickSDFMaskImportPanel::AddMultipleSourcesFromSelectedRow(TArray<FQuickSDFMaskImportSource> NewSources, const TSharedPtr<FQuickSDFMaskImportRowData>& SelectedRow)
{
	if (!SelectedRow.IsValid())
	{
		AddSources(NewSources);
		return;
	}

	const int32 StartIndex = FMath::Clamp(SelectedRow->SlotIndex, 0, FMath::Max(Rows.Num() - 1, 0));
	const float Step = GetFallbackAssignmentStep();
	TArray<int32> SourceIndicesToRemove;
	SourceIndicesToRemove.Reserve(NewSources.Num());

	for (int32 SourceIndex = 0; SourceIndex < NewSources.Num(); ++SourceIndex)
	{
		const int32 TargetRowIndex = StartIndex + SourceIndex;
		float TargetAngle = SelectedRow->Angle + Step * static_cast<float>(SourceIndex);
		if (Rows.IsValidIndex(TargetRowIndex) && Rows[TargetRowIndex].IsValid())
		{
			TargetAngle = Rows[TargetRowIndex]->Angle;
			if (Rows[TargetRowIndex]->bHasSource)
			{
				const int32 ExistingSourceIndex = FindSourceIndex(Rows[TargetRowIndex]->Source);
				if (ExistingSourceIndex != INDEX_NONE)
				{
					SourceIndicesToRemove.AddUnique(ExistingSourceIndex);
				}
			}
		}

		NewSources[SourceIndex].bHasForcedAngle = true;
		NewSources[SourceIndex].ForcedAngle = TargetAngle;
	}

	SourceIndicesToRemove.Sort(TGreater<int32>());
	for (int32 ExistingSourceIndex : SourceIndicesToRemove)
	{
		if (Sources.IsValidIndex(ExistingSourceIndex))
		{
			Sources.RemoveAt(ExistingSourceIndex);
		}
	}

	Sources.Append(NewSources);

	RebuildRows();
	SelectRowClosestToAngle(NewSources[0].ForcedAngle);
	if (RowListView.IsValid())
	{
		RowListView->RequestListRefresh();
	}
}

void SQuickSDFMaskImportPanel::ClearSourceFromRow(const TSharedPtr<FQuickSDFMaskImportRowData>& Row)
{
	if (!Row.IsValid() || !Row->bHasSource)
	{
		return;
	}

	const int32 ExistingSourceIndex = FindSourceIndex(Row->Source);
	if (ExistingSourceIndex == INDEX_NONE)
	{
		return;
	}

	const float TargetAngle = Row->Angle;
	PushUndoState();
	Sources.RemoveAt(ExistingSourceIndex);
	RebuildRows();
	SelectRowClosestToAngle(TargetAngle);
	if (RowListView.IsValid())
	{
		RowListView->RequestListRefresh();
	}
}

int32 SQuickSDFMaskImportPanel::FindSourceIndex(const FQuickSDFMaskImportSource& Source) const
{
	for (int32 Index = 0; Index < Sources.Num(); ++Index)
	{
		if (DoSourcesReferToSameContent(Sources[Index], Source))
		{
			return Index;
		}
	}
	return INDEX_NONE;
}

float SQuickSDFMaskImportPanel::GetFallbackAssignmentStep() const
{
	if (Rows.Num() > 1)
	{
		for (int32 Index = 1; Index < Rows.Num(); ++Index)
		{
			if (Rows[Index].IsValid() && Rows[Index - 1].IsValid())
			{
				const float Step = Rows[Index]->Angle - Rows[Index - 1]->Angle;
				if (Step > KINDA_SMALL_NUMBER)
				{
					return Step;
				}
			}
		}
	}

	UQuickSDFPaintTool* Tool = PaintTool.Get();
	const UQuickSDFToolProperties* Properties = Tool ? Tool->Properties.Get() : nullptr;
	const bool bSymmetry = !Properties || Properties->UsesFrontHalfAngles();
	const int32 DefaultCount = QuickSDFPaintToolPrivate::GetQuickSDFDefaultAngleCount(bSymmetry);
	return (bSymmetry ? 90.0f : 180.0f) / static_cast<float>(FMath::Max(DefaultCount - 1, 1));
}

void SQuickSDFMaskImportPanel::RebuildRows()
{
	Rows.Reset();

	UQuickSDFPaintTool* Tool = PaintTool.Get();
	UQuickSDFToolProperties* Properties = Tool ? Tool->Properties.Get() : nullptr;
	const bool bBaseSymmetry = !Properties || Properties->UsesFrontHalfAngles();

	bool bNeedsFullRange = false;
	TArray<TSharedPtr<FQuickSDFMaskImportRowData>> SourceRows;
	SourceRows.Reserve(Sources.Num());

	for (const FQuickSDFMaskImportSource& Source : Sources)
	{
		if (!Source.Texture.IsValid())
		{
			continue;
		}

		TSharedPtr<FQuickSDFMaskImportRowData> Row = MakeShared<FQuickSDFMaskImportRowData>();
		Row->Source = Source;
		Row->bHasSource = true;
		Row->Width = Source.Width;
		Row->Height = Source.Height;
		if (Source.bHasForcedAngle)
		{
			Row->Angle = Source.ForcedAngle;
			Row->bAngleFromName = false;
			Row->bAngleInferred = false;
			Row->bAngleFromCurrentSelection = true;
		}
		else
		{
			Row->bAngleFromName = QuickSDFPaintToolPrivate::TryExtractAngleFromName(GetSourceName(Source), Row->Angle);
			Row->bAngleInferred = !Row->bAngleFromName;
		}
		bNeedsFullRange |= Row->bAngleFromName && Row->Angle > 90.01f;
		bNeedsFullRange |= Source.bHasForcedAngle && Row->Angle > 90.01f;
		SourceRows.Add(Row);
	}

	const float MaxAngle = (bBaseSymmetry && !bNeedsFullRange) ? 90.0f : 180.0f;
	int32 AutoIndex = 0;
	int32 AutoCount = 0;
	const bool bUseCurrentAngleForSingleSource =
		SourceRows.Num() == 1 &&
		SourceRows[0].IsValid() &&
		SourceRows[0]->bAngleInferred &&
		Properties &&
		Properties->TargetAngles.IsValidIndex(Properties->EditAngleIndex);

	if (bUseCurrentAngleForSingleSource)
	{
		SourceRows[0]->Angle = Properties->TargetAngles[Properties->EditAngleIndex];
		SourceRows[0]->bAngleInferred = false;
		SourceRows[0]->bAngleFromCurrentSelection = true;
	}

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
	int32 ForcedSourceCount = 0;
	float ForcedMinAngle = TNumericLimits<float>::Max();
	float ForcedMaxAngle = TNumericLimits<float>::Lowest();
	for (const TSharedPtr<FQuickSDFMaskImportRowData>& Row : Rows)
	{
		if (Row.IsValid() && Row->bHasSource && Row->Source.bHasForcedAngle)
		{
			++ForcedSourceCount;
			ForcedMinAngle = FMath::Min(ForcedMinAngle, Row->Angle);
			ForcedMaxAngle = FMath::Max(ForcedMaxAngle, Row->Angle);
		}
	}
	const bool bHasForcedRun = ForcedSourceCount > 1;
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
		const bool bExpectedAngleInsideForcedRun = bHasForcedRun &&
			ExpectedAngle >= ForcedMinAngle - QuickSDFImportAngleMatchTolerance &&
			ExpectedAngle <= ForcedMaxAngle + QuickSDFImportAngleMatchTolerance;
		if (bExpectedAngleOccupied || bExpectedAngleInsideForcedRun)
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
	const bool bBaseSymmetry = !Properties || Properties->UsesFrontHalfAngles();
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

	TMap<FString, int32> WritableSourceTargetCounts;
	for (int32 RowIndex = 0; RowIndex < Rows.Num(); ++RowIndex)
	{
		TSharedPtr<FQuickSDFMaskImportRowData> Row = Rows[RowIndex];
		if (!Row.IsValid())
		{
			continue;
		}

		Row->SlotIndex = RowIndex;
		Row->ExistingSlotIndex = INDEX_NONE;
		Row->WriteText = FText::GetEmpty();
		Row->WriteToolTipText = FText::GetEmpty();
		Row->WriteTargetPath.Empty();
		Row->bCanToggleWrite = false;
		Row->bAllowSourceTextureOverwrite = false;
		Row->bWriteConflict = false;
		if (Properties)
		{
			for (int32 TargetIndex = 0; TargetIndex < Properties->TargetAngles.Num(); ++TargetIndex)
			{
				if (FMath::IsNearlyEqual(Properties->TargetAngles[TargetIndex], Row->Angle, QuickSDFImportAngleMatchTolerance))
				{
					Row->ExistingSlotIndex = TargetIndex;
					break;
				}
			}
		}

		const FText AngleText = FText::AsNumber(Row->Angle, &FNumberFormattingOptions::DefaultNoGrouping());
		if (!Row->bHasSource)
		{
			Row->AssignmentText = LOCTEXT("EmptyAssignment", "No source");
			Row->AssignmentToolTipText = FText::Format(
				LOCTEXT("EmptyAssignmentTooltip", "This is an expected {0} deg slot, but no Texture2D asset will be assigned to it."),
				AngleText);
		}
		else if (Row->ExistingSlotIndex != INDEX_NONE)
		{
			const bool bCurrentSlot = Properties && Row->ExistingSlotIndex == Properties->EditAngleIndex;
			Row->AssignmentText = bCurrentSlot
				? FText::Format(LOCTEXT("CurrentSlotAssignment", "Current slot {0}"), FText::AsNumber(Row->ExistingSlotIndex + 1))
				: FText::Format(LOCTEXT("ExistingSlotAssignment", "Slot {0}"), FText::AsNumber(Row->ExistingSlotIndex + 1));
			Row->AssignmentToolTipText = FText::Format(
				bCurrentSlot ? LOCTEXT("CurrentSlotAssignmentTooltip", "Apply Import will assign this asset to the current timeline slot at {0} deg.")
							 : LOCTEXT("ExistingSlotAssignmentTooltip", "Apply Import will assign this asset to existing timeline slot {1} at {0} deg."),
				AngleText,
				FText::AsNumber(Row->ExistingSlotIndex + 1));
		}
		else
		{
			Row->AssignmentText = LOCTEXT("SkippedSlotAssignment", "Skipped");
			Row->AssignmentToolTipText = FText::Format(
				LOCTEXT("SkippedSlotAssignmentTooltip", "This asset is not assigned to an existing timeline slot at {0} deg and will be skipped by Apply Import."),
				AngleText);
		}

		TArray<FText> Warnings;
		if (Row->bHasSource)
		{
			if (Row->bAngleInferred)
			{
				Warnings.Add(LOCTEXT("InferredAngleWarning", "Angle inferred"));
			}
			if (Row->ExistingSlotIndex == INDEX_NONE)
			{
				Warnings.Add(LOCTEXT("NoExistingSlotWarning", "No existing slot; skipped"));
			}
			if (bBaseSymmetry && Row->Angle > 90.01f)
			{
				Warnings.Add(LOCTEXT("FullRangeWarning", "Switches to 0-180"));
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

			Row->WriteTargetPath = Row->Source.Texture.IsValid() ? Row->Source.Texture->GetPathName() : FString();
			const bool bProtectedEngineTexture = IsEngineTexture(Row->Source.Texture.Get());
			Row->bCanToggleWrite = Row->Source.Texture.IsValid() && !bProtectedEngineTexture;
			Row->bAllowSourceTextureOverwrite = Row->Source.bAllowSourceTextureOverwrite && Row->Source.Texture.IsValid() && !bProtectedEngineTexture;
			Row->WriteText = bProtectedEngineTexture
				? LOCTEXT("WriteProtectedEngineSource", "Protected")
				: LOCTEXT("WriteAllowWriteBack", "Allow write-back");
			Row->WriteToolTipText = bProtectedEngineTexture
				? LOCTEXT("WriteProtectedEngineSourceTooltip", "Engine Texture2D assets cannot be overwritten by QuickSDFTool.")
				: (Row->bAllowSourceTextureOverwrite
					? LOCTEXT("WriteAllowWriteBackCheckedTooltip", "This Texture2D is allowed to be overwritten later by Overwrite Source Textures.")
					: LOCTEXT("WriteAllowWriteBackTooltip", "Check this to allow Overwrite Source Textures to write the edited mask back to this Texture2D later."));
			if (bProtectedEngineTexture)
			{
				Warnings.Add(LOCTEXT("EngineSourceProtectedWarning", "Engine texture protected"));
			}
			if (Row->bAllowSourceTextureOverwrite && Row->ExistingSlotIndex != INDEX_NONE)
			{
				WritableSourceTargetCounts.FindOrAdd(Row->WriteTargetPath)++;
			}
		}

		Row->WarningText = Warnings.Num() > 0 ? FText::Join(LOCTEXT("WarningSeparator", " / "), Warnings) : FText::GetEmpty();
	}

	for (const TSharedPtr<FQuickSDFMaskImportRowData>& Row : Rows)
	{
		if (!Row.IsValid() || !Row->bHasSource)
		{
			continue;
		}

		if (Row->ExistingSlotIndex != INDEX_NONE && Row->bAllowSourceTextureOverwrite && WritableSourceTargetCounts.FindRef(Row->WriteTargetPath) > 1)
		{
			Row->bWriteConflict = true;
			Row->WarningText = AppendWarningText(Row->WarningText, LOCTEXT("SharedWritableSourceWarning", "Shared writable source"));
		}
	}

	if (RowListView.IsValid())
	{
		RowListView->RequestListRefresh();
	}
}

void SQuickSDFMaskImportPanel::OnRowSelectionChanged(TSharedPtr<FQuickSDFMaskImportRowData> Item, ESelectInfo::Type SelectInfo)
{
}

TSharedPtr<FQuickSDFMaskImportRowData> SQuickSDFMaskImportPanel::GetSelectedRow() const
{
	if (!RowListView.IsValid())
	{
		return nullptr;
	}

	TArray<TSharedPtr<FQuickSDFMaskImportRowData>> SelectedRows = RowListView->GetSelectedItems();
	return SelectedRows.Num() > 0 ? SelectedRows[0] : nullptr;
}

void SQuickSDFMaskImportPanel::SelectDefaultRow()
{
	if (!RowListView.IsValid() || Rows.Num() == 0)
	{
		return;
	}

	UQuickSDFPaintTool* Tool = PaintTool.Get();
	const UQuickSDFToolProperties* Properties = Tool ? Tool->Properties.Get() : nullptr;
	if (Properties && Properties->TargetAngles.IsValidIndex(Properties->EditAngleIndex))
	{
		SelectRowClosestToAngle(Properties->TargetAngles[Properties->EditAngleIndex]);
		return;
	}

	RowListView->SetSelection(Rows[0], ESelectInfo::Direct);
	RowListView->RequestScrollIntoView(Rows[0]);
}

void SQuickSDFMaskImportPanel::SelectRowClosestToAngle(float Angle)
{
	if (!RowListView.IsValid() || Rows.Num() == 0)
	{
		return;
	}

	TSharedPtr<FQuickSDFMaskImportRowData> BestRow;
	float BestDistance = TNumericLimits<float>::Max();
	for (const TSharedPtr<FQuickSDFMaskImportRowData>& Row : Rows)
	{
		if (!Row.IsValid())
		{
			continue;
		}

		const float Distance = FMath::Abs(Row->Angle - Angle);
		if (Distance < BestDistance)
		{
			BestDistance = Distance;
			BestRow = Row;
		}
	}

	if (BestRow.IsValid())
	{
		RowListView->SetSelection(BestRow, ESelectInfo::Direct);
		RowListView->RequestScrollIntoView(BestRow);
	}
}

void SQuickSDFMaskImportPanel::PushUndoState()
{
	FQuickSDFMaskImportUndoState Snapshot;
	Snapshot.Sources = Sources;
	Snapshot.ExpectedCountOverride = ExpectedCountOverride;
	if (const TSharedPtr<FQuickSDFMaskImportRowData> SelectedRow = GetSelectedRow())
	{
		Snapshot.bHasSelectionAngle = true;
		Snapshot.SelectionAngle = SelectedRow->Angle;
	}

	UndoStack.Add(MoveTemp(Snapshot));
	constexpr int32 MaxUndoStates = 16;
	if (UndoStack.Num() > MaxUndoStates)
	{
		UndoStack.RemoveAt(0, UndoStack.Num() - MaxUndoStates);
	}
}

bool SQuickSDFMaskImportPanel::RestoreLastUndoState()
{
	if (UndoStack.Num() == 0)
	{
		return false;
	}

	FQuickSDFMaskImportUndoState Snapshot = MoveTemp(UndoStack.Last());
	UndoStack.Pop(EAllowShrinking::No);
	Sources = MoveTemp(Snapshot.Sources);
	ExpectedCountOverride = Snapshot.ExpectedCountOverride;

	RebuildRows();
	if (Snapshot.bHasSelectionAngle)
	{
		SelectRowClosestToAngle(Snapshot.SelectionAngle);
	}
	else
	{
		SelectDefaultRow();
	}
	if (RowListView.IsValid())
	{
		RowListView->RequestListRefresh();
	}
	return true;
}

bool SQuickSDFMaskImportPanel::CanUndoPreviewChange() const
{
	return UndoStack.Num() > 0;
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

bool SQuickSDFMaskImportPanel::HasNewSlotImportRows() const
{
	for (const TSharedPtr<FQuickSDFMaskImportRowData>& Row : Rows)
	{
		if (Row.IsValid() && Row->bHasSource && Row->ExistingSlotIndex == INDEX_NONE)
		{
			return true;
		}
	}
	return false;
}

bool SQuickSDFMaskImportPanel::HasWriteConflictRows() const
{
	for (const TSharedPtr<FQuickSDFMaskImportRowData>& Row : Rows)
	{
		if (IsApplicableImportRow(Row) && Row->bWriteConflict)
		{
			return true;
		}
	}
	return false;
}

bool SQuickSDFMaskImportPanel::CanApplyImport() const
{
	return HasImportableRows() && Rows.ContainsByPredicate(IsApplicableImportRow) && !HasWriteConflictRows();
}

FText SQuickSDFMaskImportPanel::GetUndoToolTipText() const
{
	return CanUndoPreviewChange()
		? LOCTEXT("UndoAddTooltip", "Undo the last preview edit. This does not affect imported assets.")
		: LOCTEXT("UndoAddDisabledTooltip", "There is no preview edit to undo.");
}

FText SQuickSDFMaskImportPanel::GetSelectedTargetText() const
{
	const TSharedPtr<FQuickSDFMaskImportRowData> SelectedRow = GetSelectedRow();
	if (!SelectedRow.IsValid())
	{
		return LOCTEXT("NoSelectedTarget", "Select a row, then choose a Texture2D in its Texture field.");
	}

	const FText AngleText = FText::AsNumber(SelectedRow->Angle, &FNumberFormattingOptions::DefaultNoGrouping());
	return SelectedRow->bHasSource
		? FText::Format(
			LOCTEXT("SelectedFilledTarget", "Selected target: Slot {0} / {1} deg / replace {2}"),
			FText::AsNumber(SelectedRow->SlotIndex + 1),
			AngleText,
			FText::FromString(GetSourceName(SelectedRow->Source)))
		: FText::Format(
			LOCTEXT("SelectedEmptyTarget", "Selected target: Slot {0} / {1} deg"),
			FText::AsNumber(SelectedRow->SlotIndex + 1),
			AngleText);
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

FText SQuickSDFMaskImportPanel::GetWriteSummaryText() const
{
	int32 WritableSourceCount = 0;
	for (const TSharedPtr<FQuickSDFMaskImportRowData>& Row : Rows)
	{
		if (!IsApplicableImportRow(Row))
		{
			continue;
		}
		if (Row->bAllowSourceTextureOverwrite)
		{
			++WritableSourceCount;
		}
	}

	if (WritableSourceCount == 0)
	{
		return LOCTEXT("WriteSummarySafeDefault", "Write: assets are assigned only unless write-back is enabled per row.");
	}
	return FText::Format(
		LOCTEXT("WriteSummary", "Write: {0} writable source textures"),
		FText::AsNumber(WritableSourceCount));
}

FSlateColor SQuickSDFMaskImportPanel::GetWriteSummaryColor() const
{
	for (const TSharedPtr<FQuickSDFMaskImportRowData>& Row : Rows)
	{
		if (IsApplicableImportRow(Row) && (Row->bAllowSourceTextureOverwrite || Row->bWriteConflict))
		{
			return FSlateColor(FLinearColor(1.0f, 0.72f, 0.28f, 1.0f));
		}
	}
	return FSlateColor(FLinearColor(0.58f, 0.58f, 0.58f, 1.0f));
}

FText SQuickSDFMaskImportPanel::GetApplyToolTipText() const
{
	if (!HasImportableRows())
	{
		return LOCTEXT("ApplyDisabledTooltip", "Add at least one Texture2D asset before applying.");
	}
	if (!Rows.ContainsByPredicate(IsApplicableImportRow))
	{
		return LOCTEXT("ApplyNoExistingSlotDisabledTooltip", "No source is assigned to an existing timeline slot. Choose a Texture2D in one of the row fields.");
	}
	if (HasWriteConflictRows())
	{
		return LOCTEXT("ApplyWriteConflictDisabledTooltip", "Resolve protected or shared writable source rows before applying.");
	}
	if (HasNewSlotImportRows())
	{
		return LOCTEXT("ApplyWithSkippedRowsTooltip", "Apply rows assigned to existing timeline slots. Rows marked Skipped will not be imported.");
	}
	return LOCTEXT("ApplyTooltip", "Assign the listed Texture2D assets to existing timeline slots.");
}

FReply SQuickSDFMaskImportPanel::OnUndoClicked()
{
	RestoreLastUndoState();
	return FReply::Handled();
}

FReply SQuickSDFMaskImportPanel::OnCompleteClicked()
{
	UQuickSDFPaintTool* Tool = PaintTool.Get();
	const UQuickSDFToolProperties* Properties = Tool ? Tool->Properties.Get() : nullptr;
	const bool bSymmetry = !Properties || Properties->UsesFrontHalfAngles();
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
	const bool bSymmetry = !Properties || Properties->UsesFrontHalfAngles();
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
	if (!Tool)
	{
		return FReply::Handled();
	}

	if (!Rows.ContainsByPredicate(IsApplicableImportRow))
	{
		FMessageDialog::Open(
			EAppMsgType::Ok,
			LOCTEXT("ApplyNoExistingSlotBlockedMessage", "No source is assigned to an existing timeline slot. Choose a Texture2D in one of the row fields."));
		return FReply::Handled();
	}
	if (HasWriteConflictRows())
	{
		FMessageDialog::Open(
			EAppMsgType::Ok,
			LOCTEXT("ApplyWriteConflictBlockedMessage", "Some rows are protected or share a writable source texture. Resolve those rows before applying."));
		return FReply::Handled();
	}

	TArray<UTexture2D*> Textures;
	TArray<TSharedPtr<FQuickSDFMaskImportRowData>> ImportRows;
	int32 SkippedNewSlotCount = 0;

	for (const TSharedPtr<FQuickSDFMaskImportRowData>& Row : Rows)
	{
		if (!Row.IsValid() || !Row->bHasSource)
		{
			continue;
		}
		if (Row->ExistingSlotIndex == INDEX_NONE)
		{
			++SkippedNewSlotCount;
			continue;
		}

		UTexture2D* Texture = Row->Source.Texture.Get();
		if (Texture)
		{
			Textures.Add(Texture);
			ImportRows.Add(Row);
		}
	}

	if (Textures.Num() == 0)
	{
		return FReply::Handled();
	}

	bool bAllAssigned = true;
	for (int32 Index = 0; Index < Textures.Num(); ++Index)
	{
		bAllAssigned &= Tool->AssignMaskTextureToAngle(ImportRows[Index]->ExistingSlotIndex, Textures[Index], ImportRows[Index]->bAllowSourceTextureOverwrite);
	}

	if (bAllAssigned)
	{
		FNotificationInfo Info(FText::Format(
			LOCTEXT("SlotImportCompleteNotification", "Assigned {0} masks / Skipped {1}"),
			FText::AsNumber(Textures.Num()),
			FText::AsNumber(SkippedNewSlotCount)));
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
