#include "SQuickSDFTimeline.h"
#include "QuickSDFPaintTool.h"
#include "QuickSDFPaintToolPrivate.h"
#include "QuickSDFEditorMode.h"
#include "DragAndDrop/AssetDragDropOp.h"
#include "EditorModeManager.h"
#include "InteractiveToolManager.h"
#include "Input/DragAndDrop.h"
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
#include "CanvasItem.h"
#include "CanvasTypes.h"
#include "Engine/Canvas.h"
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
constexpr float QuickSDFTimelineKeyframeHitSlop = 14.0f;
constexpr float QuickSDFTimelineSeekLaneHeight = 28.0f;
constexpr float QuickSDFTimelineKeyframeLaneHeight = 44.0f;
constexpr float QuickSDFTimelineTrackHeight = QuickSDFTimelineSeekLaneHeight + QuickSDFTimelineKeyframeLaneHeight;
constexpr float QuickSDFTimelineKeyframeWidth = 26.0f;
constexpr float QuickSDFTimelineButtonHeight = 24.0f;
constexpr float QuickSDFTimelineIconButtonWidth = 28.0f;
constexpr float QuickSDFTimelineAccentR = 0.35f;
constexpr float QuickSDFTimelineAccentG = 0.82f;
constexpr float QuickSDFTimelineAccentB = 1.0f;

FLinearColor GetQuickSDFTimelineAccentColor(float Alpha = 1.0f)
{
	return FLinearColor(QuickSDFTimelineAccentR, QuickSDFTimelineAccentG, QuickSDFTimelineAccentB, Alpha);
}

TSharedRef<SWidget> MakeTimelineIconButton(const FName IconName, const FText& ToolTip, FOnClicked OnClicked)
{
	return SNew(SBox)
		.WidthOverride(QuickSDFTimelineIconButtonWidth)
		.HeightOverride(QuickSDFTimelineButtonHeight)
		[
			SNew(SButton)
			.ButtonStyle(FQuickSDFToolStyle::Get().Get(), "QuickSDF.Timeline.Button")
			.HAlign(HAlign_Center)
			.VAlign(VAlign_Center)
			.OnClicked(OnClicked)
			.ContentPadding(FMargin(0.0f))
			.ToolTipText(ToolTip)
			[
				SNew(SBox)
				.WidthOverride(16.0f)
				.HeightOverride(16.0f)
				[
					SNew(SImage)
					.Image(FQuickSDFToolStyle::GetBrush(IconName))
				]
			]
		];
}

TSharedRef<SWidget> MakeTimelineIconLabelButton(const FName IconName, TAttribute<FText> Label, TAttribute<FText> ToolTip, FOnClicked OnClicked, float Width)
{
	return SNew(SBox)
		.WidthOverride(Width)
		.HeightOverride(QuickSDFTimelineButtonHeight)
		[
			SNew(SButton)
			.ButtonStyle(FQuickSDFToolStyle::Get().Get(), "QuickSDF.Timeline.Button")
			.HAlign(HAlign_Center)
			.VAlign(VAlign_Center)
			.OnClicked(OnClicked)
			.ContentPadding(FMargin(4.0f, 0.0f))
			.ToolTipText(ToolTip)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				.Padding(0.0f, 0.0f, 4.0f, 0.0f)
				[
					SNew(SBox)
					.WidthOverride(16.0f)
					.HeightOverride(16.0f)
					[
						SNew(SImage)
						.Image(FQuickSDFToolStyle::GetBrush(IconName))
					]
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				[
					SNew(STextBlock)
					.Text(Label)
					.Font(FAppStyle::GetFontStyle("SmallFont"))
				]
			]
		];
}

TSharedRef<SWidget> MakeTimelineSeparator()
{
	return SNew(SBox)
		.WidthOverride(1.0f)
		.HeightOverride(18.0f)
		[
			SNew(SBorder)
			.BorderImage(FAppStyle::GetBrush("WhiteBrush"))
			.BorderBackgroundColor(FLinearColor(1.0f, 1.0f, 1.0f, 0.12f))
		];
}

UTexture2D* CreateTimelineThumbnailTexture(UQuickSDFPaintTool* Tool, UTextureRenderTarget2D* RenderTarget)
{
	if (!Tool || !RenderTarget)
	{
		return nullptr;
	}

	UTextureRenderTarget2D* ThumbnailRenderTarget = NewObject<UTextureRenderTarget2D>(GetTransientPackage(), NAME_None, RF_Transient);
	if (!ThumbnailRenderTarget)
	{
		return nullptr;
	}
	ThumbnailRenderTarget->RenderTargetFormat = RTF_RGBA8;
	ThumbnailRenderTarget->ClearColor = FLinearColor::Black;
	ThumbnailRenderTarget->SRGB = false;
	ThumbnailRenderTarget->InitAutoFormat(QuickSDFTimelineThumbnailSize, QuickSDFTimelineThumbnailSize);
	ThumbnailRenderTarget->UpdateResourceImmediate(true);

	TArray<FColor> ThumbnailPixels;
	FTextureRenderTargetResource* ThumbnailResource = ThumbnailRenderTarget->GameThread_GetRenderTargetResource();
	if (!ThumbnailResource)
	{
		return nullptr;
	}

	FCanvas Canvas(ThumbnailResource, nullptr, GEditor ? GEditor->GetEditorWorldContext().World() : nullptr, GMaxRHIFeatureLevel);
	FCanvasTileItem TileItem(
		FVector2D::ZeroVector,
		RenderTarget->GetResource(),
		FVector2D(QuickSDFTimelineThumbnailSize, QuickSDFTimelineThumbnailSize),
		FLinearColor::White);
	TileItem.BlendMode = SE_BLEND_Opaque;
	Canvas.DrawItem(TileItem);
	Canvas.Flush_GameThread(true);

	if (!Tool->CaptureRenderTargetPixels(ThumbnailRenderTarget, ThumbnailPixels) ||
		ThumbnailPixels.Num() != QuickSDFTimelineThumbnailSize * QuickSDFTimelineThumbnailSize)
	{
		return nullptr;
	}

	for (FColor& Pixel : ThumbnailPixels)
	{
		const uint8 Value = FMath::Max3(Pixel.R, Pixel.G, Pixel.B);
		Pixel = FColor(Value, Value, Value, 255);
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
					.Padding(0.0f, 0.0f, 8.0f, 0.0f)
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
						+ SHorizontalBox::Slot()
						.AutoWidth()
						.VAlign(VAlign_Center)
						.Padding(8.0f, 0.0f, 0.0f, 0.0f)
						[
							SNew(STextBlock)
							.Text(this, &SQuickSDFTimeline::GetHeaderStatusText)
							.Font(FAppStyle::GetFontStyle("SmallFont"))
							.ColorAndOpacity(FLinearColor(0.62f, 0.62f, 0.62f, 1.0f))
						]
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

						+ SHorizontalBox::Slot()
						.AutoWidth()
						.VAlign(VAlign_Center)
						.Padding(2.0f, 0.0f, 7.0f, 0.0f)
						[
							MakeTimelineSeparator()
						]

						+ SHorizontalBox::Slot()
						.AutoWidth()
						.VAlign(VAlign_Center)
						.Padding(0.0f, 0.0f, 2.0f, 0.0f)
						[
							SNew(SBox)
							.WidthOverride(QuickSDFTimelineIconButtonWidth)
							.HeightOverride(QuickSDFTimelineButtonHeight)
							[
								SNew(SCheckBox)
								.Style(FQuickSDFToolStyle::Get().Get(), "QuickSDF.Timeline.ToggleButton")
								.ToolTipText(LOCTEXT("SnapTooltip", "Snap dragged timeline keys to 5 degree steps"))
								.IsChecked(this, &SQuickSDFTimeline::IsGridSnapEnabled)
								.OnCheckStateChanged(this, &SQuickSDFTimeline::OnGridSnapStateChanged)
								.Padding(FMargin(3.0f, 2.0f))
								[
									SNew(SImage)
									.Image(FQuickSDFToolStyle::GetBrush("QuickSDF.Action.Snap"))
									.ColorAndOpacity(TAttribute<FSlateColor>::CreateLambda([this]()
									{
										return bGridSnapEnabled
											? FSlateColor(GetQuickSDFTimelineAccentColor(1.0f))
											: FSlateColor(FLinearColor(0.62f, 0.62f, 0.62f, 1.0f));
									}))
								]
							]
						]

						+ SHorizontalBox::Slot()
						.AutoWidth()
						.VAlign(VAlign_Center)
						.Padding(5.0f, 0.0f, 7.0f, 0.0f)
						[
							MakeTimelineSeparator()
						]

						+ SHorizontalBox::Slot()
						.AutoWidth()
						.Padding(1.0f, 0.0f)
						[
							MakeTimelineIconButton(
								"QuickSDF.Action.ImportMasks",
								LOCTEXT("ImportMasksTooltip", "Open the mask import panel. Choose Texture2D assets in the panel or drag them onto it."),
								FOnClicked::CreateSP(this, &SQuickSDFTimeline::OnImportClicked))
						]

						+ SHorizontalBox::Slot()
						.AutoWidth()
						.Padding(1.0f, 0.0f)
						[
							MakeTimelineIconLabelButton(
								"QuickSDF.Action.CompleteToEight",
								TAttribute<FText>::Create(TAttribute<FText>::FGetter::CreateSP(this, &SQuickSDFTimeline::GetCompleteMaskButtonText)),
								TAttribute<FText>::Create(TAttribute<FText>::FGetter::CreateSP(this, &SQuickSDFTimeline::GetCompleteMaskTooltipText)),
								FOnClicked::CreateSP(this, &SQuickSDFTimeline::OnCompleteToEightClicked),
								46.0f)
						]

						+ SHorizontalBox::Slot()
						.AutoWidth()
						.Padding(1.0f, 0.0f)
						[
							MakeTimelineIconLabelButton(
								"QuickSDF.Action.Redistribute",
								TAttribute<FText>(LOCTEXT("RedistributeBtn", "Even")),
								TAttribute<FText>(LOCTEXT("RedistributeTooltip", "Redistribute timeline angles evenly")),
								FOnClicked::CreateSP(this, &SQuickSDFTimeline::OnRedistributeEvenlyClicked),
								62.0f)
						]

						+ SHorizontalBox::Slot()
						.AutoWidth()
						.Padding(1.0f, 0.0f)
						[
							MakeTimelineIconButton(
								"QuickSDF.Action.AddKey",
								LOCTEXT("AddFrameTooltip", "Add a timeline keyframe at the current seek angle"),
								FOnClicked::CreateSP(this, &SQuickSDFTimeline::OnAddKeyframeClicked))
						]
						+ SHorizontalBox::Slot()
						.AutoWidth()
						.Padding(1.0f, 0.0f)
						[
							MakeTimelineIconButton(
								"QuickSDF.Action.DuplicateKey",
								LOCTEXT("DuplicateFrameTooltip", "Duplicate the selected keyframe at the current seek angle"),
								FOnClicked::CreateSP(this, &SQuickSDFTimeline::OnDuplicateKeyframeClicked))
						]
						+ SHorizontalBox::Slot()
						.AutoWidth()
						.Padding(1.0f, 0.0f)
						[
							MakeTimelineIconButton(
								"QuickSDF.Action.DeleteKey",
								LOCTEXT("DelFrameTooltip", "Delete the selected timeline keyframe"),
								FOnClicked::CreateSP(this, &SQuickSDFTimeline::OnDeleteKeyframeClicked))
						]
						+ SHorizontalBox::Slot()
						.AutoWidth()
						.Padding(1.0f, 0.0f)
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
							.HeightOverride(QuickSDFTimelineTrackHeight)
							[
								SNew(SOverlay)
								
								// Track base.
								+ SOverlay::Slot()
								.VAlign(VAlign_Fill)
								[
									SNew(SBorder)
									.BorderImage(FAppStyle::GetBrush("WhiteBrush"))
									.BorderBackgroundColor(FLinearColor(0.012f, 0.012f, 0.012f, 0.96f))
								]

								// The actual canvas for keyframes
								+ SOverlay::Slot()
								[
									SAssignNew(TimelineTrackCanvas, SCanvas)
								]
							]
						]
						+ SVerticalBox::Slot()
						.AutoHeight()
						.Padding(0.0f, 6.0f, 0.0f, 0.0f)
						[
							SAssignNew(ImportPanelBox, SBox)
							.Visibility_Lambda([this]()
							{
								return bImportPanelOpen ? EVisibility::Visible : EVisibility::Collapsed;
							})
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

	if (Tool->ConsumeImportPanelRequest())
	{
		OpenImportPanel(TArray<FQuickSDFMaskImportSource>());
	}

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
	if (AssetDragDropOp.IsValid() && AssetDragDropOp->HasAssets())
	{
		for (const FAssetData& AssetData : AssetDragDropOp->GetAssets())
		{
			if (AssetData.GetClass() && AssetData.GetClass()->IsChildOf(UTexture2D::StaticClass()))
			{
				return FReply::Handled();
			}
		}
	}

	return FReply::Unhandled();
}

FReply SQuickSDFTimeline::OnDrop(const FGeometry& MyGeometry, const FDragDropEvent& DragDropEvent)
{
	TSharedPtr<FAssetDragDropOp> AssetDragDropOp = DragDropEvent.GetOperationAs<FAssetDragDropOp>();
	if (AssetDragDropOp.IsValid() && AssetDragDropOp->HasAssets())
	{
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

		if (Textures.Num() == 1)
		{
			const int32 TargetIndex = FindKeyframeAtScreenPosition(DragDropEvent.GetScreenSpacePosition());
			if (TargetIndex != INDEX_NONE)
			{
				if (UQuickSDFPaintTool* Tool = GetActivePaintTool())
				{
					Tool->AssignMaskTextureToAngle(TargetIndex, Textures[0]);
					return FReply::Handled();
				}
			}
		}

		OpenImportPanel(MakeImportSourcesFromTextures(Textures));
		return FReply::Handled();
	}

	return FReply::Unhandled();
}

FReply SQuickSDFTimeline::OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (MouseEvent.GetEffectingButton() == EKeys::LeftMouseButton &&
		IsSeekLaneUnderCursor(MouseEvent.GetScreenSpacePosition()))
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

FText SQuickSDFTimeline::GetHeaderStatusText() const
{
	const UQuickSDFPaintTool* Tool = GetActivePaintTool();
	const UQuickSDFToolProperties* Props = Tool ? Tool->Properties : nullptr;
	const int32 MaskCount = Props ? Props->NumAngles : 0;
	const int32 CurrentIndex = Props ? FMath::Clamp(Props->EditAngleIndex, 0, FMath::Max(Props->NumAngles - 1, 0)) : 0;
	const float CurrentAngle = Props && Props->TargetAngles.IsValidIndex(CurrentIndex) ? Props->TargetAngles[CurrentIndex] : 0.0f;
	return FText::Format(
		LOCTEXT("TimelineHeaderStatus", "{0} masks / {1} deg"),
		FText::AsNumber(MaskCount),
		FText::AsNumber(FMath::RoundToInt(CurrentAngle)));
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

FText SQuickSDFTimeline::GetCompleteMaskButtonText() const
{
	const UQuickSDFPaintTool* Tool = GetActivePaintTool();
	const UQuickSDFToolProperties* Props = Tool ? Tool->Properties : nullptr;
	const bool bSymmetryMode = !Props || Props->bSymmetryMode;
	const int32 TargetCount = QuickSDFPaintToolPrivate::GetQuickSDFDefaultAngleCount(bSymmetryMode);
	return FText::AsNumber(TargetCount);
}

FText SQuickSDFTimeline::GetCompleteMaskTooltipText() const
{
	const UQuickSDFPaintTool* Tool = GetActivePaintTool();
	const UQuickSDFToolProperties* Props = Tool ? Tool->Properties : nullptr;
	const bool bSymmetryMode = !Props || Props->bSymmetryMode;
	const int32 TargetCount = QuickSDFPaintToolPrivate::GetQuickSDFDefaultAngleCount(bSymmetryMode);
	const int32 MaxAngle = bSymmetryMode ? 90 : 180;
	return FText::Format(
		LOCTEXT("CompleteDefaultMasksTooltip", "Complete the mask set to {0} angles across 0-{1} degrees"),
		FText::AsNumber(TargetCount),
		FText::AsNumber(MaxAngle));
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
	UMeshComponent* MeshComp = Tool->GetCurrentComponent();

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

float SQuickSDFTimeline::GetCurrentSeekAngle() const
{
	UQuickSDFPaintTool* Tool = GetActivePaintTool();
	const UQuickSDFToolProperties* Props = Tool ? Tool->Properties : nullptr;
	const float MaxAngle = Props && Props->bSymmetryMode ? 90.0f : 180.0f;

	if (bHasSeekAngle)
	{
		return FMath::Clamp(LastSeekAngle, 0.0f, MaxAngle);
	}

	const float LightYaw = GetCurrentLightYaw();
	if (LightYaw >= 0.0f)
	{
		return FMath::Clamp(LightYaw, 0.0f, MaxAngle);
	}

	if (Props && Props->TargetAngles.IsValidIndex(Props->EditAngleIndex))
	{
		return FMath::Clamp(Props->TargetAngles[Props->EditAngleIndex], 0.0f, MaxAngle);
	}

	return 0.0f;
}

void SQuickSDFTimeline::SetSeekAngle(float Angle)
{
	UQuickSDFPaintTool* Tool = GetActivePaintTool();
	const UQuickSDFToolProperties* Props = Tool ? Tool->Properties : nullptr;
	const float MaxAngle = Props && Props->bSymmetryMode ? 90.0f : 180.0f;
	LastSeekAngle = FMath::Clamp(Angle, 0.0f, MaxAngle);
	bHasSeekAngle = true;
	Invalidate(EInvalidateWidgetReason::PaintAndVolatility);
}

float SQuickSDFTimeline::ResolveTimelineActionAngle() const
{
	return GetCurrentSeekAngle();
}

bool SQuickSDFTimeline::IsSeekLaneUnderCursor(const FVector2D& ScreenPosition) const
{
	if (!TimelineTrackCanvas.IsValid())
	{
		return false;
	}

	const FGeometry TrackGeometry = TimelineTrackCanvas->GetTickSpaceGeometry();
	const FVector2D LocalPosition = TrackGeometry.AbsoluteToLocal(ScreenPosition);
	const FVector2D LocalSize = TrackGeometry.GetLocalSize();
	return LocalPosition.X >= 0.0 && LocalPosition.Y >= 0.0 &&
		LocalPosition.X <= LocalSize.X && LocalPosition.Y <= QuickSDFTimelineSeekLaneHeight;
}

int32 SQuickSDFTimeline::FindKeyframeAtScreenPosition(const FVector2D& ScreenPosition) const
{
	if (!TimelineTrackCanvas.IsValid())
	{
		return INDEX_NONE;
	}

	const UQuickSDFPaintTool* Tool = GetActivePaintTool();
	const UQuickSDFToolProperties* Props = Tool ? Tool->Properties : nullptr;
	if (!Props)
	{
		return INDEX_NONE;
	}

	const FGeometry TrackGeometry = TimelineTrackCanvas->GetTickSpaceGeometry();
	const FVector2D LocalPosition = TrackGeometry.AbsoluteToLocal(ScreenPosition);
	const FVector2D LocalSize = TrackGeometry.GetLocalSize();
	if (LocalPosition.Y < QuickSDFTimelineSeekLaneHeight || LocalPosition.Y > LocalSize.Y)
	{
		return INDEX_NONE;
	}

	const float TrackWidth = FMath::Max(LocalSize.X - 40.0f, 1.0f);
	const float MaxAngle = Props->bSymmetryMode ? 90.0f : 180.0f;
	int32 BestIndex = INDEX_NONE;
	float BestDistance = TNumericLimits<float>::Max();

	for (int32 Index = 0; Index < Props->TargetAngles.Num(); ++Index)
	{
		const float Angle = Props->TargetAngles[Index];
		if (Props->bSymmetryMode && Angle > MaxAngle)
		{
			continue;
		}

		const float KeyX = FMath::Clamp(Angle / MaxAngle, 0.0f, 1.0f) * TrackWidth + 20.0f;
		const float Distance = FMath::Abs(LocalPosition.X - KeyX);
		if (Distance <= QuickSDFTimelineKeyframeHitSlop && Distance < BestDistance)
		{
			BestDistance = Distance;
			BestIndex = Index;
		}
	}

	return BestIndex;
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
	LastSeekAngle = SeekAngle;
	bHasSeekAngle = true;

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

void SQuickSDFTimeline::OpenImportPanel(const TArray<FQuickSDFMaskImportSource>& Sources)
{
	if (!ImportPanelBox.IsValid())
	{
		return;
	}

	UQuickSDFPaintTool* Tool = GetActivePaintTool();
	bImportPanelOpen = true;
	ImportPanelBox->SetContent(
		SNew(SQuickSDFMaskImportPanel)
		.PaintTool(TWeakObjectPtr<UQuickSDFPaintTool>(Tool))
		.Sources(Sources)
		.OnClosed(FSimpleDelegate::CreateSP(this, &SQuickSDFTimeline::CloseImportPanel)));
}

void SQuickSDFTimeline::CloseImportPanel()
{
	bImportPanelOpen = false;
	if (ImportPanelBox.IsValid())
	{
		ImportPanelBox->SetContent(SNullWidget::NullWidget);
	}
}

TArray<FQuickSDFMaskImportSource> SQuickSDFTimeline::MakeImportSourcesFromTextures(const TArray<UTexture2D*>& Textures) const
{
	TArray<FQuickSDFMaskImportSource> Sources;
	Sources.Reserve(Textures.Num());
	for (UTexture2D* Texture : Textures)
	{
		if (!Texture)
		{
			continue;
		}

		FQuickSDFMaskImportSource Source;
		Source.Texture = Texture;
		Source.DisplayName = Texture->GetName();
		Source.Width = Texture->GetSizeX();
		Source.Height = Texture->GetSizeY();
		Sources.Add(Source);
	}
	return Sources;
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
		for (int32 i = 0; i < Asset->GetActiveAngleDataList().Num(); ++i)
		{
			UTexture2D* ThumbnailTexture = CreateTimelineThumbnailTexture(Tool, Asset->GetActiveAngleDataList()[i].PaintRenderTarget);
			if (ThumbnailTexture)
			{
				ThumbnailTextures.Add(TStrongObjectPtr<UTexture2D>(ThumbnailTexture));
			}
			
			UTexture* Tex = ThumbnailTexture ? ThumbnailTexture : Asset->GetActiveAngleDataList()[i].TextureMask;
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

	const float KeyframeLaneTop = QuickSDFTimelineSeekLaneHeight;
	const float KeyframeLaneHeight = QuickSDFTimelineKeyframeLaneHeight;
	const float KeyframeWidth = QuickSDFTimelineKeyframeWidth;

	TimelineTrackCanvas->AddSlot()
	.Position(FVector2D::ZeroVector)
	.Size(TAttribute<FVector2D>::CreateLambda([this]() {
		const float TrackWidth = TimelineTrackCanvas->GetTickSpaceGeometry().GetLocalSize().X;
		return FVector2D(FMath::Max(0.0f, TrackWidth), QuickSDFTimelineSeekLaneHeight);
	}))
	[
		SNew(SBorder)
		.Visibility(EVisibility::HitTestInvisible)
		.BorderImage(FAppStyle::GetBrush("WhiteBrush"))
		.BorderBackgroundColor(FLinearColor(0.025f, 0.025f, 0.025f, 1.0f))
	];

	TimelineTrackCanvas->AddSlot()
	.Position(FVector2D(0.0f, KeyframeLaneTop))
	.Size(TAttribute<FVector2D>::CreateLambda([this]() {
		const float TrackWidth = TimelineTrackCanvas->GetTickSpaceGeometry().GetLocalSize().X;
		return FVector2D(FMath::Max(0.0f, TrackWidth), QuickSDFTimelineKeyframeLaneHeight);
	}))
	[
		SNew(SBorder)
		.Visibility(EVisibility::HitTestInvisible)
		.BorderImage(FAppStyle::GetBrush("WhiteBrush"))
		.BorderBackgroundColor(FLinearColor(0.012f, 0.012f, 0.012f, 0.96f))
	];

	TimelineTrackCanvas->AddSlot()
	.Position(FVector2D(0.0f, KeyframeLaneTop))
	.Size(TAttribute<FVector2D>::CreateLambda([this]() {
		const float TrackWidth = TimelineTrackCanvas->GetTickSpaceGeometry().GetLocalSize().X;
		return FVector2D(FMath::Max(0.0f, TrackWidth), 1.0f);
	}))
	[
		SNew(SBorder)
		.Visibility(EVisibility::HitTestInvisible)
		.BorderImage(FAppStyle::GetBrush("WhiteBrush"))
		.BorderBackgroundColor(FLinearColor(1.0f, 1.0f, 1.0f, 0.12f))
	];

	// 1. Add the thumbnail strip segments.
	for (int32 i = 0; i < Props->NumAngles; ++i)
	{
		// 'i' here is the visual order (0th smallest, 1st smallest...)
		TimelineTrackCanvas->AddSlot()
		.Position(TAttribute<FVector2D>::CreateLambda([this, i, KeyframeLaneTop]() {
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
			
			return FVector2D(FMath::Max(0.0f, TrackWidth) * (L / MaxAngle) + 20.0f, KeyframeLaneTop);
		}))
		.Size(TAttribute<FVector2D>::CreateLambda([this, i, KeyframeLaneHeight]() {
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
			
			return FVector2D(FMath::Max(0.0f, TrackWidth) * ((R - L) / MaxAngle), KeyframeLaneHeight);
		}))
		[
			SNew(SBorder)
			.Visibility(EVisibility::HitTestInvisible)
			.BorderImage(FAppStyle::GetBrush("WhiteBrush"))
			.BorderBackgroundColor(FLinearColor(0.025f, 0.025f, 0.025f, 1.0f))
			.Padding(FMargin(1.0f, 2.0f))
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
					.ColorAndOpacity(TAttribute<FSlateColor>::CreateLambda([this, i]() {
						UQuickSDFPaintTool* Tool = this->GetActivePaintTool();
						if (!Tool || !Tool->Properties) return FSlateColor(FLinearColor(0.7f, 0.7f, 0.7f, 0.65f));
						UQuickSDFToolProperties* P = Tool->Properties;

						bool bSymmetry = P->bSymmetryMode;
						float MaxAngle = bSymmetry ? 90.0f : 180.0f;

						TArray<int32> Indices;
						for (int32 k = 0; k < P->TargetAngles.Num(); ++k)
						{
							if (!bSymmetry || P->TargetAngles[k] <= MaxAngle) Indices.Add(k);
						}
						Indices.Sort([P](int32 A, int32 B) { return P->TargetAngles[A] < P->TargetAngles[B]; });

						if (Indices.IsValidIndex(i) && P->EditAngleIndex == Indices[i])
						{
							return FSlateColor(FLinearColor(1.0f, 1.0f, 1.0f, 0.95f));
						}
						return FSlateColor(FLinearColor(0.72f, 0.72f, 0.72f, 0.72f));
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
		.Size(FVector2D(1.0f, QuickSDFTimelineSeekLaneHeight))
		[
			SNew(SBorder)
			.Visibility(EVisibility::HitTestInvisible)
			.BorderImage(FAppStyle::GetBrush("WhiteBrush"))
			.BorderBackgroundColor(FLinearColor(0.8f, 0.8f, 0.8f, 0.18f))
		];
	}

	// 3. Add Keyframes in two passes to handle "Z-Order" (Active on top)
	auto AddKeyframeToCanvas = [this, Props, KeyframeLaneTop, KeyframeLaneHeight, KeyframeWidth](int32 i)
	{
		TimelineTrackCanvas->AddSlot()
		.Position(TAttribute<FVector2D>::CreateLambda([this, i, KeyframeLaneTop, KeyframeWidth]() {
			UQuickSDFPaintTool* ActiveTool = this->GetActivePaintTool();
			if (ActiveTool && ActiveTool->Properties && ActiveTool->Properties->TargetAngles.IsValidIndex(i))
			{
				float CurrentAngle = ActiveTool->Properties->TargetAngles[i];
				bool bSymmetry = ActiveTool->Properties->bSymmetryMode;
				float MaxAngle = bSymmetry ? 90.0f : 180.0f;

				if (bSymmetry && CurrentAngle > MaxAngle) return FVector2D(-1000.0f, -1000.0f); // Hide

				float Percent = FMath::Clamp(CurrentAngle / MaxAngle, 0.0f, 1.0f);
				float TrackWidth = TimelineTrackCanvas->GetTickSpaceGeometry().GetLocalSize().X - 40.0f;
				return FVector2D(FMath::Max(0.0f, TrackWidth) * Percent + 20.0f - (KeyframeWidth * 0.5f), KeyframeLaneTop);
			}
			return FVector2D::ZeroVector;
		}))
		.Size(FVector2D(KeyframeWidth, KeyframeLaneHeight))
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
			.bAllowSourceTextureOverwrite(TAttribute<bool>::CreateLambda([this, i]() {
				if (const UQuickSDFPaintTool* ActiveTool = this->GetActivePaintTool())
				{
					if (const UQuickSDFToolSubsystem* Subsystem = GEditor ? GEditor->GetEditorSubsystem<UQuickSDFToolSubsystem>() : nullptr)
					{
						const UQuickSDFAsset* Asset = Subsystem->GetActiveSDFAsset();
						return Asset && Asset->GetActiveAngleDataList().IsValidIndex(i) && Asset->GetActiveAngleDataList()[i].bAllowSourceTextureOverwrite;
					}
				}
				return false;
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
		
		float SeekAngle = GetCurrentSeekAngle();
		if (SeekAngle < 0.0f) return FVector2D(-1000.0f, -1000.0f);

		float Percent = FMath::Clamp(SeekAngle / MaxAngle, 0.0f, 1.0f);
		float TrackWidth = TimelineTrackCanvas->GetTickSpaceGeometry().GetLocalSize().X - 40.0f;
		return FVector2D(FMath::Max(0.0f, TrackWidth) * Percent + 19.0f, 2.0f);
	}))
	.Size(FVector2D(2.0f, QuickSDFTimelineSeekLaneHeight - 4.0f))
	[
		SNew(SBorder)
		.Visibility(EVisibility::HitTestInvisible)
		.BorderImage(FAppStyle::GetBrush("WhiteBrush"))
		.BorderBackgroundColor(FLinearColor(1.0f, 0.85f, 0.2f, 0.85f))
	];
}

FReply SQuickSDFTimeline::OnAddKeyframeClicked()
{
	UQuickSDFPaintTool* Tool = GetActivePaintTool();
	if (Tool)
	{
		Tool->AddKeyframeAtAngle(ResolveTimelineActionAngle());
	}
	return FReply::Handled();
}

FReply SQuickSDFTimeline::OnDuplicateKeyframeClicked()
{
	UQuickSDFPaintTool* Tool = GetActivePaintTool();
	if (Tool)
	{
		Tool->DuplicateKeyframeAtAngle(ResolveTimelineActionAngle());
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

FReply SQuickSDFTimeline::OnImportClicked()
{
	OpenImportPanel(TArray<FQuickSDFMaskImportSource>());
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
		const float TargetAngle = Tool->Properties->TargetAngles[Index];
		LastSeekAngle = TargetAngle;
		bHasSeekAngle = true;
		Mode->SetPreviewLightAngle(TargetAngle);
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
					if (Subsystem->GetActiveSDFAsset()->GetActiveAngleDataList().IsValidIndex(Index))
					{
						Subsystem->GetActiveSDFAsset()->GetActiveAngleDataList()[Index].Angle = NewAngle;
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
