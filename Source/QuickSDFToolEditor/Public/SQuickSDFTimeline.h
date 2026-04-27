#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"

class UQuickSDFPaintTool;

DECLARE_DELEGATE_OneParam(FOnKeyframeAngleChanged, float /*NewAngle*/);

class SQuickSDFTimelineKeyframe : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SQuickSDFTimelineKeyframe) {}
		SLATE_ARGUMENT(int32, Index)
		SLATE_ATTRIBUTE(float, Angle)
		SLATE_ATTRIBUTE(bool, bIsActive)
		SLATE_ATTRIBUTE(bool, bSnapEnabled)
		SLATE_EVENT(FOnKeyframeAngleChanged, OnAngleChanged)
		SLATE_EVENT(FSimpleDelegate, OnClicked)
		SLATE_EVENT(FSimpleDelegate, OnDragStarted)
		SLATE_EVENT(FSimpleDelegate, OnDragEnded)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	virtual FReply OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseMove(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FCursorReply OnCursorQuery(const FGeometry& MyGeometry, const FPointerEvent& CursorEvent) const override;

private:
	int32 Index = 0;
	TAttribute<float> Angle;
	TAttribute<bool> bIsActive;
	TAttribute<bool> bSnapEnabled;
	bool bIsDragging = false;
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

private:
	// UI Generation
	void RebuildTimeline();
	FReply OnAddKeyframeClicked();
	FReply OnDeleteKeyframeClicked();
	void OnKeyframeClicked(int32 Index);
	void OnKeyframeAngleChanged(float NewAngle, int32 Index);
	void OnKeyframeDragStarted();
	void OnKeyframeDragEnded();

	ECheckBoxState IsGridSnapEnabled() const;
	void OnGridSnapStateChanged(ECheckBoxState NewState);

	// State
	UQuickSDFPaintTool* GetActivePaintTool() const;
	float GetCurrentLightYaw() const;
	
	// Caching
	int32 CachedNumAngles = -1;
	int32 CachedEditAngleIndex = -1;
	TArray<float> CachedAngles;
	TArray<UTexture2D*> CachedTextures;
	bool bGridSnapEnabled = true;

	// Widget refs
	TSharedPtr<class SCanvas> TimelineTrackCanvas;
};

