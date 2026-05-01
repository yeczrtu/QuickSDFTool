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
	FString SourcePath;
	FString DisplayName;
	TWeakObjectPtr<UTexture2D> Texture;
	int32 Width = 0;
	int32 Height = 0;
};

struct FQuickSDFMaskImportRowData;

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

private:
	TSharedRef<ITableRow> GenerateRow(TSharedPtr<FQuickSDFMaskImportRowData> Item, const TSharedRef<STableViewBase>& OwnerTable);
	FReply OnAddFilesClicked();
	FReply OnAddAssetsClicked();
	FReply OnApplyClicked();
	FReply OnCancelClicked();
	FReply OnCompleteClicked();
	FReply OnEvenClicked();
	void OnAssetPicked(const FAssetData& AssetData);
	void AddSources(const TArray<FQuickSDFMaskImportSource>& NewSources);
	void RebuildRows();
	bool HasImportableRows() const;
	FText GetSummaryText() const;
	FText GetApplyToolTipText() const;
	FText GetImportedFolderText() const;

	TWeakObjectPtr<UQuickSDFPaintTool> PaintTool;
	TArray<FQuickSDFMaskImportSource> Sources;
	TArray<TSharedPtr<FQuickSDFMaskImportRowData>> Rows;
	TSharedPtr<SListView<TSharedPtr<FQuickSDFMaskImportRowData>>> RowListView;
	FSimpleDelegate OnClosed;
	int32 ExpectedCountOverride = INDEX_NONE;
};
