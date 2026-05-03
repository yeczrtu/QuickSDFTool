#include "SQuickSDFTimeline.h"

#include "InputCoreTypes.h"
#include "Styling/AppStyle.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "SQuickSDFTimelineKeyframe"

namespace
{
constexpr float QuickSDFTimelineDragStartDistance = 8.0f;
constexpr float QuickSDFTimelineAccentR = 0.35f;
constexpr float QuickSDFTimelineAccentG = 0.82f;
constexpr float QuickSDFTimelineAccentB = 1.0f;
constexpr float QuickSDFTimelineBadgeSize = 7.0f;
constexpr float QuickSDFTimelineBadgeInnerSize = 5.0f;
constexpr float QuickSDFTimelineAngleLabelWidth = 26.0f;

FLinearColor GetQuickSDFTimelineAccentColor(float Alpha = 1.0f)
{
	return FLinearColor(QuickSDFTimelineAccentR, QuickSDFTimelineAccentG, QuickSDFTimelineAccentB, Alpha);
}

TSharedRef<SWidget> MakeStatusBadge(TAttribute<EVisibility> Visibility, TAttribute<FSlateColor> Color)
{
	return SNew(SBox)
		.WidthOverride(QuickSDFTimelineBadgeSize)
		.HeightOverride(QuickSDFTimelineBadgeSize)
		.Visibility(Visibility)
		[
			SNew(SBorder)
			.Visibility(EVisibility::HitTestInvisible)
			.BorderImage(FAppStyle::GetBrush("WhiteBrush"))
			.BorderBackgroundColor(FLinearColor(0.0f, 0.0f, 0.0f, 0.58f))
			.Padding(1.0f)
			[
				SNew(SBox)
				.WidthOverride(QuickSDFTimelineBadgeInnerSize)
				.HeightOverride(QuickSDFTimelineBadgeInnerSize)
				[
					SNew(SBorder)
					.BorderImage(FAppStyle::GetBrush("WhiteBrush"))
					.BorderBackgroundColor(Color)
				]
			]
		];
}
}

void SQuickSDFTimelineKeyframe::Construct(const FArguments& InArgs)
{
	Index = InArgs._Index;
	Angle = InArgs._Angle;
	bIsActive = InArgs._bIsActive;
	bSymmetryMode = InArgs._bSymmetryMode;
	bAllowSourceTextureOverwrite = InArgs._bAllowSourceTextureOverwrite;
	bHasMask = InArgs._bHasMask;
	bIsInPaintTargetRange = InArgs._bIsInPaintTargetRange;
	bGuardEnabled = InArgs._bGuardEnabled;
	bHasUnbakedVectorLayer = InArgs._bHasUnbakedVectorLayer;
	bHasWarning = InArgs._bHasWarning;
	StatusToolTip = InArgs._StatusToolTip;
	TextureBrush = InArgs._TextureBrush;
	OnAngleChanged = InArgs._OnAngleChanged;
	OnClicked = InArgs._OnClicked;
	OnDragStarted = InArgs._OnDragStarted;
	OnDragEnded = InArgs._OnDragEnded;

	auto ColorAttr = TAttribute<FSlateColor>::CreateLambda([this]()
	{
		return bIsActive.Get()
			? FSlateColor(FLinearColor(0.95f, 0.95f, 0.95f, 1.0f))
			: bIsInPaintTargetRange.Get()
				? FSlateColor(FLinearColor(0.78f, 0.88f, 0.92f, 0.92f))
				: FSlateColor(FLinearColor(0.72f, 0.72f, 0.72f, 0.72f));
	});

	SetToolTipText(TAttribute<FText>::CreateLambda([this]()
	{
		const FText ToolTip = StatusToolTip.Get();
		return ToolTip.IsEmpty()
			? LOCTEXT("TimelineKeyframeFallbackTooltip", "Timeline key")
			: ToolTip;
	}));

	ChildSlot
	[
		SNew(SOverlay)

		// A quiet hit/drag mark for every keyframe.
		+ SOverlay::Slot()
		.HAlign(HAlign_Center)
		.VAlign(VAlign_Top)
		.Padding(0.0f, 5.0f, 0.0f, 0.0f)
		[
			SNew(SBox)
			.WidthOverride(1.0f)
			.HeightOverride(28.0f)
			[
				SNew(SBorder)
				.BorderImage(FAppStyle::GetBrush("WhiteBrush"))
				.BorderBackgroundColor(TAttribute<FSlateColor>::CreateLambda([this]()
				{
					if (bIsActive.Get())
					{
						return GetQuickSDFTimelineAccentColor(0.95f);
					}
					if (bIsInPaintTargetRange.Get())
					{
						return GetQuickSDFTimelineAccentColor(0.52f);
					}
					return FLinearColor(1.0f, 1.0f, 1.0f, 0.18f);
				}))
			]
		]

		// Active keyframe handle.
		+ SOverlay::Slot()
		.HAlign(HAlign_Center)
		.VAlign(VAlign_Top)
		.Padding(0.0f, 1.0f, 0.0f, 0.0f)
		[
			SNew(SBox)
			.WidthOverride(8.0f)
			.HeightOverride(8.0f)
			.Visibility(TAttribute<EVisibility>::CreateLambda([this]()
			{
				return bIsActive.Get() ? EVisibility::HitTestInvisible : EVisibility::Collapsed;
			}))
			[
				SNew(SBorder)
				.BorderImage(FAppStyle::GetBrush("WhiteBrush"))
				.BorderBackgroundColor(GetQuickSDFTimelineAccentColor(1.0f))
			]
		]

		// Active keyframe accent line.
		+ SOverlay::Slot()
		.HAlign(HAlign_Fill)
		.VAlign(VAlign_Bottom)
		.Padding(3.0f, 0.0f, 3.0f, 11.0f)
		[
			SNew(SBox)
			.HeightOverride(2.0f)
			.Visibility(TAttribute<EVisibility>::CreateLambda([this]()
			{
				return bIsActive.Get() ? EVisibility::HitTestInvisible : EVisibility::Collapsed;
			}))
			[
				SNew(SBorder)
				.BorderImage(FAppStyle::GetBrush("WhiteBrush"))
				.BorderBackgroundColor(GetQuickSDFTimelineAccentColor(1.0f))
			]
		]

		// Angle label.
		+ SOverlay::Slot()
		.HAlign(HAlign_Center)
		.VAlign(VAlign_Bottom)
		.Padding(0.0f, 0.0f, 0.0f, 1.0f)
		[
			SNew(SBox)
			.WidthOverride(QuickSDFTimelineAngleLabelWidth)
			.HAlign(HAlign_Center)
			[
				SNew(STextBlock)
				.Text(TAttribute<FText>::CreateLambda([this]() {
					return FText::FromString(FString::Printf(TEXT("%.0f\u00B0"), Angle.Get()));
				}))
				.ColorAndOpacity(ColorAttr)
				.Font(FCoreStyle::GetDefaultFontStyle("Bold", 7))
				.ShadowOffset(FVector2D(1.0f, 1.0f))
				.ShadowColorAndOpacity(FLinearColor::Black)
			]
		]

		// Compact status badges: mask, guard, future vector-layer, warning.
		+ SOverlay::Slot()
		.HAlign(HAlign_Center)
		.VAlign(VAlign_Top)
		.Padding(0.0f, 10.0f, 0.0f, 0.0f)
		[
			SNew(SHorizontalBox)
			.Visibility(EVisibility::HitTestInvisible)
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(1.5f, 0.0f)
			[
				MakeStatusBadge(
					TAttribute<EVisibility>(EVisibility::HitTestInvisible),
					TAttribute<FSlateColor>::CreateLambda([this]()
					{
						return bHasMask.Get()
							? FSlateColor(FLinearColor(0.48f, 0.92f, 0.68f, 0.82f))
							: FSlateColor(FLinearColor(0.56f, 0.56f, 0.56f, 0.54f));
					}))
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(1.5f, 0.0f)
			[
				MakeStatusBadge(
					TAttribute<EVisibility>::CreateLambda([this]()
					{
						return bGuardEnabled.Get() ? EVisibility::HitTestInvisible : EVisibility::Collapsed;
					}),
					TAttribute<FSlateColor>(FLinearColor(0.26f, 0.66f, 1.0f, 0.82f)))
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(1.5f, 0.0f)
			[
				MakeStatusBadge(
					TAttribute<EVisibility>::CreateLambda([this]()
					{
						return bHasUnbakedVectorLayer.Get() ? EVisibility::HitTestInvisible : EVisibility::Collapsed;
					}),
					TAttribute<FSlateColor>(FLinearColor(0.82f, 0.42f, 1.0f, 0.80f)))
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(1.5f, 0.0f)
			[
				MakeStatusBadge(
					TAttribute<EVisibility>::CreateLambda([this]()
					{
						return bHasWarning.Get() ? EVisibility::HitTestInvisible : EVisibility::Collapsed;
					}),
					TAttribute<FSlateColor>(FLinearColor(1.0f, 0.50f, 0.12f, 0.86f)))
			]
		]
	];
}

FReply SQuickSDFTimelineKeyframe::OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (MouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
	{
		bIsMouseDown = true;
		bIsDragging = false;
		MouseDownScreenPosition = MouseEvent.GetScreenSpacePosition();
		return FReply::Handled().CaptureMouse(SharedThis(this));
	}
	return FReply::Unhandled();
}

FReply SQuickSDFTimelineKeyframe::OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (MouseEvent.GetEffectingButton() == EKeys::LeftMouseButton && bIsMouseDown)
	{
		bIsMouseDown = false;
		if (bIsDragging)
		{
			bIsDragging = false;
			OnDragEnded.ExecuteIfBound();
		}
		else
		{
			OnClicked.ExecuteIfBound();
		}
		return FReply::Handled().ReleaseMouseCapture();
	}
	return FReply::Unhandled();
}

void SQuickSDFTimelineKeyframe::OnMouseCaptureLost(const FCaptureLostEvent& CaptureLostEvent)
{
	bIsMouseDown = false;
	if (bIsDragging)
	{
		bIsDragging = false;
		OnDragEnded.ExecuteIfBound();
	}
}

FCursorReply SQuickSDFTimelineKeyframe::OnCursorQuery(const FGeometry& MyGeometry, const FPointerEvent& CursorEvent) const
{
	return FCursorReply::Cursor(EMouseCursor::GrabHand);
}

FReply SQuickSDFTimelineKeyframe::OnMouseMove(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (bIsMouseDown)
	{
		if (!bIsDragging)
		{
			const float DragDistance = FVector2D::Distance(MouseDownScreenPosition, MouseEvent.GetScreenSpacePosition());
			if (DragDistance < QuickSDFTimelineDragStartDistance)
			{
				return FReply::Handled();
			}

			bIsDragging = true;
			OnClicked.ExecuteIfBound();
			OnDragStarted.ExecuteIfBound();
		}

		// Need to get the parent canvas geometry to determine percentage
		TSharedPtr<SWidget> ParentWidget = GetParentWidget();
		if (ParentWidget.IsValid())
		{
			FGeometry ParentGeometry = ParentWidget->GetTickSpaceGeometry();
			FVector2D LocalPos = ParentGeometry.AbsoluteToLocal(MouseEvent.GetScreenSpacePosition());
			
			float TrackWidth = ParentGeometry.GetLocalSize().X - 40.0f; // Account for padding
			if (TrackWidth > 0.0f)
			{
				bool bSymmetry = bSymmetryMode.Get();
				float MaxAngle = bSymmetry ? 90.0f : 180.0f;
				
				float Percent = FMath::Clamp((LocalPos.X - 20.0f) / TrackWidth, 0.0f, 1.0f);
				float NewAngle = Percent * MaxAngle;

				// Notify parent immediately
				OnAngleChanged.ExecuteIfBound(NewAngle);
			}
		}
		return FReply::Handled();
	}
	return FReply::Unhandled();
}

#undef LOCTEXT_NAMESPACE
