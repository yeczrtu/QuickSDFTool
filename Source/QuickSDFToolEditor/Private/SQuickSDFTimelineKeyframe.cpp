#include "SQuickSDFTimeline.h"

#include "InputCoreTypes.h"
#include "Styling/AppStyle.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "SQuickSDFTimelineKeyframe"

namespace
{
constexpr float QuickSDFTimelineDragStartDistance = 4.0f;
constexpr float QuickSDFTimelineAccentR = 0.35f;
constexpr float QuickSDFTimelineAccentG = 0.82f;
constexpr float QuickSDFTimelineAccentB = 1.0f;

FLinearColor GetQuickSDFTimelineAccentColor(float Alpha = 1.0f)
{
	return FLinearColor(QuickSDFTimelineAccentR, QuickSDFTimelineAccentG, QuickSDFTimelineAccentB, Alpha);
}
}

void SQuickSDFTimelineKeyframe::Construct(const FArguments& InArgs)
{
	Index = InArgs._Index;
	Angle = InArgs._Angle;
	bIsActive = InArgs._bIsActive;
	bSnapEnabled = InArgs._bSnapEnabled;
	bSymmetryMode = InArgs._bSymmetryMode;
	bAllowSourceTextureOverwrite = InArgs._bAllowSourceTextureOverwrite;
	TextureBrush = InArgs._TextureBrush;
	OnAngleChanged = InArgs._OnAngleChanged;
	OnClicked = InArgs._OnClicked;
	OnDragStarted = InArgs._OnDragStarted;
	OnDragEnded = InArgs._OnDragEnded;

	auto ColorAttr = TAttribute<FSlateColor>::CreateLambda([this]()
	{
		return bIsActive.Get()
			? FSlateColor(FLinearColor(0.95f, 0.95f, 0.95f, 1.0f))
			: FSlateColor(FLinearColor(0.72f, 0.72f, 0.72f, 0.72f));
	});

	SetToolTipText(TAttribute<FText>::CreateLambda([this]()
	{
		return bAllowSourceTextureOverwrite.Get()
			? LOCTEXT("WritableSourceKeyframeTooltip", "Writable source: Overwrite Source Textures can write this mask back to its Texture2D.")
			: LOCTEXT("AssignedOnlySourceKeyframeTooltip", "Assigned only: this mask will not overwrite its Texture2D.");
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
					return bIsActive.Get() ? GetQuickSDFTimelineAccentColor(0.95f) : FLinearColor(1.0f, 1.0f, 1.0f, 0.18f);
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
			SNew(STextBlock)
			.Text(TAttribute<FText>::CreateLambda([this]() {
				return FText::FromString(FString::Printf(TEXT("%.0f\u00B0"), Angle.Get()));
			}))
			.ColorAndOpacity(ColorAttr)
			.Font(FCoreStyle::GetDefaultFontStyle("Bold", 7))
			.ShadowOffset(FVector2D(1.0f, 1.0f))
			.ShadowColorAndOpacity(FLinearColor::Black)
		]

		+ SOverlay::Slot()
		.HAlign(HAlign_Center)
		.VAlign(VAlign_Top)
		.Padding(0.0f, 11.0f, 0.0f, 0.0f)
		[
			SNew(STextBlock)
			.Text(LOCTEXT("WritableSourceKeyframeLabel", "Writable"))
			.Visibility(TAttribute<EVisibility>::CreateLambda([this]()
			{
				return bAllowSourceTextureOverwrite.Get() ? EVisibility::HitTestInvisible : EVisibility::Collapsed;
			}))
			.ColorAndOpacity(FLinearColor(1.0f, 0.78f, 0.3f, 1.0f))
			.Font(FCoreStyle::GetDefaultFontStyle("Bold", 6))
			.ShadowOffset(FVector2D(1.0f, 1.0f))
			.ShadowColorAndOpacity(FLinearColor::Black)
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
		OnClicked.ExecuteIfBound();
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

				if (bSnapEnabled.Get())
				{
					NewAngle = FMath::RoundToFloat(NewAngle / 5.0f) * 5.0f;
				}
				
				// Notify parent immediately
				OnAngleChanged.ExecuteIfBound(NewAngle);
			}
		}
		return FReply::Handled();
	}
	return FReply::Unhandled();
}

#undef LOCTEXT_NAMESPACE
