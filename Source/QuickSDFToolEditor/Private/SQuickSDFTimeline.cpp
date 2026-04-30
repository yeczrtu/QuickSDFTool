#include "SQuickSDFTimeline.h"
#include "QuickSDFPaintTool.h"
#include "QuickSDFEditorMode.h"
#include "DragAndDrop/AssetDragDropOp.h"
#include "EditorModeManager.h"
#include "InteractiveToolManager.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SSpacer.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SCanvas.h"
#include "Widgets/SOverlay.h"
#include "Styling/CoreStyle.h"
#include "Styling/AppStyle.h"
#include "Editor.h"
#include "QuickSDFToolSubsystem.h"
#include "Engine/Texture2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "EngineUtils.h"
#include "Engine/DirectionalLight.h"
#include "QuickSDFAsset.h"
#include "QuickSDFToolStyle.h"
#include "QuickSDFToolUI.h"
#include "Brushes/SlateImageBrush.h"
#include "Widgets/Layout/SExpandableArea.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Layout/SScaleBox.h"

#define LOCTEXT_NAMESPACE "SQuickSDFTimeline"

namespace
{
constexpr int32 QuickSDFTimelineThumbnailSize = 64;
constexpr float QuickSDFTimelineDragStartDistance = 4.0f;
constexpr float QuickSDFTimelineKeyframeHitSlop = 14.0f;

TSharedRef<SWidget> MakeTimelineIconButton(const FName IconName, const FText& ToolTip, FOnClicked OnClicked)
{
	return SNew(SButton)
		.OnClicked(OnClicked)
		.ContentPadding(FMargin(4.0f, 2.0f))
		.ToolTipText(ToolTip)
		[
			SNew(SImage)
			.Image(FQuickSDFToolStyle::GetBrush(IconName))
		];
}

UTexture2D* CreateTimelineThumbnailTexture(UQuickSDFPaintTool* Tool, UTextureRenderTarget2D* RenderTarget)
{
	if (!Tool || !RenderTarget)
	{
		return nullptr;
	}

	TArray<FColor> SourcePixels;
	if (!Tool->CaptureRenderTargetPixels(RenderTarget, SourcePixels) ||
		SourcePixels.Num() != RenderTarget->SizeX * RenderTarget->SizeY)
	{
		return nullptr;
	}

	TArray<FColor> ThumbnailPixels;
	ThumbnailPixels.SetNum(QuickSDFTimelineThumbnailSize * QuickSDFTimelineThumbnailSize);
	for (int32 Y = 0; Y < QuickSDFTimelineThumbnailSize; ++Y)
	{
		const int32 SourceY = FMath::Clamp(
			FMath::RoundToInt((static_cast<float>(Y) + 0.5f) / QuickSDFTimelineThumbnailSize * RenderTarget->SizeY - 0.5f),
			0,
			RenderTarget->SizeY - 1);
		for (int32 X = 0; X < QuickSDFTimelineThumbnailSize; ++X)
		{
			const int32 SourceX = FMath::Clamp(
				FMath::RoundToInt((static_cast<float>(X) + 0.5f) / QuickSDFTimelineThumbnailSize * RenderTarget->SizeX - 0.5f),
				0,
				RenderTarget->SizeX - 1);
			ThumbnailPixels[Y * QuickSDFTimelineThumbnailSize + X] = SourcePixels[SourceY * RenderTarget->SizeX + SourceX];
			ThumbnailPixels[Y * QuickSDFTimelineThumbnailSize + X].A = 255;
		}
	}

	UTexture2D* Thumbnail = UTexture2D::CreateTransient(QuickSDFTimelineThumbnailSize, QuickSDFTimelineThumbnailSize, PF_B8G8R8A8);
	if (!Thumbnail || !Thumbnail->GetPlatformData() || Thumbnail->GetPlatformData()->Mips.Num() == 0)
	{
		return nullptr;
	}

	Thumbnail->MipGenSettings = TMGS_NoMipmaps;
	Thumbnail->Filter = TF_Nearest;
	Thumbnail->SRGB = false;
	FTexture2DMipMap& Mip = Thumbnail->GetPlatformData()->Mips[0];
	void* Data = Mip.BulkData.Lock(LOCK_READ_WRITE);
	FMemory::Memcpy(Data, ThumbnailPixels.GetData(), ThumbnailPixels.Num() * sizeof(FColor));
	Mip.BulkData.Unlock();
	Thumbnail->UpdateResource();
	return Thumbnail;
}
}

void SQuickSDFTimelineKeyframe::Construct(const FArguments& InArgs)
{
	Index = InArgs._Index;
	Angle = InArgs._Angle;
	bIsActive = InArgs._bIsActive;
	bSnapEnabled = InArgs._bSnapEnabled;
	bSymmetryMode = InArgs._bSymmetryMode;
	TextureBrush = InArgs._TextureBrush;
	OnAngleChanged = InArgs._OnAngleChanged;
	OnClicked = InArgs._OnClicked;
	OnDragStarted = InArgs._OnDragStarted;
	OnDragEnded = InArgs._OnDragEnded;

	auto ColorAttr = TAttribute<FSlateColor>::CreateLambda([this]() {
		return bIsActive.Get() ? FSlateColor(FLinearColor(1.0f, 0.6f, 0.1f, 1.0f)) : FSlateColor(FLinearColor(1.0f, 1.0f, 1.0f, 0.5f));
	});

	ChildSlot
	[
		SNew(SOverlay)
		
		// 1. The Needle (Background)
		+ SOverlay::Slot()
		.HAlign(HAlign_Center)
		.VAlign(VAlign_Fill)
		[
			SNew(SBox)
			.WidthOverride(2.0f)
			[
				SNew(SBorder)
				.BorderImage(FAppStyle::GetBrush("WhiteBrush"))
				.BorderBackgroundColor(TAttribute<FSlateColor>::CreateLambda([this]() {
					return bIsActive.Get() ? FLinearColor(1.0f, 0.6f, 0.1f, 1.0f) : FLinearColor(1.0f, 1.0f, 1.0f, 0.2f);
				}))
			]
		]

		// 2. The Small Handle Preview (Top)
		+ SOverlay::Slot()
		.HAlign(HAlign_Center)
		.VAlign(VAlign_Top)
		.Padding(0, 2, 0, 0)
		[
			SNew(SBorder)
			.BorderImage(FAppStyle::GetBrush("WhiteBrush"))
			.BorderBackgroundColor(TAttribute<FSlateColor>::CreateLambda([this]() {
				return bIsActive.Get() ? FLinearColor(1.0f, 0.6f, 0.1f, 1.0f) : FLinearColor(0.1f, 0.1f, 0.1f, 1.0f);
			}))
			.Padding(1.0f)
			[
				SNew(SBox)
				.WidthOverride(20.0f)
				.HeightOverride(20.0f)
				[
					SNew(SImage)
					.Image(TAttribute<const FSlateBrush*>::CreateLambda([this]() {
						return TextureBrush.Get() ? TextureBrush.Get() : FAppStyle::GetBrush("DefaultBrush");
					}))
				]
			]
		]

		// 3. The Angle Text (Bottom)
		+ SOverlay::Slot()
		.HAlign(HAlign_Center)
		.VAlign(VAlign_Bottom)
		.Padding(0, 0, 0, 0)
		[
			SNew(STextBlock)
			.Text(TAttribute<FText>::CreateLambda([this]() {
				return FText::FromString(FString::Printf(TEXT("%.0f\u00B0"), Angle.Get()));
			}))
			.ColorAndOpacity(ColorAttr)
			.Font(FCoreStyle::GetDefaultFontStyle("Bold", 7))
			.ShadowOffset(FVector2D(1, 1))
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
		.Padding(FMargin(10.0f, 0.0f, 10.0f, 0.0f))
		[
			SNew(SBox)
			.WidthOverride(800.0f) // Wider for timeline
			[
				SNew(SExpandableArea)
				.InitiallyCollapsed(false)
				.Padding(0.0f)
				.HeaderContent()
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					[
						SNew(STextBlock)
						.Text(LOCTEXT("TimelineAreaTitle", "Timeline"))
						.Font(FAppStyle::GetFontStyle("SmallFont"))
					]

					+ SHorizontalBox::Slot().FillWidth(1.0f) [ SNew(SSpacer) ]

					// Controls (paint toggles, snap, keyframe actions)
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					[
						SNew(SHorizontalBox)

						+ SHorizontalBox::Slot()
						.AutoWidth()
						.VAlign(VAlign_Center)
						.Padding(0.0f, 0.0f, 6.0f, 0.0f)
						[
							QuickSDFToolUI::MakePaintToggleBar([]()
							{
								return QuickSDFToolUI::GetActivePaintTool();
							})
						]

						// Snap Checkbox
						+ SHorizontalBox::Slot()
						.AutoWidth()
						.VAlign(VAlign_Center)
						.Padding(4.0f, 0.0f)
						[
							SNew(SCheckBox)
							.Style(FAppStyle::Get(), "ToggleButtonCheckbox")
							.ToolTipText(LOCTEXT("SnapTooltip", "Snap dragged timeline keys to 5 degree steps"))
							.IsChecked(this, &SQuickSDFTimeline::IsGridSnapEnabled)
							.OnCheckStateChanged(this, &SQuickSDFTimeline::OnGridSnapStateChanged)
							[
								SNew(SImage)
								.Image(FQuickSDFToolStyle::GetBrush("QuickSDF.Action.Snap"))
							]
						]

						// Controls (Add/Delete)
						+ SHorizontalBox::Slot()
						.AutoWidth()
						.Padding(2.0f, 0.0f)
						[
							QuickSDFToolUI::MakeIconLabelButton(
								"QuickSDF.Action.CompleteToEight",
								LOCTEXT("CompleteTo8Btn", "8"),
								LOCTEXT("CompleteTo8Tooltip", "Complete the mask set to eight angles"),
								FOnClicked::CreateSP(this, &SQuickSDFTimeline::OnCompleteToEightClicked))
						]

						+ SHorizontalBox::Slot()
						.AutoWidth()
						.Padding(2.0f, 0.0f)
						[
							QuickSDFToolUI::MakeIconLabelButton(
								"QuickSDF.Action.Redistribute",
								LOCTEXT("RedistributeBtn", "Even"),
								LOCTEXT("RedistributeTooltip", "Redistribute timeline angles evenly"),
								FOnClicked::CreateSP(this, &SQuickSDFTimeline::OnRedistributeEvenlyClicked))
						]

						+ SHorizontalBox::Slot()
						.AutoWidth()
						.Padding(2.0f, 0.0f)
						[
							MakeTimelineIconButton(
								"QuickSDF.Action.AddKey",
								LOCTEXT("AddFrameTooltip", "Add a timeline keyframe"),
								FOnClicked::CreateSP(this, &SQuickSDFTimeline::OnAddKeyframeClicked))
						]
						+ SHorizontalBox::Slot()
						.AutoWidth()
						.Padding(2.0f, 0.0f)
						[
							MakeTimelineIconButton(
								"QuickSDF.Action.DeleteKey",
								LOCTEXT("DelFrameTooltip", "Delete the selected timeline keyframe"),
								FOnClicked::CreateSP(this, &SQuickSDFTimeline::OnDeleteKeyframeClicked))
						]
						+ SHorizontalBox::Slot()
						.AutoWidth()
						.Padding(2.0f, 0.0f)
						[
							MakeTimelineIconButton(
								"QuickSDF.Toggle.AutoSyncLight",
								LOCTEXT("SyncLightTooltip", "Sync Directional Light to selected keyframe angle"),
								FOnClicked::CreateSP(this, &SQuickSDFTimeline::OnSyncLightClicked))
						]
					]
				]
				.BodyContent()
				[
					SNew(SBorder)
					.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
					.Padding(0.0f)
					[
						SNew(SVerticalBox)
						// Timeline Track
						+ SVerticalBox::Slot()
						.AutoHeight()
						.Padding(0.0f)
						[
							SNew(SBox)
							.ToolTipText(this, &SQuickSDFTimeline::GetCompactSummaryText)
							.HeightOverride(32.0f)
							[
								SNew(SOverlay)
								
								// Track background line
								+ SOverlay::Slot()
								.VAlign(VAlign_Center)
								[
									SNew(SBorder)
									.BorderImage(FAppStyle::GetBrush("WhiteBrush"))
									.BorderBackgroundColor(FLinearColor(0.01f, 0.01f, 0.01f, 1.0f))
									.Padding(FMargin(0, 1))
								]

								// Filmstrip decorative holes (Top)
								+ SOverlay::Slot()
								.VAlign(VAlign_Top)
								.Padding(0, 3)
								[
									SNew(SHorizontalBox)
									+ SHorizontalBox::Slot().AutoWidth() [ SNew(SBox).WidthOverride(6).HeightOverride(4).HAlign(HAlign_Center) [ SNew(SBorder).BorderBackgroundColor(FLinearColor(0.15f, 0.15f, 0.15f, 1.0f)).BorderImage(FAppStyle::GetBrush("WhiteBrush")) ] ]
									+ SHorizontalBox::Slot().FillWidth(1.0f) [ SNew(SSpacer) ]
									+ SHorizontalBox::Slot().AutoWidth() [ SNew(SBox).WidthOverride(6).HeightOverride(4).HAlign(HAlign_Center) [ SNew(SBorder).BorderBackgroundColor(FLinearColor(0.15f, 0.15f, 0.15f, 1.0f)).BorderImage(FAppStyle::GetBrush("WhiteBrush")) ] ]
								]

								// Filmstrip decorative holes (Bottom)
								+ SOverlay::Slot()
								.VAlign(VAlign_Bottom)
								.Padding(0, 3)
								[
									SNew(SHorizontalBox)
									+ SHorizontalBox::Slot().AutoWidth() [ SNew(SBox).WidthOverride(6).HeightOverride(4).HAlign(HAlign_Center) [ SNew(SBorder).BorderBackgroundColor(FLinearColor(0.15f, 0.15f, 0.15f, 1.0f)).BorderImage(FAppStyle::GetBrush("WhiteBrush")) ] ]
									+ SHorizontalBox::Slot().FillWidth(1.0f) [ SNew(SSpacer) ]
									+ SHorizontalBox::Slot().AutoWidth() [ SNew(SBox).WidthOverride(6).HeightOverride(4).HAlign(HAlign_Center) [ SNew(SBorder).BorderBackgroundColor(FLinearColor(0.15f, 0.15f, 0.15f, 1.0f)).BorderImage(FAppStyle::GetBrush("WhiteBrush")) ] ]
								]

								// The actual canvas for keyframes
								+ SOverlay::Slot()
								[
									SAssignNew(TimelineTrackCanvas, SCanvas)
								]
							]
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

	if (!bNeedsRebuild && CachedMaskRevision != Tool->GetMaskRevision())
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
		CachedMaskRevision = Tool->GetMaskRevision();
		CachedAngles = Props->TargetAngles;
		CachedTextures = Props->TargetTextures;

		RebuildTimeline();
	}
}

FReply SQuickSDFTimeline::OnDragOver(const FGeometry& MyGeometry, const FDragDropEvent& DragDropEvent)
{
	TSharedPtr<FAssetDragDropOp> AssetDragDropOp = DragDropEvent.GetOperationAs<FAssetDragDropOp>();
	return AssetDragDropOp.IsValid() && AssetDragDropOp->HasAssets()
		? FReply::Handled()
		: FReply::Unhandled();
}

FReply SQuickSDFTimeline::OnDrop(const FGeometry& MyGeometry, const FDragDropEvent& DragDropEvent)
{
	TSharedPtr<FAssetDragDropOp> AssetDragDropOp = DragDropEvent.GetOperationAs<FAssetDragDropOp>();
	if (!AssetDragDropOp.IsValid() || !AssetDragDropOp->HasAssets())
	{
		return FReply::Unhandled();
	}

	TArray<UTexture2D*> Textures;
	for (const FAssetData& AssetData : AssetDragDropOp->GetAssets())
	{
		if (UTexture2D* Texture = Cast<UTexture2D>(AssetData.GetAsset()))
		{
			Textures.Add(Texture);
		}
	}

	if (Textures.Num() == 0)
	{
		return FReply::Unhandled();
	}

	if (UQuickSDFPaintTool* Tool = GetActivePaintTool())
	{
		Tool->ImportEditedMasksFromTextures(Textures);
		return FReply::Handled();
	}

	return FReply::Unhandled();
}

FReply SQuickSDFTimeline::OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (MouseEvent.GetEffectingButton() == EKeys::LeftMouseButton &&
		IsTimelineTrackUnderCursor(MouseEvent.GetScreenSpacePosition()) &&
		!IsNearKeyframeHandle(MouseEvent.GetScreenSpacePosition()))
	{
		bSeekingTimeline = true;
		SeekTimelineAtScreenPosition(MouseEvent.GetScreenSpacePosition());
		return FReply::Handled().CaptureMouse(SharedThis(this));
	}

	return SCompoundWidget::OnMouseButtonDown(MyGeometry, MouseEvent);
}

FReply SQuickSDFTimeline::OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (MouseEvent.GetEffectingButton() == EKeys::LeftMouseButton && bSeekingTimeline)
	{
		bSeekingTimeline = false;
		return FReply::Handled().ReleaseMouseCapture();
	}

	return SCompoundWidget::OnMouseButtonUp(MyGeometry, MouseEvent);
}

void SQuickSDFTimeline::OnMouseCaptureLost(const FCaptureLostEvent& CaptureLostEvent)
{
	bSeekingTimeline = false;
	if (bTimelineDragTransactionOpen)
	{
		OnKeyframeDragEnded();
	}
}

FReply SQuickSDFTimeline::OnMouseMove(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (bSeekingTimeline)
	{
		SeekTimelineAtScreenPosition(MouseEvent.GetScreenSpacePosition());
		return FReply::Handled();
	}

	return SCompoundWidget::OnMouseMove(MyGeometry, MouseEvent);
}

EVisibility SQuickSDFTimeline::GetRefineVisibility() const
{
	return EVisibility::Visible;
}

EVisibility SQuickSDFTimeline::GetCompactVisibility() const
{
	return EVisibility::Visible;
}

FText SQuickSDFTimeline::GetCompactSummaryText() const
{
	const UQuickSDFPaintTool* Tool = GetActivePaintTool();
	const UQuickSDFToolProperties* Props = Tool ? Tool->Properties : nullptr;
	const int32 MaskCount = Props ? Props->NumAngles : 0;
	const int32 CurrentIndex = Props ? FMath::Clamp(Props->EditAngleIndex, 0, FMath::Max(Props->NumAngles - 1, 0)) : 0;
	const float CurrentAngle = Props && Props->TargetAngles.IsValidIndex(CurrentIndex) ? Props->TargetAngles[CurrentIndex] : 0.0f;
	return FText::Format(LOCTEXT("CompactSummary", "{0} masks ready. Current {1}: {2} deg. Click or drag the timeline to seek."), MaskCount, CurrentIndex + 1, FText::AsNumber(FMath::RoundToInt(CurrentAngle)));
}

ECheckBoxState SQuickSDFTimeline::IsGridSnapEnabled() const
{
	return bGridSnapEnabled ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
}

void SQuickSDFTimeline::OnGridSnapStateChanged(ECheckBoxState NewState)
{
	bGridSnapEnabled = (NewState == ECheckBoxState::Checked);
}

ECheckBoxState SQuickSDFTimeline::IsSymmetryModeEnabled() const
{
	UQuickSDFPaintTool* Tool = GetActivePaintTool();
	if (Tool && Tool->Properties)
	{
		return Tool->Properties->bSymmetryMode ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
	}
	return ECheckBoxState::Unchecked;
}

void SQuickSDFTimeline::OnSymmetryModeStateChanged(ECheckBoxState NewState)
{
	UQuickSDFPaintTool* Tool = GetActivePaintTool();
	if (Tool && Tool->Properties)
	{
		Tool->Properties->bSymmetryMode = (NewState == ECheckBoxState::Checked);
		
		// Rebuild the timeline to reflect the new range
		RebuildTimeline();
	}
}

float SQuickSDFTimeline::GetCurrentLightYaw() const
{
	UQuickSDFPaintTool* Tool = GetActivePaintTool();
	if (!Tool) return 0.0f;

	// Use mesh relative calculation if we have a target component
	TObjectPtr<UMeshComponent> MeshComp = Tool->CurrentComponent.Get();

	if (Tool->GetToolManager() && Tool->GetToolManager()->GetContextQueriesAPI())
	{
		if (UWorld* World = Tool->GetToolManager()->GetContextQueriesAPI()->GetCurrentEditingWorld())
		{
			// Get the mode's preview light specifically
			UQuickSDFEditorMode* Mode = Cast<UQuickSDFEditorMode>(GLevelEditorModeTools().GetActiveScriptableMode("EM_QuickSDFEditorMode"));
			ADirectionalLight* DirLight = Mode ? Mode->GetPreviewLight() : nullptr;

			if (DirLight)
			{
				float RelativeAngle = 0.0f;

					if (MeshComp)
					{
						FVector MeshForward = MeshComp->GetForwardVector();
						FVector MeshRight = MeshComp->GetRightVector();
						FVector LightForward = DirLight->GetActorForwardVector();

						// Project light direction onto the mesh's horizontal plane
						float ProjX = FVector::DotProduct(LightForward, MeshForward);
						float ProjY = FVector::DotProduct(LightForward, MeshRight);

						// If the light is behind the mesh, we don't show it on the front-sweep timeline
						if (ProjX > 0.0f)
						{
							return -1.0f;
						}

						// The user wants:
						// Front (-MeshForward) -> 90 degrees
						// Left Side (Light points Right, MeshRight) -> 0 degrees
						// Right Side (Light points Left, -MeshRight) -> 180 degrees
						// Formula: Atan2(-ProjY, -ProjX) + 90
						// Front (-1, 0) -> Atan2(0, 1) = 0 -> 90
						// Left (0, 1)   -> Atan2(-1, 0) = -90 -> 0
						// Right (0, -1) -> Atan2(1, 0) = 90 -> 180
						RelativeAngle = FMath::RadiansToDegrees(FMath::Atan2(-ProjY, -ProjX)) + 90.0f;
					}
					else
					{
						// Fallback to world yaw if no mesh is selected
						RelativeAngle = DirLight->GetActorRotation().Yaw;
						while (RelativeAngle < 0.0f) RelativeAngle += 360.0f;
						while (RelativeAngle >= 360.0f) RelativeAngle -= 360.0f;
						if (RelativeAngle > 180.0f) return -1.0f;
					}
					
					// If symmetry mode is on, we only care about 0-90
					bool bSymmetry = Tool->Properties && Tool->Properties->bSymmetryMode;
					if (bSymmetry)
					{
						// Map 0-180 to 0-90 in a symmetric way around the front (90)
						// 90 (Front) -> 90
						// 0 (Left Side) -> 0
						// 180 (Right Side) -> 0
						if (RelativeAngle > 90.0f) RelativeAngle = 180.0f - RelativeAngle;
						return FMath::Clamp(RelativeAngle, 0.0f, 90.0f);
					}
					
					return FMath::Clamp(RelativeAngle, 0.0f, 180.0f);
				}
		}
	}
	return 0.0f;
}

bool SQuickSDFTimeline::IsTimelineTrackUnderCursor(const FVector2D& ScreenPosition) const
{
	if (!TimelineTrackCanvas.IsValid())
	{
		return false;
	}

	const FGeometry TrackGeometry = TimelineTrackCanvas->GetTickSpaceGeometry();
	const FVector2D LocalPosition = TrackGeometry.AbsoluteToLocal(ScreenPosition);
	const FVector2D LocalSize = TrackGeometry.GetLocalSize();
	return LocalPosition.X >= 0.0 && LocalPosition.Y >= 0.0 &&
		LocalPosition.X <= LocalSize.X && LocalPosition.Y <= LocalSize.Y;
}

bool SQuickSDFTimeline::IsNearKeyframeHandle(const FVector2D& ScreenPosition) const
{
	if (!TimelineTrackCanvas.IsValid())
	{
		return false;
	}

	const UQuickSDFPaintTool* Tool = GetActivePaintTool();
	const UQuickSDFToolProperties* Props = Tool ? Tool->Properties : nullptr;
	if (!Props)
	{
		return false;
	}

	const FGeometry TrackGeometry = TimelineTrackCanvas->GetTickSpaceGeometry();
	const FVector2D LocalPosition = TrackGeometry.AbsoluteToLocal(ScreenPosition);
	const float TrackWidth = FMath::Max(TrackGeometry.GetLocalSize().X - 40.0f, 1.0f);
	const float MaxAngle = Props->bSymmetryMode ? 90.0f : 180.0f;

	for (float Angle : Props->TargetAngles)
	{
		if (Props->bSymmetryMode && Angle > MaxAngle)
		{
			continue;
		}

		const float KeyX = FMath::Clamp(Angle / MaxAngle, 0.0f, 1.0f) * TrackWidth + 20.0f;
		if (FMath::Abs(LocalPosition.X - KeyX) <= QuickSDFTimelineKeyframeHitSlop)
		{
			return true;
		}
	}

	return false;
}

void SQuickSDFTimeline::SeekTimelineAtScreenPosition(const FVector2D& ScreenPosition)
{
	UQuickSDFPaintTool* Tool = GetActivePaintTool();
	if (!Tool || !Tool->Properties || !TimelineTrackCanvas.IsValid())
	{
		return;
	}

	UQuickSDFToolProperties* Props = Tool->Properties;
	if (Props->TargetAngles.Num() == 0)
	{
		return;
	}

	const FGeometry TrackGeometry = TimelineTrackCanvas->GetTickSpaceGeometry();
	const FVector2D LocalPosition = TrackGeometry.AbsoluteToLocal(ScreenPosition);
	const float TrackWidth = FMath::Max(TrackGeometry.GetLocalSize().X - 40.0f, 1.0f);
	const float MaxAngle = Props->bSymmetryMode ? 90.0f : 180.0f;
	const float SeekPercent = FMath::Clamp((LocalPosition.X - 20.0f) / TrackWidth, 0.0f, 1.0f);
	const float SeekAngle = SeekPercent * MaxAngle;

	int32 BestIndex = INDEX_NONE;
	float BestDistance = TNumericLimits<float>::Max();
	for (int32 Index = 0; Index < Props->TargetAngles.Num(); ++Index)
	{
		if (Props->bSymmetryMode && Props->TargetAngles[Index] > MaxAngle)
		{
			continue;
		}

		const float Distance = FMath::Abs(Props->TargetAngles[Index] - SeekAngle);
		if (Distance < BestDistance)
		{
			BestDistance = Distance;
			BestIndex = Index;
		}
	}

	if (BestIndex == INDEX_NONE)
	{
		return;
	}

	if (Props->EditAngleIndex != BestIndex)
	{
		Props->EditAngleIndex = BestIndex;
		FProperty* Prop = Props->GetClass()->FindPropertyByName(GET_MEMBER_NAME_CHECKED(UQuickSDFToolProperties, EditAngleIndex));
		Tool->OnPropertyModified(Props, Prop);
	}

	if (Props->bAutoSyncLight)
	{
		if (UQuickSDFEditorMode* Mode = Cast<UQuickSDFEditorMode>(GLevelEditorModeTools().GetActiveScriptableMode("EM_QuickSDFEditorMode")))
		{
			Mode->SetPreviewLightAngle(SeekAngle);
		}
	}

	Invalidate(EInvalidateWidgetReason::PaintAndVolatility);
	if (GEditor)
	{
		GEditor->RedrawAllViewports(false);
	}
}

void SQuickSDFTimeline::RebuildTimeline()
{
	if (!TimelineTrackCanvas.IsValid()) return;
	
	TimelineTrackCanvas->ClearChildren();
	KeyframeBrushes.Empty();
	ThumbnailTextures.Empty();

	UQuickSDFPaintTool* Tool = GetActivePaintTool();
	if (!Tool) return;

	UQuickSDFToolProperties* Props = Tool->Properties;
	if (!Props) return;

	UQuickSDFToolSubsystem* Subsystem = GEditor->GetEditorSubsystem<UQuickSDFToolSubsystem>();
	UQuickSDFAsset* Asset = Subsystem ? Subsystem->GetActiveSDFAsset() : nullptr;

	if (Asset)
	{
		for (int32 i = 0; i < Asset->AngleDataList.Num(); ++i)
		{
			UTexture2D* ThumbnailTexture = CreateTimelineThumbnailTexture(Tool, Asset->AngleDataList[i].PaintRenderTarget);
			if (ThumbnailTexture)
			{
				ThumbnailTextures.Add(TStrongObjectPtr<UTexture2D>(ThumbnailTexture));
			}
			
			UTexture* Tex = ThumbnailTexture ? ThumbnailTexture : Asset->AngleDataList[i].TextureMask;
			if (Tex)
			{
				KeyframeBrushes.Add(MakeShared<FSlateImageBrush>(Tex, FVector2D(64, 64)));
			}
			else
			{
				KeyframeBrushes.Add(nullptr);
			}
		}
	}

	float CanvasHeight = 32.0f;
	float KeyframeWidth = 24.0f;

	// 1. Add Filmstrip Background (One segment per keyframe)
	for (int32 i = 0; i < Props->NumAngles; ++i)
	{
		// 'i' here is the visual order (0th smallest, 1st smallest...)
		TimelineTrackCanvas->AddSlot()
		.Position(TAttribute<FVector2D>::CreateLambda([this, i]() {
			UQuickSDFPaintTool* Tool = this->GetActivePaintTool();
			if (!Tool || !Tool->Properties) return FVector2D::ZeroVector;
			UQuickSDFToolProperties* P = Tool->Properties;

			bool bSymmetry = P->bSymmetryMode;
			float MaxAngle = bSymmetry ? 90.0f : 180.0f;

			TArray<int32> Indices;
			for (int32 k = 0; k < P->TargetAngles.Num(); ++k)
			{
				if (!bSymmetry || P->TargetAngles[k] <= MaxAngle) Indices.Add(k);
			}
			Indices.Sort([P](int32 A, int32 B) { return P->TargetAngles[A] < P->TargetAngles[B]; });

			if (!Indices.IsValidIndex(i)) return FVector2D::ZeroVector;

			float TrackWidth = TimelineTrackCanvas->GetTickSpaceGeometry().GetLocalSize().X - 40.0f;
			float PrevAngle = (i == 0) ? 0.0f : P->TargetAngles[Indices[i - 1]];
			float CurrAngle = P->TargetAngles[Indices[i]];
			float L = (i == 0) ? 0.0f : (PrevAngle + CurrAngle) * 0.5f;
			
			return FVector2D(FMath::Max(0.0f, TrackWidth) * (L / MaxAngle) + 20.0f, 0.0f);
		}))
		.Size(TAttribute<FVector2D>::CreateLambda([this, i, CanvasHeight]() {
			UQuickSDFPaintTool* Tool = this->GetActivePaintTool();
			if (!Tool || !Tool->Properties) return FVector2D::ZeroVector;
			UQuickSDFToolProperties* P = Tool->Properties;

			bool bSymmetry = P->bSymmetryMode;
			float MaxAngle = bSymmetry ? 90.0f : 180.0f;

			TArray<int32> Indices;
			for (int32 k = 0; k < P->TargetAngles.Num(); ++k)
			{
				if (!bSymmetry || P->TargetAngles[k] <= MaxAngle) Indices.Add(k);
			}
			Indices.Sort([P](int32 A, int32 B) { return P->TargetAngles[A] < P->TargetAngles[B]; });

			if (!Indices.IsValidIndex(i)) return FVector2D::ZeroVector;

			float TrackWidth = TimelineTrackCanvas->GetTickSpaceGeometry().GetLocalSize().X - 40.0f;
			float PrevAngle = (i == 0) ? 0.0f : P->TargetAngles[Indices[i - 1]];
			float CurrAngle = P->TargetAngles[Indices[i]];
			float NextAngle = (i == Indices.Num() - 1) ? MaxAngle : P->TargetAngles[Indices[i + 1]];
			
			float L = (i == 0) ? 0.0f : (PrevAngle + CurrAngle) * 0.5f;
			float R = (i == Indices.Num() - 1) ? MaxAngle : (CurrAngle + NextAngle) * 0.5f;
			
			return FVector2D(FMath::Max(0.0f, TrackWidth) * ((R - L) / MaxAngle), CanvasHeight);
		}))
		[
			SNew(SBorder)
			.BorderImage(FAppStyle::GetBrush("WhiteBrush"))
			.BorderBackgroundColor(FLinearColor(0.05f, 0.05f, 0.05f, 1.0f))
			.Padding(1.0f)
			[
				SNew(SScaleBox)
				.Stretch(EStretch::ScaleToFill)
				[
					SNew(SImage)
				.Image(TAttribute<const FSlateBrush*>::CreateLambda([this, i]() -> FSlateBrush* {
					UQuickSDFPaintTool* Tool = this->GetActivePaintTool();
					if (!Tool || !Tool->Properties) return nullptr;
					UQuickSDFToolProperties* P = Tool->Properties;

					bool bSymmetry = P->bSymmetryMode;
					float MaxAngle = bSymmetry ? 90.0f : 180.0f;

					TArray<int32> Indices;
					for (int32 k = 0; k < P->TargetAngles.Num(); ++k)
					{
						if (!bSymmetry || P->TargetAngles[k] <= MaxAngle) Indices.Add(k);
					}
					Indices.Sort([P](int32 A, int32 B) { return P->TargetAngles[A] < P->TargetAngles[B]; });

					if (Indices.IsValidIndex(i))
					{
						int32 KeyIndex = Indices[i];
						if (KeyframeBrushes.IsValidIndex(KeyIndex)) return KeyframeBrushes[KeyIndex].Get();
					}
					return nullptr;
				}))
				]
			]
		];
	}

	// 2. Add tick marks (Middle layer)
	bool bSymmetryModeActive = Props->bSymmetryMode;
	int32 NumTicks = bSymmetryModeActive ? 1 : 2;
	float TickStep = 90.0f;
	float MaxTimelineAngle = bSymmetryModeActive ? 90.0f : 180.0f;

	for (int32 i = 0; i <= NumTicks; ++i)
	{
		float TickAngle = i * TickStep;
		float Percent = TickAngle / MaxTimelineAngle;
		
		TimelineTrackCanvas->AddSlot()
		.Position(TAttribute<FVector2D>::CreateLambda([this, Percent]() {
			float TrackWidth = TimelineTrackCanvas->GetTickSpaceGeometry().GetLocalSize().X - 40.0f;
			return FVector2D(FMath::Max(0.0f, TrackWidth) * Percent + 19.0f, 0.0f);
		}))
		.Size(FVector2D(2.0f, CanvasHeight))
		[
			SNew(SBorder)
			.BorderImage(FAppStyle::GetBrush("WhiteBrush"))
			.BorderBackgroundColor(FLinearColor(0.5f, 0.5f, 0.5f, 0.3f))
		];
	}

	// 3. Add Keyframes in two passes to handle "Z-Order" (Active on top)
	auto AddKeyframeToCanvas = [this, Props, CanvasHeight, KeyframeWidth](int32 i)
	{
		TimelineTrackCanvas->AddSlot()
		.Position(TAttribute<FVector2D>::CreateLambda([this, i, KeyframeWidth]() {
			UQuickSDFPaintTool* ActiveTool = this->GetActivePaintTool();
			if (ActiveTool && ActiveTool->Properties && ActiveTool->Properties->TargetAngles.IsValidIndex(i))
			{
				float CurrentAngle = ActiveTool->Properties->TargetAngles[i];
				bool bSymmetry = ActiveTool->Properties->bSymmetryMode;
				float MaxAngle = bSymmetry ? 90.0f : 180.0f;

				if (bSymmetry && CurrentAngle > MaxAngle) return FVector2D(-1000.0f, -1000.0f); // Hide

				float Percent = FMath::Clamp(CurrentAngle / MaxAngle, 0.0f, 1.0f);
				float TrackWidth = TimelineTrackCanvas->GetTickSpaceGeometry().GetLocalSize().X - 40.0f;
				return FVector2D(FMath::Max(0.0f, TrackWidth) * Percent + 20.0f - (KeyframeWidth * 0.5f), 0.0f);
			}
			return FVector2D::ZeroVector;
		}))
		.Size(FVector2D(KeyframeWidth, CanvasHeight))
		[
			SNew(SQuickSDFTimelineKeyframe)
			.Index(i)
			.Angle(TAttribute<float>::CreateLambda([this, i]() {
				UQuickSDFPaintTool* ActiveTool = this->GetActivePaintTool();
				if (ActiveTool && ActiveTool->Properties && ActiveTool->Properties->TargetAngles.IsValidIndex(i))
					return ActiveTool->Properties->TargetAngles[i];
				return 0.0f;
			}))
			.bIsActive(TAttribute<bool>::CreateLambda([this, i]() {
				UQuickSDFPaintTool* ActiveTool = this->GetActivePaintTool();
				return ActiveTool && ActiveTool->Properties && ActiveTool->Properties->EditAngleIndex == i;
			}))
			.bSnapEnabled(TAttribute<bool>::CreateLambda([this]() { return bGridSnapEnabled; }))
			.bSymmetryMode(TAttribute<bool>::CreateLambda([this]() {
				UQuickSDFPaintTool* ActiveTool = this->GetActivePaintTool();
				return ActiveTool && ActiveTool->Properties && ActiveTool->Properties->bSymmetryMode;
			}))
			.TextureBrush(TAttribute<FSlateBrush*>::CreateLambda([this, i]() -> FSlateBrush* {
				if (KeyframeBrushes.IsValidIndex(i)) return KeyframeBrushes[i].Get();
				return nullptr;
			}))
			.OnClicked(this, &SQuickSDFTimeline::OnKeyframeClicked, i)
			.OnAngleChanged(this, &SQuickSDFTimeline::OnKeyframeAngleChanged, i)
			.OnDragStarted(this, &SQuickSDFTimeline::OnKeyframeDragStarted)
			.OnDragEnded(this, &SQuickSDFTimeline::OnKeyframeDragEnded)
		];
	};

	// Pass 1: Non-active keyframes
	for (int32 i = 0; i < Props->NumAngles; ++i)
	{
		if (Props->EditAngleIndex != i)
		{
			AddKeyframeToCanvas(i);
		}
	}

	// Pass 2: Active keyframe (should be on top of other keyframes)
	if (Props->TargetAngles.IsValidIndex(Props->EditAngleIndex))
	{
		AddKeyframeToCanvas(Props->EditAngleIndex);
	}

	TimelineTrackCanvas->AddSlot()
	.Position(TAttribute<FVector2D>::CreateLambda([this]() {
		UQuickSDFPaintTool* Tool = this->GetActivePaintTool();
		bool bSymmetry = Tool && Tool->Properties && Tool->Properties->bSymmetryMode;
		float MaxAngle = bSymmetry ? 90.0f : 180.0f;
		
		float LightYaw = GetCurrentLightYaw();
		if (LightYaw < 0.0f) return FVector2D(-1000.0f, -1000.0f); // Hide if in back hemisphere

		float Percent = FMath::Clamp(LightYaw / MaxAngle, 0.0f, 1.0f);
		float TrackWidth = TimelineTrackCanvas->GetTickSpaceGeometry().GetLocalSize().X - 40.0f;
		return FVector2D(FMath::Max(0.0f, TrackWidth) * Percent + 19.0f, 0.0f);
	}))
	.Size(FVector2D(2.0f, CanvasHeight))
	[
		SNew(SBorder)
		.BorderImage(FAppStyle::GetBrush("WhiteBrush"))
		.BorderBackgroundColor(FLinearColor(1.0f, 1.0f, 0.0f, 0.8f)) // Yellow
	];
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

FReply SQuickSDFTimeline::OnCompleteToEightClicked()
{
	if (UQuickSDFPaintTool* Tool = GetActivePaintTool())
	{
		Tool->CompleteToEightMasks();
	}
	return FReply::Handled();
}

FReply SQuickSDFTimeline::OnRedistributeEvenlyClicked()
{
	if (UQuickSDFPaintTool* Tool = GetActivePaintTool())
	{
		Tool->RedistributeAnglesEvenly();
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

			if (Props->bAutoSyncLight)
			{
				OnSyncLightClicked();
			}

			Invalidate(EInvalidateWidgetReason::PaintAndVolatility);
			if (GEditor)
			{
				GEditor->RedrawAllViewports(false);
			}
		}
	}
}

FReply SQuickSDFTimeline::OnSyncLightClicked()
{
	UQuickSDFPaintTool* Tool = GetActivePaintTool();
	if (!Tool || !Tool->Properties) return FReply::Handled();

	UQuickSDFEditorMode* Mode = Cast<UQuickSDFEditorMode>(GLevelEditorModeTools().GetActiveScriptableMode("EM_QuickSDFEditorMode"));
	if (!Mode) return FReply::Handled();

	int32 Index = Tool->Properties->EditAngleIndex;
	if (Tool->Properties->TargetAngles.IsValidIndex(Index))
	{
		Mode->SetPreviewLightAngle(Tool->Properties->TargetAngles[Index]);
	}

	return FReply::Handled();
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
				
				UQuickSDFToolSubsystem* Subsystem = GEditor->GetEditorSubsystem<UQuickSDFToolSubsystem>();
				if (Subsystem && Subsystem->GetActiveSDFAsset())
				{
					if (Subsystem->GetActiveSDFAsset()->AngleDataList.IsValidIndex(Index))
					{
						Subsystem->GetActiveSDFAsset()->AngleDataList[Index].Angle = NewAngle;
					}
				}

				// Fire property modified to update the preview light
				FProperty* Prop = Props->GetClass()->FindPropertyByName(GET_MEMBER_NAME_CHECKED(UQuickSDFToolProperties, TargetAngles));
				Tool->OnPropertyModified(Props, Prop);
				Invalidate(EInvalidateWidgetReason::PaintAndVolatility);
				if (GEditor)
				{
					GEditor->RedrawAllViewports(false);
				}
			}
		}
	}
}

void SQuickSDFTimeline::OnKeyframeDragStarted()
{
	if (bTimelineDragTransactionOpen)
	{
		return;
	}

	UQuickSDFPaintTool* Tool = GetActivePaintTool();
	if (Tool && Tool->GetToolManager())
	{
		Tool->GetToolManager()->BeginUndoTransaction(LOCTEXT("TimelineDrag", "Drag Timeline Keyframe"));
		bTimelineDragTransactionOpen = true;
		
		UQuickSDFToolSubsystem* Subsystem = GEditor->GetEditorSubsystem<UQuickSDFToolSubsystem>();
		if (Subsystem && Subsystem->GetActiveSDFAsset())
		{
			Subsystem->GetActiveSDFAsset()->Modify();
		}

		if (Tool->Properties)
		{
			Tool->Properties->Modify();
		}
	}
}

void SQuickSDFTimeline::OnKeyframeDragEnded()
{
	if (!bTimelineDragTransactionOpen)
	{
		return;
	}

	UQuickSDFPaintTool* Tool = GetActivePaintTool();
	if (Tool && Tool->GetToolManager())
	{
		Tool->GetToolManager()->EndUndoTransaction();
	}
	bTimelineDragTransactionOpen = false;
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
