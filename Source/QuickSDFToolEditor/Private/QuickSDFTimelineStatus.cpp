#include "QuickSDFTimelineStatus.h"

bool FQuickSDFTimelineRangeStatus::IsKeyInTargetRange(int32 KeyIndex) const
{
	if (const FQuickSDFTimelineKeySegment* Segment = FindSegmentByKeyIndex(KeyIndex))
	{
		return Segment->bInPaintTargetRange;
	}
	return false;
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

FQuickSDFTimelineRangeStatus BuildRangeStatus(
	const TArray<float>& Angles,
	int32 ActiveKeyIndex,
	EQuickSDFPaintTargetMode PaintTargetMode,
	bool bPaintAllAngles,
	bool bSymmetryMode)
{
	FQuickSDFTimelineRangeStatus Status;
	Status.MaxAngle = bSymmetryMode ? 90.0f : 180.0f;
	Status.ActiveKeyIndex = Angles.IsValidIndex(ActiveKeyIndex) ? ActiveKeyIndex : INDEX_NONE;
	Status.PaintTargetMode = ResolvePaintTargetMode(PaintTargetMode, bPaintAllAngles);
	Status.VisibleKeyIndices = MakeVisibleSortedKeyIndices(Angles, bSymmetryMode);
	Status.ActiveVisualIndex = Status.VisibleKeyIndices.IndexOfByKey(Status.ActiveKeyIndex);

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

		if (Status.PaintTargetMode == EQuickSDFPaintTargetMode::All)
		{
			Segment.bInPaintTargetRange = true;
		}
		else if (Status.ActiveVisualIndex != INDEX_NONE)
		{
			switch (Status.PaintTargetMode)
			{
			case EQuickSDFPaintTargetMode::CurrentOnly:
				Segment.bInPaintTargetRange = VisualIndex == Status.ActiveVisualIndex;
				break;
			case EQuickSDFPaintTargetMode::BeforeCurrent:
				Segment.bInPaintTargetRange = VisualIndex <= Status.ActiveVisualIndex;
				break;
			case EQuickSDFPaintTargetMode::AfterCurrent:
				Segment.bInPaintTargetRange = VisualIndex >= Status.ActiveVisualIndex;
				break;
			default:
				break;
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

FQuickSDFTimelineKeyStatus BuildKeyStatus(const FQuickSDFTimelineKeyStatusInput& Input)
{
	FQuickSDFTimelineKeyStatus Status;
	Status.KeyIndex = Input.KeyIndex;
	Status.Angle = Input.Angle;
	Status.bVisible = Input.bVisible;
	Status.bIsActive = Input.bIsActive;
	Status.bInPaintTargetRange = Input.bInPaintTargetRange;
	Status.bHasMask = Input.bHasTextureMask || Input.bHasPaintRenderTarget;
	Status.bAllowSourceTextureOverwrite = Input.bAllowSourceTextureOverwrite;
	Status.bGuardEnabled = Input.bGuardEnabled;
	Status.bHasUnbakedVectorLayer = Input.bHasUnbakedVectorLayer;
	Status.bHasWarning = Input.bHasWarning;
	Status.TextureName = Input.TextureName;
	Status.WarningMessage = Input.WarningMessage;
	return Status;
}

FText BuildKeyTooltip(const FQuickSDFTimelineKeyStatus& Status)
{
	TArray<FString> Lines;
	Lines.Add(FString::Printf(TEXT("Angle: %.0f deg"), Status.Angle));
	Lines.Add(FString::Printf(TEXT("Texture: %s"), Status.TextureName.IsEmpty() ? TEXT("Missing") : *Status.TextureName));
	Lines.Add(FString::Printf(TEXT("Mask: %s"), Status.bHasMask ? TEXT("Ready") : TEXT("Missing")));
	Lines.Add(FString::Printf(TEXT("Edit State: %s"), Status.bIsActive ? TEXT("Current key") : TEXT("Inactive key")));
	Lines.Add(FString::Printf(TEXT("Paint Target: %s"), Status.bInPaintTargetRange ? TEXT("Included") : TEXT("Excluded")));
	Lines.Add(FString::Printf(TEXT("Monotonic Guard: %s"), Status.bGuardEnabled ? TEXT("On") : TEXT("Off")));
	Lines.Add(FString::Printf(TEXT("Source Overwrite: %s"), Status.bAllowSourceTextureOverwrite ? TEXT("Writable") : TEXT("Assigned only")));

	if (Status.bHasUnbakedVectorLayer)
	{
		Lines.Add(TEXT("Vector Layer: Unbaked"));
	}

	if (Status.bHasWarning)
	{
		const FString WarningText = Status.WarningMessage.IsEmpty()
			? FString(TEXT("Timeline key has a warning"))
			: Status.WarningMessage.ToString();
		Lines.Add(FString::Printf(TEXT("Warning: %s"), *WarningText));
	}

	return FText::FromString(FString::Join(Lines, TEXT("\n")));
}
}
