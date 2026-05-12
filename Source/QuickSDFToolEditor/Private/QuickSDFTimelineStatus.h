#pragma once

#include "CoreMinimal.h"
#include "QuickSDFToolProperties.h"

struct FQuickSDFTimelineKeySegment
{
	int32 KeyIndex = INDEX_NONE;
	int32 VisualIndex = INDEX_NONE;
	float Angle = 0.0f;
	float LeftAngle = 0.0f;
	float RightAngle = 0.0f;
	bool bInPaintTargetRange = false;
};

struct FQuickSDFTimelineRangeStatus
{
	float MaxAngle = 180.0f;
	int32 ActiveKeyIndex = INDEX_NONE;
	int32 ActiveVisualIndex = INDEX_NONE;
	EQuickSDFPaintTargetMode PaintTargetMode = EQuickSDFPaintTargetMode::CurrentOnly;
	TArray<int32> VisibleKeyIndices;
	TArray<FQuickSDFTimelineKeySegment> Segments;
	bool bHasTargetRange = false;
	float TargetRangeLeftAngle = 0.0f;
	float TargetRangeRightAngle = 0.0f;

	bool IsKeyInTargetRange(int32 KeyIndex) const;
	const FQuickSDFTimelineKeySegment* FindSegmentByKeyIndex(int32 KeyIndex) const;
};

struct FQuickSDFTimelineKeyStatusInput
{
	int32 KeyIndex = INDEX_NONE;
	float Angle = 0.0f;
	float GlobalAngleOffset = 0.0f;
	float AngleOffsetDelta = 0.0f;
	float EffectivePreviewAngle = 0.0f;
	float MinPreviewAngle = 0.0f;
	float MaxPreviewAngle = 180.0f;
	bool bVisible = true;
	bool bIsActive = false;
	bool bInPaintTargetRange = false;
	bool bHasTextureMask = false;
	bool bHasPaintRenderTarget = false;
	bool bAllowSourceTextureOverwrite = false;
	bool bGuardEnabled = false;
	bool bHasUnbakedVectorLayer = false;
	FString TextureName;
};

struct FQuickSDFTimelineKeyStatus
{
	int32 KeyIndex = INDEX_NONE;
	float Angle = 0.0f;
	float GlobalAngleOffset = 0.0f;
	float AngleOffsetDelta = 0.0f;
	float OffsetPreviewAngle = 0.0f;
	float EffectivePreviewAngle = 0.0f;
	float MinPreviewAngle = 0.0f;
	float MaxPreviewAngle = 180.0f;
	bool bVisible = true;
	bool bIsActive = false;
	bool bInPaintTargetRange = false;
	bool bHasMask = false;
	bool bAllowSourceTextureOverwrite = false;
	bool bGuardEnabled = false;
	bool bHasUnbakedVectorLayer = false;
	FString TextureName;
};

struct FQuickSDFTimelineOffsetVisual
{
	float AuthoredPercent = 0.0f;
	float EffectivePercent = 0.0f;
	float LeftPercent = 0.0f;
	float WidthPercent = 0.0f;
	bool bVisible = false;
	bool bMovesForward = false;
};

namespace QuickSDFTimelineStatus
{
EQuickSDFPaintTargetMode ResolvePaintTargetMode(EQuickSDFPaintTargetMode PaintTargetMode, bool bPaintAllAngles);
TArray<int32> MakeVisibleSortedKeyIndices(const TArray<float>& Angles, bool bSymmetryMode);
float NormalizeAngleToTimelinePercent(float Angle, float MaxAngle);
bool ShouldShowOffsetVisual(float AngleOffsetDelta);
FQuickSDFTimelineOffsetVisual BuildOffsetVisual(float AuthoredAngle, float EffectivePreviewAngle, float AngleOffsetDelta, float MaxAngle);
FQuickSDFTimelineRangeStatus BuildRangeStatus(
	const TArray<float>& Angles,
	int32 ActiveKeyIndex,
	EQuickSDFPaintTargetMode PaintTargetMode,
	bool bPaintAllAngles,
	bool bSymmetryMode);
FQuickSDFTimelineKeyStatus BuildKeyStatus(const FQuickSDFTimelineKeyStatusInput& Input);
FText BuildKeyTooltip(const FQuickSDFTimelineKeyStatus& Status);
}
