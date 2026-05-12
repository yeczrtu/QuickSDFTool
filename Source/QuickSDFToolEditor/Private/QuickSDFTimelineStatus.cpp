#include "QuickSDFTimelineStatus.h"

bool FQuickSDFTimelineRangeStatus::IsKeyInTargetRange(int32 KeyIndex) const
{
	if (const FQuickSDFTimelineKeySegment* Segment = FindSegmentByKeyIndex(KeyIndex))
	{
		return Segment->bInPaintTargetRange;
	}
	return false;
}

float FQuickSDFTimelineRangeStatus::GetKeyRadiusScale(int32 KeyIndex) const
{
	if (const FQuickSDFTimelineKeySegment* Segment = FindSegmentByKeyIndex(KeyIndex))
	{
		return Segment->RadiusScale;
	}
	return 0.0f;
}

const FQuickSDFTimelineKeySegment* FQuickSDFTimelineRangeStatus::FindSegmentByKeyIndex(int32 KeyIndex) const
{
	return Segments.FindByPredicate([KeyIndex](const FQuickSDFTimelineKeySegment& Segment)
	{
		return Segment.KeyIndex == KeyIndex;
	});
}

namespace QuickSDFTimelineStatus
{
namespace
{
FString GetApplyModeLabel(EQuickSDFApplyMode Mode)
{
	switch (Mode)
	{
	case EQuickSDFApplyMode::Single:
		return TEXT("Single");
	case EQuickSDFApplyMode::SolidRange:
		return TEXT("Solid Range");
	case EQuickSDFApplyMode::GradientRange:
		return TEXT("Gradient Range");
	default:
		return TEXT("Unknown");
	}
}

FString GetApplyDirectionLabel(EQuickSDFApplyDirection Direction)
{
	switch (Direction)
	{
	case EQuickSDFApplyDirection::Both:
		return TEXT("Both");
	case EQuickSDFApplyDirection::Before:
		return TEXT("Before");
	case EQuickSDFApplyDirection::After:
		return TEXT("After");
	default:
		return TEXT("Unknown");
	}
}

float EvaluateGradientRadiusScale(const FRuntimeFloatCurve* GradientCurve, float NormalizedDistance)
{
	const float ClampedDistance = FMath::Clamp(NormalizedDistance, 0.0f, 1.0f);
	const float DefaultScale = 1.0f - ClampedDistance;
	const FRichCurve* Curve = GradientCurve ? GradientCurve->GetRichCurveConst() : nullptr;
	const float Scale = Curve && Curve->GetNumKeys() > 0
		? Curve->Eval(ClampedDistance, DefaultScale)
		: DefaultScale;
	return FMath::Clamp(Scale, 0.0f, 1.0f);
}

EQuickSDFPaintTargetMode MakeLegacyPaintTargetMode(EQuickSDFApplyMode ApplyMode, EQuickSDFApplyDirection ApplyDirection)
{
	if (ApplyMode == EQuickSDFApplyMode::Single)
	{
		return EQuickSDFPaintTargetMode::CurrentOnly;
	}

	switch (ApplyDirection)
	{
	case EQuickSDFApplyDirection::Before:
		return EQuickSDFPaintTargetMode::BeforeCurrent;
	case EQuickSDFApplyDirection::After:
		return EQuickSDFPaintTargetMode::AfterCurrent;
	case EQuickSDFApplyDirection::Both:
	default:
		return EQuickSDFPaintTargetMode::All;
	}
}
}

EQuickSDFPaintTargetMode ResolvePaintTargetMode(EQuickSDFPaintTargetMode PaintTargetMode, bool bPaintAllAngles)
{
	if (bPaintAllAngles && PaintTargetMode == EQuickSDFPaintTargetMode::CurrentOnly)
	{
		return EQuickSDFPaintTargetMode::All;
	}
	return PaintTargetMode;
}

TArray<int32> MakeVisibleSortedKeyIndices(const TArray<float>& Angles, bool bSymmetryMode)
{
	const float MaxAngle = bSymmetryMode ? 90.0f : 180.0f;
	TArray<int32> Indices;
	for (int32 Index = 0; Index < Angles.Num(); ++Index)
	{
		if (!bSymmetryMode || Angles[Index] <= MaxAngle)
		{
			Indices.Add(Index);
		}
	}

	Indices.Sort([&Angles](int32 A, int32 B)
	{
		return Angles[A] < Angles[B];
	});
	return Indices;
}

float NormalizeAngleToTimelinePercent(float Angle, float MaxAngle)
{
	if (MaxAngle <= KINDA_SMALL_NUMBER)
	{
		return 0.0f;
	}

	return FMath::Clamp(Angle / MaxAngle, 0.0f, 1.0f);
}

bool ShouldShowOffsetVisual(float AngleOffsetDelta)
{
	return !FMath::IsNearlyZero(AngleOffsetDelta, 0.01f);
}

FQuickSDFTimelineOffsetVisual BuildOffsetVisual(float AuthoredAngle, float EffectivePreviewAngle, float AngleOffsetDelta, float MaxAngle)
{
	FQuickSDFTimelineOffsetVisual Visual;
	Visual.AuthoredPercent = NormalizeAngleToTimelinePercent(AuthoredAngle, MaxAngle);
	Visual.EffectivePercent = NormalizeAngleToTimelinePercent(EffectivePreviewAngle, MaxAngle);
	Visual.LeftPercent = FMath::Min(Visual.AuthoredPercent, Visual.EffectivePercent);
	Visual.WidthPercent = FMath::Abs(Visual.EffectivePercent - Visual.AuthoredPercent);
	Visual.bVisible = ShouldShowOffsetVisual(AngleOffsetDelta);
	Visual.bMovesForward = Visual.EffectivePercent >= Visual.AuthoredPercent;
	return Visual;
}

FQuickSDFTimelineRangeStatus BuildRangeStatus(
	const TArray<float>& Angles,
	int32 ActiveKeyIndex,
	EQuickSDFApplyMode ApplyMode,
	EQuickSDFApplyDirection ApplyDirection,
	const FRuntimeFloatCurve* GradientCurve,
	bool bSymmetryMode)
{
	FQuickSDFTimelineRangeStatus Status;
	Status.MaxAngle = bSymmetryMode ? 90.0f : 180.0f;
	Status.ActiveKeyIndex = Angles.IsValidIndex(ActiveKeyIndex) ? ActiveKeyIndex : INDEX_NONE;
	Status.ApplyMode = ApplyMode;
	Status.ApplyDirection = ApplyDirection;
	Status.PaintTargetMode = MakeLegacyPaintTargetMode(ApplyMode, ApplyDirection);
	Status.VisibleKeyIndices = MakeVisibleSortedKeyIndices(Angles, bSymmetryMode);
	Status.ActiveVisualIndex = Status.VisibleKeyIndices.IndexOfByKey(Status.ActiveKeyIndex);

	const bool bHasActiveVisualIndex = Status.ActiveVisualIndex != INDEX_NONE;
	int32 TargetStartVisualIndex = INDEX_NONE;
	int32 TargetEndVisualIndex = INDEX_NONE;
	if (bHasActiveVisualIndex)
	{
		if (ApplyMode == EQuickSDFApplyMode::Single)
		{
			TargetStartVisualIndex = Status.ActiveVisualIndex;
			TargetEndVisualIndex = Status.ActiveVisualIndex;
		}
		else
		{
			TargetStartVisualIndex = ApplyDirection == EQuickSDFApplyDirection::After ? Status.ActiveVisualIndex : 0;
			TargetEndVisualIndex = ApplyDirection == EQuickSDFApplyDirection::Before ? Status.ActiveVisualIndex : Status.VisibleKeyIndices.Num() - 1;
		}
	}

	const float ActiveAngle = bHasActiveVisualIndex
		? FMath::Clamp(Angles[Status.ActiveKeyIndex], 0.0f, Status.MaxAngle)
		: 0.0f;
	const float BeforeMaxDistance = bHasActiveVisualIndex && TargetStartVisualIndex < Status.ActiveVisualIndex
		? FMath::Abs(ActiveAngle - FMath::Clamp(Angles[Status.VisibleKeyIndices[TargetStartVisualIndex]], 0.0f, Status.MaxAngle))
		: 0.0f;
	const float AfterMaxDistance = bHasActiveVisualIndex && TargetEndVisualIndex > Status.ActiveVisualIndex
		? FMath::Abs(FMath::Clamp(Angles[Status.VisibleKeyIndices[TargetEndVisualIndex]], 0.0f, Status.MaxAngle) - ActiveAngle)
		: 0.0f;

	for (int32 VisualIndex = 0; VisualIndex < Status.VisibleKeyIndices.Num(); ++VisualIndex)
	{
		const int32 KeyIndex = Status.VisibleKeyIndices[VisualIndex];
		const float CurrentAngle = FMath::Clamp(Angles[KeyIndex], 0.0f, Status.MaxAngle);
		const float PreviousAngle = VisualIndex == 0
			? 0.0f
			: FMath::Clamp(Angles[Status.VisibleKeyIndices[VisualIndex - 1]], 0.0f, Status.MaxAngle);
		const float NextAngle = VisualIndex == Status.VisibleKeyIndices.Num() - 1
			? Status.MaxAngle
			: FMath::Clamp(Angles[Status.VisibleKeyIndices[VisualIndex + 1]], 0.0f, Status.MaxAngle);

		FQuickSDFTimelineKeySegment Segment;
		Segment.KeyIndex = KeyIndex;
		Segment.VisualIndex = VisualIndex;
		Segment.Angle = CurrentAngle;
		Segment.LeftAngle = VisualIndex == 0 ? 0.0f : (PreviousAngle + CurrentAngle) * 0.5f;
		Segment.RightAngle = VisualIndex == Status.VisibleKeyIndices.Num() - 1 ? Status.MaxAngle : (CurrentAngle + NextAngle) * 0.5f;

		if (TargetStartVisualIndex != INDEX_NONE && TargetEndVisualIndex != INDEX_NONE)
		{
			Segment.bInPaintTargetRange = VisualIndex >= TargetStartVisualIndex && VisualIndex <= TargetEndVisualIndex;
			if (Segment.bInPaintTargetRange)
			{
				Segment.RadiusScale = 1.0f;
				if (ApplyMode == EQuickSDFApplyMode::GradientRange)
				{
					const float Distance = FMath::Abs(CurrentAngle - ActiveAngle);
					const float MaxDistance = CurrentAngle <= ActiveAngle ? BeforeMaxDistance : AfterMaxDistance;
					Segment.NormalizedDistance = MaxDistance > KINDA_SMALL_NUMBER
						? FMath::Clamp(Distance / MaxDistance, 0.0f, 1.0f)
						: 0.0f;
					Segment.RadiusScale = EvaluateGradientRadiusScale(GradientCurve, Segment.NormalizedDistance);
				}
			}
		}

		if (Segment.bInPaintTargetRange)
		{
			if (!Status.bHasTargetRange)
			{
				Status.TargetRangeLeftAngle = Segment.LeftAngle;
				Status.TargetRangeRightAngle = Segment.RightAngle;
				Status.bHasTargetRange = true;
			}
			else
			{
				Status.TargetRangeLeftAngle = FMath::Min(Status.TargetRangeLeftAngle, Segment.LeftAngle);
				Status.TargetRangeRightAngle = FMath::Max(Status.TargetRangeRightAngle, Segment.RightAngle);
			}
		}

		Status.Segments.Add(Segment);
	}

	return Status;
}

FQuickSDFTimelineRangeStatus BuildRangeStatus(
	const TArray<float>& Angles,
	int32 ActiveKeyIndex,
	EQuickSDFPaintTargetMode PaintTargetMode,
	bool bPaintAllAngles,
	bool bSymmetryMode)
{
	const EQuickSDFPaintTargetMode ResolvedMode = ResolvePaintTargetMode(PaintTargetMode, bPaintAllAngles);
	EQuickSDFApplyMode ApplyMode = EQuickSDFApplyMode::Single;
	EQuickSDFApplyDirection ApplyDirection = EQuickSDFApplyDirection::Both;
	switch (ResolvedMode)
	{
	case EQuickSDFPaintTargetMode::All:
		ApplyMode = EQuickSDFApplyMode::SolidRange;
		ApplyDirection = EQuickSDFApplyDirection::Both;
		break;
	case EQuickSDFPaintTargetMode::BeforeCurrent:
		ApplyMode = EQuickSDFApplyMode::SolidRange;
		ApplyDirection = EQuickSDFApplyDirection::Before;
		break;
	case EQuickSDFPaintTargetMode::AfterCurrent:
		ApplyMode = EQuickSDFApplyMode::SolidRange;
		ApplyDirection = EQuickSDFApplyDirection::After;
		break;
	case EQuickSDFPaintTargetMode::CurrentOnly:
	default:
		break;
	}

	return BuildRangeStatus(Angles, ActiveKeyIndex, ApplyMode, ApplyDirection, nullptr, bSymmetryMode);
}

FQuickSDFTimelineKeyStatus BuildKeyStatus(const FQuickSDFTimelineKeyStatusInput& Input)
{
	FQuickSDFTimelineKeyStatus Status;
	Status.KeyIndex = Input.KeyIndex;
	Status.Angle = Input.Angle;
	Status.GlobalAngleOffset = Input.GlobalAngleOffset;
	Status.AngleOffsetDelta = Input.AngleOffsetDelta;
	Status.EffectivePreviewAngle = Input.EffectivePreviewAngle;
	Status.MinPreviewAngle = Input.MinPreviewAngle;
	Status.MaxPreviewAngle = Input.MaxPreviewAngle;
	Status.bVisible = Input.bVisible;
	Status.bIsActive = Input.bIsActive;
	Status.bInPaintTargetRange = Input.bInPaintTargetRange;
	Status.ApplyMode = Input.ApplyMode;
	Status.ApplyDirection = Input.ApplyDirection;
	Status.RadiusScale = Input.RadiusScale;
	Status.bHasMask = Input.bHasTextureMask || Input.bHasPaintRenderTarget;
	Status.bAllowSourceTextureOverwrite = Input.bAllowSourceTextureOverwrite;
	Status.bGuardEnabled = Input.bGuardEnabled;
	Status.bHasUnbakedVectorLayer = Input.bHasUnbakedVectorLayer;
	Status.TextureName = Input.TextureName;
	return Status;
}

FText BuildKeyTooltip(const FQuickSDFTimelineKeyStatus& Status)
{
	TArray<FString> Lines;
	Lines.Add(FString::Printf(TEXT("Authored Angle: %.0f deg"), Status.Angle));
	Lines.Add(FString::Printf(TEXT("Global Offset: %.1f deg"), Status.GlobalAngleOffset));
	Lines.Add(FString::Printf(TEXT("Bake Shift: %+.1f deg"), Status.AngleOffsetDelta));
	Lines.Add(FString::Printf(TEXT("Resolved Bake/Preview Angle: %.1f deg"), Status.EffectivePreviewAngle));
	Lines.Add(FString::Printf(TEXT("Allowed Bake Range: %.1f..%.1f deg"), Status.MinPreviewAngle, Status.MaxPreviewAngle));
	Lines.Add(TEXT("Authored key stays fixed; only bake and preview evaluation angle shifts."));
	Lines.Add(FString::Printf(TEXT("Texture: %s"), Status.TextureName.IsEmpty() ? TEXT("Missing") : *Status.TextureName));
	Lines.Add(FString::Printf(TEXT("Mask: %s"), Status.bHasMask ? TEXT("Ready") : TEXT("Missing")));
	Lines.Add(FString::Printf(TEXT("Edit State: %s"), Status.bIsActive ? TEXT("Current key") : TEXT("Inactive key")));
	Lines.Add(FString::Printf(TEXT("Apply Mode: %s"), *GetApplyModeLabel(Status.ApplyMode)));
	if (Status.ApplyMode != EQuickSDFApplyMode::Single)
	{
		Lines.Add(FString::Printf(TEXT("Direction: %s"), *GetApplyDirectionLabel(Status.ApplyDirection)));
	}
	Lines.Add(FString::Printf(TEXT("Apply Target: %s"), Status.bInPaintTargetRange ? TEXT("Included") : TEXT("Excluded")));
	if (Status.ApplyMode == EQuickSDFApplyMode::GradientRange)
	{
		Lines.Add(FString::Printf(TEXT("Radius Scale: %.0f%%"), FMath::Clamp(Status.RadiusScale, 0.0f, 1.0f) * 100.0f));
	}
	Lines.Add(FString::Printf(TEXT("Monotonic Guard: %s"), Status.bGuardEnabled ? TEXT("On") : TEXT("Off")));
	Lines.Add(FString::Printf(TEXT("Source Overwrite: %s"), Status.bAllowSourceTextureOverwrite ? TEXT("Writable") : TEXT("Assigned only")));

	if (Status.bHasUnbakedVectorLayer)
	{
		Lines.Add(TEXT("Vector Layer: Unbaked"));
	}

	return FText::FromString(FString::Join(Lines, TEXT("\n")));
}
}
