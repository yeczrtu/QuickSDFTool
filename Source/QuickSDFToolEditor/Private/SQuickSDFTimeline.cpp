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
#include "Engine/TextureRenderTarget2D.h"
#include "EngineUtils.h"
#include "Engine/DirectionalLight.h"
#include "QuickSDFAsset.h"
#include "Brushes/SlateImageBrush.h"

#define LOCTEXT_NAMESPACE "SQuickSDFTimeline"

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
		.Padding(0, 5, 0, 0)
		[
			SNew(SBorder)
			.BorderImage(FAppStyle::GetBrush("WhiteBrush"))
			.BorderBackgroundColor(TAttribute<FSlateColor>::CreateLambda([this]() {
				return bIsActive.Get() ? FLinearColor(1.0f, 0.6f, 0.1f, 1.0f) : FLinearColor(0.1f, 0.1f, 0.1f, 1.0f);
			}))
			.Padding(1.0f)
			[
				SNew(SBox)
				.WidthOverride(24.0f)
				.HeightOverride(24.0f)
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
		.Padding(0, 0, 0, -16)
		[
			SNew(STextBlock)
			.Text(TAttribute<FText>::CreateLambda([this]() {
				return FText::FromString(FString::Printf(TEXT("%.0f\u00B0"), Angle.Get()));
			}))
			.ColorAndOpacity(ColorAttr)
			.Font(FCoreStyle::GetDefaultFontStyle("Bold", 8))
			.ShadowOffset(FVector2D(1, 1))
			.ShadowColorAndOpacity(FLinearColor::Black)
		]
	];
}

FReply SQuickSDFTimelineKeyframe::OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (MouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
	{
		bIsDragging = true;
		OnClicked.ExecuteIfBound();
		OnDragStarted.ExecuteIfBound();
		return FReply::Handled().CaptureMouse(SharedThis(this));
	}
	return FReply::Unhandled();
}

FReply SQuickSDFTimelineKeyframe::OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (MouseEvent.GetEffectingButton() == EKeys::LeftMouseButton && bIsDragging)
	{
		bIsDragging = false;
		OnDragEnded.ExecuteIfBound();
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
		.Padding(10.0f)
		[
			SNew(SBox)
			.WidthOverride(800.0f) // Wider for spatial timeline
			[
				SNew(SBorder)
				.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
				.Padding(4.0f)
				[
					SNew(SVerticalBox)

					// Top Row: Controls
					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(0.0f, 2.0f)
					[
						SNew(SHorizontalBox)

						// Snap Checkbox
						+ SHorizontalBox::Slot()
						.AutoWidth()
						.VAlign(VAlign_Center)
						.Padding(10.0f, 0.0f, 0.0f, 0.0f)
						[
							SNew(SCheckBox)
							.IsChecked(this, &SQuickSDFTimeline::IsGridSnapEnabled)
							.OnCheckStateChanged(this, &SQuickSDFTimeline::OnGridSnapStateChanged)
							[
								SNew(STextBlock)
								.Text(LOCTEXT("SnapText", "Snap"))
							]
						]

						// Symmetry Checkbox
						+ SHorizontalBox::Slot()
						.AutoWidth()
						.VAlign(VAlign_Center)
						.Padding(10.0f, 0.0f, 0.0f, 0.0f)
						[
							SNew(SCheckBox)
							.IsChecked(this, &SQuickSDFTimeline::IsSymmetryModeEnabled)
							.OnCheckStateChanged(this, &SQuickSDFTimeline::OnSymmetryModeStateChanged)
							[
								SNew(STextBlock)
								.Text(LOCTEXT("SymmetryText", "Symmetry"))
							]
						]

						+ SHorizontalBox::Slot().FillWidth(1.0f) [ SNew(SSpacer) ]

						// Controls (Add/Delete)
						+ SHorizontalBox::Slot()
						.AutoWidth()
						.VAlign(VAlign_Center)
						.Padding(0.0f, 0.0f, 10.0f, 0.0f)
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

					// Bottom Row: Timeline Track
					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(0.0f, 5.0f)
					[
						SNew(SBox)
						.HeightOverride(44.0f)
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
	if (Tool && Tool->GetToolManager() && Tool->GetToolManager()->GetContextQueriesAPI())
	{
		if (UWorld* World = Tool->GetToolManager()->GetContextQueriesAPI()->GetCurrentEditingWorld())
		{
			for (TActorIterator<ADirectionalLight> It(World); It; ++It)
			{
				if (ADirectionalLight* DirLight = *It)
				{
					// Normalize yaw to 0-360 then map to 0-180 if needed, or just take positive
					float Yaw = DirLight->GetActorRotation().Yaw;
					while (Yaw < 0.0f) Yaw += 360.0f;
					while (Yaw >= 360.0f) Yaw -= 360.0f;
					
					// If symmetry mode is on, we only care about 0-90
					bool bSymmetry = Tool->Properties && Tool->Properties->bSymmetryMode;
					if (bSymmetry)
					{
						// Map 0-360 to 0-90 in a symmetric way
						// 0-90 -> 0-90
						// 90-180 -> 90-0
						// 180-270 -> 0-90
						// 270-360 -> 90-0
						if (Yaw > 180.0f) Yaw = 360.0f - Yaw;
						if (Yaw > 90.0f) Yaw = 180.0f - Yaw;
						return FMath::Clamp(Yaw, 0.0f, 90.0f);
					}
					
					return FMath::Clamp(Yaw, 0.0f, 180.0f);
				}
			}
		}
	}
	return 0.0f;
}

void SQuickSDFTimeline::RebuildTimeline()
{
	if (!TimelineTrackCanvas.IsValid()) return;
	
	TimelineTrackCanvas->ClearChildren();
	KeyframeBrushes.Empty();

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
			UTexture* Tex = Asset->AngleDataList[i].PaintRenderTarget;
			if (!Tex) Tex = Asset->AngleDataList[i].TextureMask;
			
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

	float CanvasHeight = 44.0f;
	float KeyframeWidth = 30.0f;

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
			}
		}
	}
}

void SQuickSDFTimeline::OnKeyframeDragStarted()
{
	UQuickSDFPaintTool* Tool = GetActivePaintTool();
	if (Tool && Tool->GetToolManager())
	{
		Tool->GetToolManager()->BeginUndoTransaction(LOCTEXT("TimelineDrag", "Drag Timeline Keyframe"));
		
		UQuickSDFToolSubsystem* Subsystem = GEditor->GetEditorSubsystem<UQuickSDFToolSubsystem>();
		if (Subsystem && Subsystem->GetActiveSDFAsset())
		{
			Subsystem->GetActiveSDFAsset()->Modify();
		}
	}
}

void SQuickSDFTimeline::OnKeyframeDragEnded()
{
	UQuickSDFPaintTool* Tool = GetActivePaintTool();
	if (Tool && Tool->GetToolManager())
	{
		Tool->GetToolManager()->EndUndoTransaction();
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
