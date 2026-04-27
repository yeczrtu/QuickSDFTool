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
#include "Styling/CoreStyle.h"
#include "Styling/AppStyle.h"
#include "Editor.h"
#include "QuickSDFToolSubsystem.h"
#include "Engine/Texture2D.h"

#define LOCTEXT_NAMESPACE "SQuickSDFTimeline"

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
			.WidthOverride(600.0f) // Reasonable default width
			[
				SNew(SBorder)
				.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
				.Padding(4.0f)
				[
					SNew(SHorizontalBox)

					// Timeline Track Box (Dynamically populated)
					+ SHorizontalBox::Slot()
					.FillWidth(1.0f)
					.VAlign(VAlign_Center)
					[
						SAssignNew(TimelineTrackBox, SHorizontalBox)
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
							.ToolTipText(LOCTEXT("AddFrameToolTip", "Add a new light angle / texture frame"))
							.OnClicked(this, &SQuickSDFTimeline::OnAddKeyframeClicked)
							.ContentPadding(FMargin(8.0f, 2.0f))
						]
						+ SHorizontalBox::Slot()
						.AutoWidth()
						.Padding(2.0f)
						[
							SNew(SButton)
							.Text(LOCTEXT("DelFrameBtn", "-"))
							.ToolTipText(LOCTEXT("DelFrameToolTip", "Remove the last frame"))
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

	// Extract the properties to check for changes
	UQuickSDFToolProperties* Props = Tool->Properties;
	if (!Props) return;

	bool bNeedsRebuild = false;

	if (CachedNumAngles != Props->NumAngles || CachedEditAngleIndex != Props->EditAngleIndex)
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
		CachedEditAngleIndex = Props->EditAngleIndex;
		CachedTextures = Props->TargetTextures;

		if (TimelineTrackBox.IsValid())
		{
			TimelineTrackBox->ClearChildren();
			TimelineTrackBox->AddSlot()
			.AutoWidth()
			[
				GenerateTimelineSlots()
			];
		}
	}
}

TSharedRef<SWidget> SQuickSDFTimeline::GenerateTimelineSlots()
{
	TSharedRef<SHorizontalBox> Box = SNew(SHorizontalBox);

	UQuickSDFPaintTool* Tool = GetActivePaintTool();
	if (!Tool) return Box;

	UQuickSDFToolProperties* Props = Tool->Properties;
	if (!Props) return Box;

	for (int32 i = 0; i < Props->NumAngles; ++i)
	{
		const bool bIsActive = (i == Props->EditAngleIndex);

		FLinearColor BorderColor = bIsActive ? FLinearColor(1.0f, 0.6f, 0.1f, 1.0f) : FLinearColor(0.1f, 0.1f, 0.1f, 1.0f);
		FString AngleText = Props->TargetAngles.IsValidIndex(i) ? FString::Printf(TEXT("%.0f"), Props->TargetAngles[i]) : TEXT("?");

		Box->AddSlot()
		.AutoWidth()
		.Padding(4.0f, 0.0f)
		[
			SNew(SButton)
			.ButtonStyle(FAppStyle::Get(), "NoBorder")
			.ContentPadding(0.0f)
			.OnClicked(this, &SQuickSDFTimeline::OnKeyframeClicked, i)
			[
				SNew(SBorder)
				.BorderImage(FAppStyle::GetBrush("WhiteBrush"))
				.BorderBackgroundColor(BorderColor)
				.Padding(bIsActive ? 3.0f : 1.0f)
				[
					SNew(SBox)
					.WidthOverride(40.0f)
					.HeightOverride(40.0f)
					[
						SNew(SOverlay)
						
						// Background
						+ SOverlay::Slot()
						[
							SNew(SBorder)
							.BorderImage(FAppStyle::GetBrush("WhiteBrush"))
							.BorderBackgroundColor(FLinearColor(0.2f, 0.2f, 0.2f, 1.0f))
						]

						// Thumbnail or Angle Text
						+ SOverlay::Slot()
						.HAlign(HAlign_Center)
						.VAlign(VAlign_Center)
						[
							SNew(STextBlock)
							.Text(FText::FromString(AngleText))
							.ColorAndOpacity(FLinearColor::White)
						]
					]
				]
			]
		];
	}

	return Box;
}

FReply SQuickSDFTimeline::OnAddKeyframeClicked()
{
	UQuickSDFPaintTool* Tool = GetActivePaintTool();
	if (Tool)
	{
		if (UQuickSDFToolProperties* Props = Tool->Properties)
		{
			Props->NumAngles = FMath::Min(Props->NumAngles + 1, 32); // Max 32 frames for safety
			
			// Fire property changed
			FProperty* Prop = Props->GetClass()->FindPropertyByName(GET_MEMBER_NAME_CHECKED(UQuickSDFToolProperties, NumAngles));
			Tool->OnPropertyModified(Props, Prop);
			
			// Select the newly added frame
			Props->EditAngleIndex = Props->NumAngles - 1;
			FProperty* EditIndexProp = Props->GetClass()->FindPropertyByName(GET_MEMBER_NAME_CHECKED(UQuickSDFToolProperties, EditAngleIndex));
			Tool->OnPropertyModified(Props, EditIndexProp);
		}
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
			if (Props->NumAngles > 1)
			{
				Props->NumAngles -= 1;
				if (Props->EditAngleIndex >= Props->NumAngles)
				{
					Props->EditAngleIndex = Props->NumAngles - 1;
					FProperty* EditIndexProp = Props->GetClass()->FindPropertyByName(GET_MEMBER_NAME_CHECKED(UQuickSDFToolProperties, EditAngleIndex));
					Tool->OnPropertyModified(Props, EditIndexProp);
				}

				FProperty* Prop = Props->GetClass()->FindPropertyByName(GET_MEMBER_NAME_CHECKED(UQuickSDFToolProperties, NumAngles));
				Tool->OnPropertyModified(Props, Prop);
			}
		}
	}
	return FReply::Handled();
}

FReply SQuickSDFTimeline::OnKeyframeClicked(int32 Index)
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
	return FReply::Handled();
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
