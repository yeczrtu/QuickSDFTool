#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"

class UQuickSDFPaintTool;

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
	TSharedRef<SWidget> GenerateTimelineSlots();
	FReply OnAddKeyframeClicked();
	FReply OnDeleteKeyframeClicked();
	FReply OnKeyframeClicked(int32 Index);

	// State
	UQuickSDFPaintTool* GetActivePaintTool() const;
	
	// Caching
	int32 CachedNumAngles = -1;
	int32 CachedEditAngleIndex = -1;
	TArray<UTexture2D*> CachedTextures;

	// Widget refs
	TSharedPtr<SHorizontalBox> TimelineTrackBox;
};
