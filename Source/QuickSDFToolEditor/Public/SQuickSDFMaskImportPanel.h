#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Views/SListView.h"

class UQuickSDFPaintTool;
class UTexture2D;
struct FAssetData;

struct FQuickSDFMaskImportSource
{
	bool bExternalFile = false;
	bool bHasForcedAngle = false;
	FString SourcePath;
	FString DisplayName;
	TWeakObjectPtr<UTexture2D> Texture;
	float ForcedAngle = 0.0f;
	int32 Width = 0;
	int32 Height = 0;
};

struct FQuickSDFMaskImportRowData;
struct FQuickSDFAssetPickerState;

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
	FReply OnAddFilesClicked();
	FReply OnAddAssetsClicked();
	FReply OnAddSelectedAssetsClicked();
	FReply OnUndoClicked();
	FReply OnApplyClicked();
	FReply OnCancelClicked();
	FReply OnCompleteClicked();
	FReply OnEvenClicked();
	void OnAssetPicked(const FAssetData& AssetData);
	void AddAssetDataSelection(const TArray<FAssetData>& AssetDataList);
	void AddSources(const TArray<FQuickSDFMaskImportSource>& NewSources);
	void AddSourcesFromUserAction(const TArray<FQuickSDFMaskImportSource>& NewSources);
	void AddSingleSourceToSelectedRow(FQuickSDFMaskImportSource Source, const TSharedPtr<FQuickSDFMaskImportRowData>& SelectedRow);
	void AddMultipleSourcesFromSelectedRow(TArray<FQuickSDFMaskImportSource> NewSources, const TSharedPtr<FQuickSDFMaskImportRowData>& SelectedRow);
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
	bool CanApplyImport() const;
	FText GetUndoToolTipText() const;
	FText GetSelectedTargetText() const;
	FText GetSummaryText() const;
	FText GetApplyToolTipText() const;
	FText GetImportedFolderText() const;

	TWeakObjectPtr<UQuickSDFPaintTool> PaintTool;
	TArray<FQuickSDFMaskImportSource> Sources;
	TArray<TSharedPtr<FQuickSDFMaskImportRowData>> Rows;
	TArray<FQuickSDFMaskImportUndoState> UndoStack;
	TSharedPtr<SListView<TSharedPtr<FQuickSDFMaskImportRowData>>> RowListView;
	TSharedPtr<FQuickSDFAssetPickerState> AssetPickerState;
	FSimpleDelegate OnClosed;
	int32 ExpectedCountOverride = INDEX_NONE;
};
