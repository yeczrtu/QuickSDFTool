#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Views/SListView.h"

class UQuickSDFPaintTool;
class UTexture2D;
struct FAssetData;

struct FQuickSDFMaskImportSource
{
	FGuid ImportGuid = FGuid::NewGuid();
	bool bHasForcedAngle = false;
	bool bAllowSourceTextureOverwrite = false;
	FString DisplayName;
	TWeakObjectPtr<UTexture2D> Texture;
	float ForcedAngle = 0.0f;
	int32 Width = 0;
	int32 Height = 0;
};

struct FQuickSDFMaskImportRowData;

struct FQuickSDFMaskImportUndoState
{
	TArray<FQuickSDFMaskImportSource> Sources;
	int32 ExpectedCountOverride = INDEX_NONE;
	bool bHasSelectionAngle = false;
	float SelectionAngle = 0.0f;
};

class SQuickSDFMaskImportPanel : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SQuickSDFMaskImportPanel) {}
		SLATE_ARGUMENT(TWeakObjectPtr<UQuickSDFPaintTool>, PaintTool)
		SLATE_ARGUMENT(TArray<FQuickSDFMaskImportSource>, Sources)
		SLATE_EVENT(FSimpleDelegate, OnClosed)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	void RefreshValidation();
	void SetRowTexture(TSharedPtr<FQuickSDFMaskImportRowData> Row, const FAssetData& AssetData);
	void SetRowWriteChecked(TSharedPtr<FQuickSDFMaskImportRowData> Row, bool bChecked);

	virtual FReply OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseMove(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseWheel(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnDragOver(const FGeometry& MyGeometry, const FDragDropEvent& DragDropEvent) override;
	virtual FReply OnDrop(const FGeometry& MyGeometry, const FDragDropEvent& DragDropEvent) override;
	virtual bool SupportsKeyboardFocus() const override;
	virtual FReply OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent) override;

private:
	TSharedRef<ITableRow> GenerateRow(TSharedPtr<FQuickSDFMaskImportRowData> Item, const TSharedRef<STableViewBase>& OwnerTable);
	FReply OnUndoClicked();
	FReply OnApplyClicked();
	FReply OnCancelClicked();
	FReply OnCompleteClicked();
	FReply OnEvenClicked();
	void AddSources(const TArray<FQuickSDFMaskImportSource>& NewSources);
	void AddSourcesFromUserAction(const TArray<FQuickSDFMaskImportSource>& NewSources);
	void AddSingleSourceToSelectedRow(FQuickSDFMaskImportSource Source, const TSharedPtr<FQuickSDFMaskImportRowData>& SelectedRow);
	void AddMultipleSourcesFromSelectedRow(TArray<FQuickSDFMaskImportSource> NewSources, const TSharedPtr<FQuickSDFMaskImportRowData>& SelectedRow);
	void ClearSourceFromRow(const TSharedPtr<FQuickSDFMaskImportRowData>& Row);
	int32 FindSourceIndex(const FQuickSDFMaskImportSource& Source) const;
	float GetFallbackAssignmentStep() const;
	void RebuildRows();
	void OnRowSelectionChanged(TSharedPtr<FQuickSDFMaskImportRowData> Item, ESelectInfo::Type SelectInfo);
	TSharedPtr<FQuickSDFMaskImportRowData> GetSelectedRow() const;
	void SelectDefaultRow();
	void SelectRowClosestToAngle(float Angle);
	void PushUndoState();
	bool RestoreLastUndoState();
	bool CanUndoPreviewChange() const;
	bool HasImportableRows() const;
	bool HasNewSlotImportRows() const;
	bool HasWriteConflictRows() const;
	bool CanApplyImport() const;
	FText GetUndoToolTipText() const;
	FText GetSelectedTargetText() const;
	FText GetSummaryText() const;
	FText GetWriteSummaryText() const;
	FSlateColor GetWriteSummaryColor() const;
	FText GetApplyToolTipText() const;

	TWeakObjectPtr<UQuickSDFPaintTool> PaintTool;
	TArray<FQuickSDFMaskImportSource> Sources;
	TArray<TSharedPtr<FQuickSDFMaskImportRowData>> Rows;
	TArray<FQuickSDFMaskImportUndoState> UndoStack;
	TSharedPtr<SListView<TSharedPtr<FQuickSDFMaskImportRowData>>> RowListView;
	FSimpleDelegate OnClosed;
	int32 ExpectedCountOverride = INDEX_NONE;
};
