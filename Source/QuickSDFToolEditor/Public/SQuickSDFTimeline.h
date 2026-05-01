#pragma once

#include "CoreMinimal.h"
#include "Input/DragAndDrop.h"
#include "SQuickSDFMaskImportPanel.h"
#include "UObject/StrongObjectPtr.h"
#include "Widgets/SCompoundWidget.h"

class UQuickSDFPaintTool;
class UTexture2D;

DECLARE_DELEGATE_OneParam(FOnKeyframeAngleChanged, float /*NewAngle*/);

class SQuickSDFTimelineKeyframe : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SQuickSDFTimelineKeyframe) {}
		SLATE_ARGUMENT(int32, Index)
		SLATE_ATTRIBUTE(float, Angle)
		SLATE_ATTRIBUTE(bool, bIsActive)
		SLATE_ATTRIBUTE(bool, bSnapEnabled)
		SLATE_ATTRIBUTE(bool, bSymmetryMode)
		SLATE_ATTRIBUTE(bool, bAllowSourceTextureOverwrite)
		SLATE_ATTRIBUTE(FSlateBrush*, TextureBrush)
		SLATE_EVENT(FOnKeyframeAngleChanged, OnAngleChanged)
		SLATE_EVENT(FSimpleDelegate, OnClicked)
		SLATE_EVENT(FSimpleDelegate, OnDragStarted)
		SLATE_EVENT(FSimpleDelegate, OnDragEnded)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	virtual FReply OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseMove(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual void OnMouseCaptureLost(const FCaptureLostEvent& CaptureLostEvent) override;
	virtual FCursorReply OnCursorQuery(const FGeometry& MyGeometry, const FPointerEvent& CursorEvent) const override;

private:
	int32 Index = 0;
	TAttribute<float> Angle;
	TAttribute<bool> bIsActive;
	TAttribute<bool> bSnapEnabled;
	TAttribute<bool> bSymmetryMode;
	TAttribute<bool> bAllowSourceTextureOverwrite;
	TAttribute<FSlateBrush*> TextureBrush;
	bool bIsMouseDown = false;
	bool bIsDragging = false;
	FVector2D MouseDownScreenPosition = FVector2D::ZeroVector;
	FOnKeyframeAngleChanged OnAngleChanged;
	FSimpleDelegate OnClicked;
	FSimpleDelegate OnDragStarted;
	FSimpleDelegate OnDragEnded;
};

/**
 * A timeline-style Slate widget that allows users to select, add, and remove 
 * textures/angles for the QuickSDFTool, similar to an animation timeline.
 */
class SQuickSDFTimeline : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SQuickSDFTimeline) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;
	virtual FReply OnDragOver(const FGeometry& MyGeometry, const FDragDropEvent& DragDropEvent) override;
	virtual FReply OnDrop(const FGeometry& MyGeometry, const FDragDropEvent& DragDropEvent) override;
	virtual FReply OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseMove(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual void OnMouseCaptureLost(const FCaptureLostEvent& CaptureLostEvent) override;

public:
	UQuickSDFPaintTool* GetActivePaintTool() const;
	float GetCurrentLightYaw() const;
	void SetSeekAngle(float Angle);
	ECheckBoxState IsSymmetryModeEnabled() const;
	void OnSymmetryModeStateChanged(ECheckBoxState NewState);
	ECheckBoxState IsGridSnapEnabled() const;
	void OnGridSnapStateChanged(ECheckBoxState NewState);

private:
	// UI Generation
	void RebuildTimeline();
	FReply OnAddKeyframeClicked();
	FReply OnDuplicateKeyframeClicked();
	FReply OnDeleteKeyframeClicked();
	FReply OnImportClicked();
	FReply OnCompleteToEightClicked();
	FReply OnRedistributeEvenlyClicked();
	void OnKeyframeClicked(int32 Index);
	FReply OnSyncLightClicked();
	void OnKeyframeAngleChanged(float NewAngle, int32 Index);
	void OnKeyframeDragStarted();
	void OnKeyframeDragEnded();
	bool IsTimelineTrackUnderCursor(const FVector2D& ScreenPosition) const;
	bool IsNearKeyframeHandle(const FVector2D& ScreenPosition) const;
	int32 FindKeyframeAtScreenPosition(const FVector2D& ScreenPosition) const;
	void SeekTimelineAtScreenPosition(const FVector2D& ScreenPosition);
	void OpenImportPanel(const TArray<FQuickSDFMaskImportSource>& Sources);
	void CloseImportPanel();
	TArray<FQuickSDFMaskImportSource> MakeImportSourcesFromTextures(const TArray<UTexture2D*>& Textures) const;
	TArray<FQuickSDFMaskImportSource> MakeImportSourcesFromFiles(const TArray<FString>& Filenames) const;
	float GetCurrentSeekAngle() const;
	float ResolveTimelineActionAngle() const;
	EVisibility GetRefineVisibility() const;
	EVisibility GetCompactVisibility() const;
	FText GetHeaderStatusText() const;
	FText GetCompactSummaryText() const;
	FText GetCompleteMaskButtonText() const;
	FText GetCompleteMaskTooltipText() const;

	// Caching
	int32 CachedNumAngles = -1;
	int32 CachedEditAngleIndex = -1;
	TArray<float> CachedAngles;
	TArray<UTexture2D*> CachedTextures;
	TArray<TSharedPtr<FSlateBrush>> KeyframeBrushes;
	TArray<TStrongObjectPtr<UTexture2D>> ThumbnailTextures;
	int32 CachedMaskRevision = INDEX_NONE;
	bool bGridSnapEnabled = false;
	bool bSeekingTimeline = false;
	bool bTimelineDragTransactionOpen = false;
	bool bHasSeekAngle = false;
	bool bImportPanelOpen = false;
	float LastSeekAngle = 0.0f;

	// Widget refs
	TSharedPtr<class SCanvas> TimelineTrackCanvas;
	TSharedPtr<class SBox> ImportPanelBox;
};

