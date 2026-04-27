#include "SQuickSDFTimeline.h"
#include "QuickSDFPaintTool.h"
#include "QuickSDFEditorMode.h"
#include "EditorModeManager.h"
#include "InteractiveToolManager.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SSpacer.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/SCanvas.h"
#include "Styling/CoreStyle.h"
#include "Styling/AppStyle.h"
#include "Editor.h"
#include "QuickSDFToolSubsystem.h"
#include "Engine/Texture2D.h"

#define LOCTEXT_NAMESPACE "SQuickSDFTimeline"

void SQuickSDFTimelineKeyframe::Construct(const FArguments& InArgs)
{
	Index = InArgs._Index;
	Angle = InArgs._Angle;
	bIsActive = InArgs._bIsActive;
	OnAngleChanged = InArgs._OnAngleChanged;
	OnClicked = InArgs._OnClicked;

	auto ColorAttr = TAttribute<FSlateColor>::CreateLambda([this]() {
		return bIsActive.Get()
			? FSlateColor(FLinearColor(1.0f, 0.6f, 0.1f, 1.0f))
			: FSlateColor(FLinearColor(0.2f, 0.2f, 0.2f, 1.0f));
	});

	ChildSlot
	[
		SNew(SVerticalBox)
		
		// The Head of the needle (small square/diamond)
		+ SVerticalBox::Slot()
		.AutoHeight()
		.HAlign(HAlign_Center)
		[
			SNew(SBorder)
			.BorderImage(FAppStyle::GetBrush("WhiteBrush"))
			.BorderBackgroundColor(ColorAttr)
			.Padding(1.0f)
			[
				SNew(SBox)
				.WidthOverride(12.0f)
				.HeightOverride(12.0f)
			]
		]
		
		// The Body of the needle (thin line)
		+ SVerticalBox::Slot()
		.FillHeight(1.0f)
		.HAlign(HAlign_Center)
		[
			SNew(SBorder)
			.BorderImage(FAppStyle::GetBrush("WhiteBrush"))
			.BorderBackgroundColor(ColorAttr)
			.Padding(0)
			[
				SNew(SBox)
				.WidthOverride(2.0f)
			]
		]
	];
}

FReply SQuickSDFTimelineKeyframe::OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (MouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
	{
		bIsDragging = true;
		OnClicked.ExecuteIfBound();
		return FReply::Handled().CaptureMouse(SharedThis(this));
	}
	return FReply::Unhandled();
}

FReply SQuickSDFTimelineKeyframe::OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (MouseEvent.GetEffectingButton() == EKeys::LeftMouseButton && bIsDragging)
	{
		bIsDragging = false;
		return FReply::Handled().ReleaseMouseCapture();
	}
	return FReply::Unhandled();
}

FCursorReply SQuickSDFTimelineKeyframe::OnCursorQuery(const FGeometry& MyGeometry, const FPointerEvent& CursorEvent) const
{
	return FCursorReply::Cursor(EMouseCursor::GrabHand);
}

FReply SQuickSDFTimelineKeyframe::OnMouseMove(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (bIsDragging)
	{
		// Need to get the parent canvas geometry to determine percentage
		TSharedPtr<SWidget> ParentWidget = GetParentWidget();
		if (ParentWidget.IsValid())
		{
			FGeometry ParentGeometry = ParentWidget->GetTickSpaceGeometry();
			FVector2D LocalPos = ParentGeometry.AbsoluteToLocal(MouseEvent.GetScreenSpacePosition());
			
			float TrackWidth = ParentGeometry.GetLocalSize().X - 40.0f; // Account for padding
			if (TrackWidth > 0.0f)
			{
				float Percent = FMath::Clamp((LocalPos.X - 20.0f) / TrackWidth, 0.0f, 1.0f);
				float NewAngle = Percent * 180.0f;
				
				// Notify parent immediately
				OnAngleChanged.ExecuteIfBound(NewAngle);
			}
		}
		return FReply::Handled();
	}
	return FReply::Unhandled();
}

void SQuickSDFTimeline::Construct(const FArguments& InArgs)
{
	ChildSlot
	[
		SNew(SVerticalBox)
		+ SVerticalBox::Slot()
		.FillHeight(1.0f) // Spacer to push content to bottom
		[
			SNew(SSpacer)
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		.HAlign(HAlign_Center)
		.Padding(10.0f)
		[
			SNew(SBox)
			.WidthOverride(800.0f) // Wider for spatial timeline
			[
				SNew(SBorder)
				.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
				.Padding(4.0f)
				[
					SNew(SHorizontalBox)

					// Timeline Track Canvas
					+ SHorizontalBox::Slot()
					.FillWidth(1.0f)
					.VAlign(VAlign_Center)
					.Padding(0.0f, 10.0f)
					[
						SNew(SBox)
						.HeightOverride(60.0f)
						[
							SNew(SOverlay)
							
							// Track background line
							+ SOverlay::Slot()
							.VAlign(VAlign_Center)
							[
								SNew(SBorder)
								.BorderImage(FAppStyle::GetBrush("WhiteBrush"))
								.BorderBackgroundColor(FLinearColor(0.05f, 0.05f, 0.05f, 1.0f))
								.Padding(FMargin(0, 2))
							]

							// The actual canvas for keyframes
							+ SOverlay::Slot()
							[
								SAssignNew(TimelineTrackCanvas, SCanvas)
							]
						]
					]

					// Controls (Add/Delete)
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					.Padding(10.0f, 0.0f, 0.0f, 0.0f)
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot()
						.AutoWidth()
						.Padding(2.0f)
						[
							SNew(SButton)
							.Text(LOCTEXT("AddFrameBtn", "+"))
							.ToolTipText(LOCTEXT("AddFrameToolTip", "Add a new light angle keyframe"))
							.OnClicked(this, &SQuickSDFTimeline::OnAddKeyframeClicked)
							.ContentPadding(FMargin(8.0f, 2.0f))
						]
						+ SHorizontalBox::Slot()
						.AutoWidth()
						.Padding(2.0f)
						[
							SNew(SButton)
							.Text(LOCTEXT("DelFrameBtn", "-"))
							.ToolTipText(LOCTEXT("DelFrameToolTip", "Remove the selected keyframe"))
							.OnClicked(this, &SQuickSDFTimeline::OnDeleteKeyframeClicked)
							.ContentPadding(FMargin(8.0f, 2.0f))
						]
					]
				]
			]
		]
	];
}

void SQuickSDFTimeline::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	SCompoundWidget::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);

	UQuickSDFPaintTool* Tool = GetActivePaintTool();
	if (!Tool) return;

	UQuickSDFToolProperties* Props = Tool->Properties;
	if (!Props) return;

	bool bNeedsRebuild = false;

	// Only rebuild if the number of elements or the actual textures changed
	if (CachedNumAngles != Props->NumAngles)
	{
		bNeedsRebuild = true;
	}

	if (!bNeedsRebuild && CachedTextures.Num() == Props->TargetTextures.Num())
	{
		for (int32 i = 0; i < CachedTextures.Num(); ++i)
		{
			if (CachedTextures[i] != Props->TargetTextures[i])
			{
				bNeedsRebuild = true;
				break;
			}
		}
	}
	else if (CachedTextures.Num() != Props->TargetTextures.Num())
	{
		bNeedsRebuild = true;
	}

	if (bNeedsRebuild)
	{
		CachedNumAngles = Props->NumAngles;
		CachedTextures = Props->TargetTextures;

		RebuildTimeline();
	}
}

void SQuickSDFTimeline::RebuildTimeline()
{
	if (!TimelineTrackCanvas.IsValid()) return;
	
	TimelineTrackCanvas->ClearChildren();

	UQuickSDFPaintTool* Tool = GetActivePaintTool();
	if (!Tool) return;

	UQuickSDFToolProperties* Props = Tool->Properties;
	if (!Props) return;

	// Add tick marks for 0, 90, 180
	for (int32 i = 0; i <= 2; ++i)
	{
		float TickAngle = i * 90.0f;
		float Percent = TickAngle / 180.0f;
		
		TimelineTrackCanvas->AddSlot()
		.Position(TAttribute<FVector2D>::CreateLambda([this, Percent]() {
			float TrackWidth = TimelineTrackCanvas->GetTickSpaceGeometry().GetLocalSize().X - 40.0f;
			// Center the 2px tick mark (20px offset + Percent * TrackWidth - 1px)
			return FVector2D(FMath::Max(0.0f, TrackWidth) * Percent + 19.0f, 0.0f);
		}))
		.Size(FVector2D(2.0f, 60.0f))
		[
			SNew(SBorder)
			.BorderImage(FAppStyle::GetBrush("WhiteBrush"))
			.BorderBackgroundColor(FLinearColor(0.3f, 0.3f, 0.3f, 1.0f))
		];
	}

	for (int32 i = 0; i < Props->NumAngles; ++i)
	{
		TimelineTrackCanvas->AddSlot()
		.Position(TAttribute<FVector2D>::CreateLambda([this, i]() {
			UQuickSDFPaintTool* ActiveTool = GetActivePaintTool();
			if (ActiveTool && ActiveTool->Properties && ActiveTool->Properties->TargetAngles.IsValidIndex(i))
			{
				float CurrentAngle = ActiveTool->Properties->TargetAngles[i];
				float Percent = FMath::Clamp(CurrentAngle / 180.0f, 0.0f, 1.0f);
				float TrackWidth = TimelineTrackCanvas->GetTickSpaceGeometry().GetLocalSize().X - 40.0f;
				return FVector2D(FMath::Max(0.0f, TrackWidth) * Percent, 0.0f);
			}
			return FVector2D::ZeroVector;
		}))
		.Size(FVector2D(40.0f, 60.0f))
		[
			SNew(SQuickSDFTimelineKeyframe)
			.Index(i)
			.Angle(TAttribute<float>::CreateLambda([this, i]() {
				UQuickSDFPaintTool* ActiveTool = GetActivePaintTool();
				if (ActiveTool && ActiveTool->Properties && ActiveTool->Properties->TargetAngles.IsValidIndex(i))
					return ActiveTool->Properties->TargetAngles[i];
				return 0.0f;
			}))
			.bIsActive(TAttribute<bool>::CreateLambda([this, i]() {
				UQuickSDFPaintTool* ActiveTool = GetActivePaintTool();
				return ActiveTool && ActiveTool->Properties && ActiveTool->Properties->EditAngleIndex == i;
			}))
			.OnClicked(this, &SQuickSDFTimeline::OnKeyframeClicked, i)
			.OnAngleChanged(this, &SQuickSDFTimeline::OnKeyframeAngleChanged, i)
		];
	}
}

FReply SQuickSDFTimeline::OnAddKeyframeClicked()
{
	UQuickSDFPaintTool* Tool = GetActivePaintTool();
	if (Tool)
	{
		Tool->AddKeyframe();
	}
	return FReply::Handled();
}

FReply SQuickSDFTimeline::OnDeleteKeyframeClicked()
{
	UQuickSDFPaintTool* Tool = GetActivePaintTool();
	if (Tool)
	{
		if (UQuickSDFToolProperties* Props = Tool->Properties)
		{
			Tool->RemoveKeyframe(Props->EditAngleIndex);
		}
	}
	return FReply::Handled();
}

void SQuickSDFTimeline::OnKeyframeClicked(int32 Index)
{
	UQuickSDFPaintTool* Tool = GetActivePaintTool();
	if (Tool)
	{
		if (UQuickSDFToolProperties* Props = Tool->Properties)
		{
			Props->EditAngleIndex = Index;
			FProperty* Prop = Props->GetClass()->FindPropertyByName(GET_MEMBER_NAME_CHECKED(UQuickSDFToolProperties, EditAngleIndex));
			Tool->OnPropertyModified(Props, Prop);
		}
	}
}

void SQuickSDFTimeline::OnKeyframeAngleChanged(float NewAngle, int32 Index)
{
	UQuickSDFPaintTool* Tool = GetActivePaintTool();
	if (Tool)
	{
		if (UQuickSDFToolProperties* Props = Tool->Properties)
		{
			if (Props->TargetAngles.IsValidIndex(Index))
			{
				Props->TargetAngles[Index] = NewAngle;
				
				// Fire property modified to update the preview light
				FProperty* Prop = Props->GetClass()->FindPropertyByName(GET_MEMBER_NAME_CHECKED(UQuickSDFToolProperties, TargetAngles));
				Tool->OnPropertyModified(Props, Prop);
			}
		}
	}
}

UQuickSDFPaintTool* SQuickSDFTimeline::GetActivePaintTool() const
{
	// Assuming EM_QuickSDFEditorMode is active
	UQuickSDFEditorMode* Mode = Cast<UQuickSDFEditorMode>(GLevelEditorModeTools().GetActiveScriptableMode("EM_QuickSDFEditorMode"));
	if (Mode && Mode->GetToolManager())
	{
		return Cast<UQuickSDFPaintTool>(Mode->GetToolManager()->GetActiveTool(EToolSide::Left));
	}
	return nullptr;
}

#undef LOCTEXT_NAMESPACE
