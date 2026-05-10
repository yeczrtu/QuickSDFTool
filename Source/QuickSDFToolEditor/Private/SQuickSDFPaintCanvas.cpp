#include "SQuickSDFPaintCanvas.h"

#include "CanvasItem.h"
#include "CanvasTypes.h"
#include "Editor.h"
#include "Engine/Engine.h"
#include "Engine/Texture2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Docking/TabManager.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "GenericPlatform/ICursor.h"
#include "HAL/PlatformTime.h"
#include "InputCoreTypes.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "QuickSDFAsset.h"
#include "QuickSDFPaintTool.h"
#include "QuickSDFToolProperties.h"
#include "QuickSDFToolStyle.h"
#include "QuickSDFToolSubsystem.h"
#include "QuickSDFToolUI.h"
#include "Slate/SceneViewport.h"
#include "Styling/AppStyle.h"
#include "UObject/GCObject.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Input/SSpinBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBar.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SSeparator.h"
#include "Widgets/Layout/SUniformGridPanel.h"
#include "Widgets/SOverlay.h"
#include "Widgets/SViewport.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "SQuickSDFPaintCanvas"

namespace
{
const FName QuickSDFPaintCanvasTabId(TEXT("QuickSDF_2DCanvas"));
constexpr double QuickSDFCanvasExternalPenFreshSeconds = 0.75;
SQuickSDFPaintCanvas* GActivePaintCanvas = nullptr;

UQuickSDFAsset* GetActiveQuickSDFAsset()
{
	UQuickSDFToolSubsystem* Subsystem = GEditor ? GEditor->GetEditorSubsystem<UQuickSDFToolSubsystem>() : nullptr;
	return Subsystem ? Subsystem->GetActiveSDFAsset() : nullptr;
}

TSharedRef<SWidget> MakeCanvasButton(const FText& Label, const FText& ToolTip, FOnClicked OnClicked)
{
	return SNew(SButton)
		.ContentPadding(FMargin(7.0f, 2.0f))
		.ToolTipText(ToolTip)
		.OnClicked(OnClicked)
		[
			SNew(STextBlock)
			.Text(Label)
			.Font(FAppStyle::GetFontStyle("SmallFont"))
		];
}

void DrawCanvasLine(FCanvas* Canvas, const FVector2D& A, const FVector2D& B, const FLinearColor& Color, float Thickness = 1.0f)
{
	FCanvasLineItem Line(A, B);
	Line.SetColor(Color);
	Line.LineThickness = Thickness;
	Line.BlendMode = SE_BLEND_Translucent;
	Canvas->DrawItem(Line);
}

void DrawCanvasCircle(FCanvas* Canvas, const FVector2D& Center, double Radius, const FLinearColor& Color, float Thickness = 1.0f)
{
	if (Radius <= KINDA_SMALL_NUMBER)
	{
		return;
	}

	const int32 SegmentCount = 96;
	FVector2D Prev = Center + FVector2D(Radius, 0.0);
	for (int32 Segment = 1; Segment <= SegmentCount; ++Segment)
	{
		const double Angle = (static_cast<double>(Segment) / static_cast<double>(SegmentCount)) * 2.0 * PI;
		const FVector2D Curr = Center + FVector2D(FMath::Cos(Angle) * Radius, FMath::Sin(Angle) * Radius);
		DrawCanvasLine(Canvas, Prev, Curr, Color, Thickness);
		Prev = Curr;
	}
}
}

namespace QuickSDFPaintCanvas
{
const FName& GetTabId()
{
	return QuickSDFPaintCanvasTabId;
}

void OpenTab()
{
	FGlobalTabmanager::Get()->TryInvokeTab(QuickSDFPaintCanvasTabId);
}

bool UpdateExternalPenPointerState(const FVector2D& AbsoluteScreenPosition, bool bInContact)
{
	return GActivePaintCanvas
		? GActivePaintCanvas->UpdateExternalPenPointerState(AbsoluteScreenPosition, bInContact)
		: false;
}

bool RequestBrushResizeFromHoveredCanvas(const FVector2D& AbsoluteScreenPosition, bool bFromExternalPen)
{
	return GActivePaintCanvas
		? GActivePaintCanvas->RequestBrushResizeFromHoveredCanvas(AbsoluteScreenPosition, bFromExternalPen)
		: false;
}
}

class FQuickSDFPaintCanvasViewportClient final : public FViewportClient, public FGCObject
{
public:
	explicit FQuickSDFPaintCanvasViewportClient(SQuickSDFPaintCanvas* InOwner)
		: Owner(InOwner)
	{
		CreateCheckerTexture();
	}

	virtual UWorld* GetWorld() const override
	{
		return GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	}

	virtual void AddReferencedObjects(FReferenceCollector& Collector) override
	{
		Collector.AddReferencedObject(CheckerTexture);
	}

	virtual FString GetReferencerName() const override
	{
		return TEXT("FQuickSDFPaintCanvasViewportClient");
	}

	virtual bool InputKey(const FInputKeyEventArgs& EventArgs) override
	{
		FViewport* Viewport = EventArgs.Viewport;
		if (!Owner || !Viewport)
		{
			return false;
		}

		const FKey Key = EventArgs.Key;
		const EInputEvent Event = EventArgs.Event;
		FVector2D MousePosition(Viewport->GetMouseX(), Viewport->GetMouseY());
		FVector2D AbsolutePosition = FSlateApplication::Get().GetCursorPos();
		const bool bUsingFreshExternalPenPosition = TryUseFreshExternalPenPosition(MousePosition, &AbsolutePosition);
		const bool bControlDown = IsControlDown(Viewport);

		if ((Event == IE_Pressed || Event == IE_Repeat) && Key == EKeys::MouseScrollUp)
		{
			if (bControlDown)
			{
				AdjustBrushRadiusByWheel(1.0);
				Viewport->InvalidateDisplay();
				return true;
			}
			Owner->ZoomBy(1.15, MousePosition);
			Viewport->InvalidateDisplay();
			return true;
		}
		if ((Event == IE_Pressed || Event == IE_Repeat) && Key == EKeys::MouseScrollDown)
		{
			if (bControlDown)
			{
				AdjustBrushRadiusByWheel(-1.0);
				Viewport->InvalidateDisplay();
				return true;
			}
			Owner->ZoomBy(1.0 / 1.15, MousePosition);
			Viewport->InvalidateDisplay();
			return true;
		}
		if ((Event == IE_Pressed || Event == IE_Repeat) && Key == EKeys::RightBracket)
		{
			AdjustBrushRadiusByWheel(1.0);
			Viewport->InvalidateDisplay();
			return true;
		}
		if ((Event == IE_Pressed || Event == IE_Repeat) && Key == EKeys::LeftBracket)
		{
			AdjustBrushRadiusByWheel(-1.0);
			Viewport->InvalidateDisplay();
			return true;
		}
		if (bBrushResizing)
		{
			if (Event == IE_Pressed && (Key == EKeys::Escape || Key == EKeys::RightMouseButton))
			{
				CancelBrushResize();
				Viewport->InvalidateDisplay();
				return true;
			}
			if (Event == IE_Pressed && (Key == EKeys::LeftMouseButton || Key == EKeys::Enter))
			{
				EndBrushResize();
				Viewport->InvalidateDisplay();
				return true;
			}
			return true;
		}
		if ((Event == IE_Pressed || Event == IE_Repeat) && Key == EKeys::F)
		{
			if (bControlDown)
			{
				BeginBrushResize(MousePosition, AbsolutePosition, bUsingFreshExternalPenPosition);
			}
			else
			{
				Owner->FitCanvas();
			}
			Viewport->InvalidateDisplay();
			return true;
		}

		if (Key == EKeys::LeftMouseButton)
		{
			if (Event == IE_Pressed)
			{
				if (Viewport->KeyState(EKeys::SpaceBar))
				{
					BeginPan(MousePosition);
					return true;
				}
				return BeginPaintStroke(Viewport, MousePosition);
			}

			if (Event == IE_Released)
			{
				EndPaintStroke();
				EndPan();
				return true;
			}
		}

		if (Key == EKeys::MiddleMouseButton)
		{
			if (Event == IE_Pressed)
			{
				BeginPan(MousePosition);
				return true;
			}
			if (Event == IE_Released)
			{
				EndPan();
				return true;
			}
		}

		if (Key == EKeys::SpaceBar && Event == IE_Released)
		{
			EndPan();
		}

		return false;
	}

	virtual void MouseMove(FViewport* Viewport, int32 X, int32 Y) override
	{
		FVector2D MousePosition(X, Y);
		TryUseFreshExternalPenPosition(MousePosition);
		HandleMouseMove(Viewport, MousePosition);
	}

	virtual void CapturedMouseMove(FViewport* Viewport, int32 X, int32 Y) override
	{
		FVector2D MousePosition(X, Y);
		TryUseFreshExternalPenPosition(MousePosition);
		HandleMouseMove(Viewport, MousePosition);
	}

	virtual void Draw(FViewport* Viewport, FCanvas* Canvas) override
	{
		if (!Owner || !Viewport || !Canvas)
		{
			return;
		}

		Canvas->Clear(FLinearColor(0.015f, 0.016f, 0.018f, 1.0f));

		UQuickSDFPaintTool* Tool = Owner->GetPaintTool();
		UTextureRenderTarget2D* ActiveRT = Owner->GetActiveRenderTarget();
		if (!Tool || !ActiveRT || !ActiveRT->GetResource())
		{
			if (GEngine)
			{
				Canvas->DrawShadowedText(
					24.0,
					24.0,
					LOCTEXT("NoPaintTool", "Open the Quick SDF Paint tool to use the 2D Canvas."),
					GEngine->GetSmallFont(),
					FLinearColor(0.78f, 0.80f, 0.82f, 1.0f));
			}
			return;
		}

		if (Owner->GetViewState().bShowCheckerboard && CheckerTexture)
		{
			DrawTextureLayer(Canvas, CheckerTexture, FLinearColor::White, SE_BLEND_Opaque);
		}
		else
		{
			DrawSolidTextureBackground(Canvas);
		}

		DrawActiveMaskLayer(Canvas, Tool, ActiveRT);

		if (Tool->Properties && Tool->Properties->bEnableOnionSkin)
		{
			if (UTextureRenderTarget2D* PrevRT = Tool->GetCanvasOnionSkinRenderTarget(-1))
			{
				DrawTextureLayer(Canvas, PrevRT, FLinearColor(1.0f, 0.08f, 0.05f, 0.42f), SE_BLEND_Additive);
			}
			if (UTextureRenderTarget2D* NextRT = Tool->GetCanvasOnionSkinRenderTarget(1))
			{
				DrawTextureLayer(Canvas, NextRT, FLinearColor(0.0f, 0.9f, 0.25f, 0.38f), SE_BLEND_Additive);
			}
		}

		if (UTextureRenderTarget2D* UVOverlayRT = Tool->GetCanvasUVOverlayRenderTarget())
		{
			DrawTextureLayer(Canvas, UVOverlayRT, FLinearColor::White, SE_BLEND_Translucent);
		}

		DrawTextureBorder(Canvas);
		DrawPixelGrid(Canvas);
		DrawBrushCursor(Viewport, Canvas);
	}

	void EndActiveInput()
	{
		EndPaintStroke();
		EndPan();
		EndBrushResize();
		bPaintingFromExternalPen = false;
		if (UQuickSDFPaintTool* Tool = Owner ? Owner->GetPaintTool() : nullptr)
		{
			Tool->SetTextureCanvasCursorActive(false);
		}
	}

	bool HandleExternalPenPointer(const FVector2D& AbsoluteScreenPosition, bool bInContact)
	{
		if (!Owner)
		{
			return false;
		}

		const bool bRequireUnderViewport = !bPainting && !bPaintingFromExternalPen && !bBrushResizing;
		FVector2D ViewportPosition;
		bool bUnderViewport = false;
		const bool bHasViewportPosition = Owner->TryResolveAbsoluteViewportPosition(
			AbsoluteScreenPosition,
			ViewportPosition,
			bRequireUnderViewport,
			&bUnderViewport);
		if (!bHasViewportPosition)
		{
			return false;
		}

		LastExternalPenViewportPosition = ViewportPosition;
		LastExternalPenAbsolutePosition = AbsoluteScreenPosition;
		LastExternalPenUpdateTime = FPlatformTime::Seconds();
		bHasFreshExternalPenViewportPosition = true;
		bLastExternalPenInContact = bInContact;
		bLastExternalPenOverViewport = bUnderViewport;

		FViewport* Viewport = Owner->SceneViewport.IsValid() ? Owner->SceneViewport.Get() : nullptr;
		if (bBrushResizing)
		{
			HandleMouseMove(Viewport, ViewportPosition);
			if (Viewport)
			{
				Viewport->InvalidateDisplay();
			}
			return true;
		}

		if (bInContact)
		{
			if (bPainting)
			{
				HandleMouseMove(Viewport, ViewportPosition);
				bPaintingFromExternalPen = true;
			}
			else
			{
				bPaintingFromExternalPen = BeginPaintStroke(Viewport, ViewportPosition);
			}
		}
		else
		{
			const bool bHadActiveStroke = bPaintingFromExternalPen || bPainting;
			if (bHadActiveStroke)
			{
				const bool bWasQuickStrokeActive = IsTextureCanvasQuickStrokeActive();
				if (bWasQuickStrokeActive)
				{
					TickTextureCanvasQuickStrokePreview(Viewport, ViewportPosition);
				}
				EndPaintStroke();
				if (bWasQuickStrokeActive)
				{
					Owner->ClearHoverUV();
				}
			}
			bPaintingFromExternalPen = false;
			if (!bHadActiveStroke)
			{
				HandleMouseMove(Viewport, ViewportPosition);
			}
		}

		if (Viewport)
		{
			Viewport->InvalidateDisplay();
		}
		return true;
	}

	bool TickExternalPenPointer()
	{
		if (!Owner ||
			!bHasFreshExternalPenViewportPosition ||
			FPlatformTime::Seconds() - LastExternalPenUpdateTime > QuickSDFCanvasExternalPenFreshSeconds)
		{
			return false;
		}

		if (!bLastExternalPenInContact && !bPaintingFromExternalPen && !bBrushResizingFromExternalPen)
		{
			return false;
		}

		FVector2D ViewportPosition = LastExternalPenViewportPosition;
		const bool bRequireUnderViewport = !bLastExternalPenInContact && !bPaintingFromExternalPen && !bBrushResizingFromExternalPen;
		if (!Owner->TryResolveAbsoluteViewportPosition(LastExternalPenAbsolutePosition, ViewportPosition, bRequireUnderViewport))
		{
			return false;
		}

		LastExternalPenViewportPosition = ViewportPosition;
		FViewport* Viewport = Owner->SceneViewport.IsValid() ? Owner->SceneViewport.Get() : nullptr;
		if (bBrushResizing)
		{
			HandleMouseMove(Viewport, ViewportPosition);
			return true;
		}

		if (bLastExternalPenInContact)
		{
			if (bPainting || bPaintingFromExternalPen)
			{
				HandleMouseMove(Viewport, ViewportPosition);
				bPaintingFromExternalPen = bPainting;
				return true;
			}
		}

		return false;
	}

	bool RequestBrushResize(const FVector2D& AbsoluteScreenPosition, bool bFromExternalPen)
	{
		if (!Owner)
		{
			return false;
		}

		FVector2D ViewportPosition;
		bool bUnderViewport = false;
		if (!Owner->TryResolveAbsoluteViewportPosition(AbsoluteScreenPosition, ViewportPosition, true, &bUnderViewport) || !bUnderViewport)
		{
			return false;
		}

		if (bFromExternalPen)
		{
			LastExternalPenViewportPosition = ViewportPosition;
			LastExternalPenAbsolutePosition = AbsoluteScreenPosition;
			LastExternalPenUpdateTime = FPlatformTime::Seconds();
			bHasFreshExternalPenViewportPosition = true;
			bLastExternalPenInContact = false;
			bLastExternalPenOverViewport = true;
		}

		FViewport* Viewport = Owner->SceneViewport.IsValid() ? Owner->SceneViewport.Get() : nullptr;
		BeginBrushResize(ViewportPosition, AbsoluteScreenPosition, bFromExternalPen);
		if (Viewport)
		{
			Viewport->InvalidateDisplay();
		}
		return true;
	}

	bool HasFreshExternalPenOverViewport() const
	{
		if (!bHasFreshExternalPenViewportPosition ||
			FPlatformTime::Seconds() - LastExternalPenUpdateTime > QuickSDFCanvasExternalPenFreshSeconds)
		{
			return false;
		}

		FVector2D ViewportPosition;
		bool bUnderViewport = false;
		return Owner &&
			Owner->TryResolveAbsoluteViewportPosition(LastExternalPenAbsolutePosition, ViewportPosition, false, &bUnderViewport) &&
			bUnderViewport;
	}

	bool IsExternalPenPainting() const
	{
		return bPaintingFromExternalPen;
	}

	bool IsExternalPenBrushResizing() const
	{
		return bBrushResizingFromExternalPen;
	}

	bool IsTextureCanvasQuickStrokeActive() const
	{
		const UQuickSDFPaintTool* Tool = Owner ? Owner->GetPaintTool() : nullptr;
		return Tool && Tool->IsTextureCanvasQuickStrokeActive();
	}

	bool TickTextureCanvasQuickStrokePreview(FViewport* Viewport, const FVector2D& MousePosition)
	{
		if (!Owner)
		{
			return false;
		}

		FVector2f UV;
		if (!Owner->IsViewportPositionInsideTexture(MousePosition, &UV))
		{
			return false;
		}

		UQuickSDFPaintTool* Tool = Owner->GetPaintTool();
		if (!Tool)
		{
			return false;
		}

		FQuickSDFTextureCanvasStrokeModifiers Modifiers;
		Modifiers.bPaintShadow = IsShiftDown(Viewport);
		const bool bUpdated = Tool->TickTextureCanvasQuickStrokePreview(UV, MousePosition, Modifiers);
		if (bUpdated)
		{
			Owner->SetHoverUV(UV);
		}
		return bUpdated;
	}

private:
	bool IsShiftDown(FViewport* Viewport) const
	{
		const FModifierKeysState ModifierKeys = FSlateApplication::Get().GetModifierKeys();
		return ModifierKeys.IsShiftDown() ||
			(Viewport && (Viewport->KeyState(EKeys::LeftShift) || Viewport->KeyState(EKeys::RightShift)));
	}

	bool IsControlDown(FViewport* Viewport) const
	{
		const FModifierKeysState ModifierKeys = FSlateApplication::Get().GetModifierKeys();
		return ModifierKeys.IsControlDown() ||
			(Viewport && (Viewport->KeyState(EKeys::LeftControl) || Viewport->KeyState(EKeys::RightControl)));
	}

	bool BeginPaintStroke(FViewport* Viewport, const FVector2D& MousePosition)
	{
		FVector2f UV;
		if (!Owner->IsViewportPositionInsideTexture(MousePosition, &UV))
		{
			return false;
		}

		UQuickSDFPaintTool* Tool = Owner->GetPaintTool();
		if (!Tool)
		{
			return false;
		}

		FQuickSDFTextureCanvasStrokeModifiers Modifiers;
		Modifiers.bPaintShadow = IsShiftDown(Viewport);
		bPainting = Tool->BeginTextureCanvasStroke(UV, MousePosition, Modifiers);
		if (bPainting)
		{
			Owner->SetHoverUV(UV);
		}
		return bPainting;
	}

	void EndPaintStroke()
	{
		if (!bPainting)
		{
			return;
		}

		if (UQuickSDFPaintTool* Tool = Owner ? Owner->GetPaintTool() : nullptr)
		{
			Tool->EndTextureCanvasStroke();
			Tool->SetTextureCanvasCursorActive(false);
		}
		bPainting = false;
		bPaintingFromExternalPen = false;
	}

	void BeginPan(const FVector2D& MousePosition)
	{
		EndPaintStroke();
		bPanning = true;
		LastMousePosition = MousePosition;
		Owner->GetViewState().bFitToViewport = false;
	}

	void EndPan()
	{
		bPanning = false;
	}

	void BeginBrushResize(const FVector2D& MousePosition, const FVector2D& AbsolutePosition, bool bFromExternalPen = false)
	{
		EndPaintStroke();
		EndPan();
		bBrushResizing = true;
		BrushResizeStartMousePosition = MousePosition;
		BrushResizeAnchorAbsolutePosition = AbsolutePosition;
		BrushResizeAccumulatedDeltaX = 0.0;
		BrushResizeStartRadiusPixels = Owner ? Owner->GetBrushRadiusPixels() : 1.0;
		bBrushResizingFromExternalPen = bFromExternalPen;
		SetBrushResizeCursorHidden(true);
	}

	void UpdateBrushResize()
	{
		if (!Owner)
		{
			return;
		}

		const FVector2D CurrentAbsolutePosition = bBrushResizingFromExternalPen
			? LastExternalPenAbsolutePosition
			: FSlateApplication::Get().GetCursorPos();
		const double DeltaX = CurrentAbsolutePosition.X - BrushResizeAnchorAbsolutePosition.X;
		if (bBrushResizingFromExternalPen)
		{
			if (FPlatformTime::Seconds() - LastExternalPenUpdateTime > QuickSDFCanvasExternalPenFreshSeconds)
			{
				return;
			}
			const double Scale = FMath::Pow(2.0, DeltaX / 180.0);
			Owner->SetBrushRadiusPixels(BrushResizeStartRadiusPixels * Scale);
			return;
		}

		if ((CurrentAbsolutePosition - BrushResizeAnchorAbsolutePosition).SizeSquared() > KINDA_SMALL_NUMBER)
		{
			BrushResizeAccumulatedDeltaX += DeltaX;
			FSlateApplication::Get().SetCursorPos(BrushResizeAnchorAbsolutePosition);
		}

		const double Scale = FMath::Pow(2.0, BrushResizeAccumulatedDeltaX / 180.0);
		Owner->SetBrushRadiusPixels(BrushResizeStartRadiusPixels * Scale);
	}

	void EndBrushResize()
	{
		if (!bBrushResizing)
		{
			return;
		}
		if (!bBrushResizingFromExternalPen)
		{
			FSlateApplication::Get().SetCursorPos(BrushResizeAnchorAbsolutePosition);
		}
		bBrushResizing = false;
		bBrushResizingFromExternalPen = false;
		SetBrushResizeCursorHidden(false);
	}

	void CancelBrushResize()
	{
		if (!bBrushResizing)
		{
			return;
		}
		if (Owner)
		{
			Owner->SetBrushRadiusPixels(BrushResizeStartRadiusPixels);
		}
		if (!bBrushResizingFromExternalPen)
		{
			FSlateApplication::Get().SetCursorPos(BrushResizeAnchorAbsolutePosition);
		}
		bBrushResizing = false;
		bBrushResizingFromExternalPen = false;
		SetBrushResizeCursorHidden(false);
	}

	void AdjustBrushRadiusByWheel(double Direction)
	{
		if (!Owner)
		{
			return;
		}

		const double CurrentRadius = Owner->GetBrushRadiusPixels();
		const double Step = FMath::Max(1.0, CurrentRadius * 0.08);
		Owner->AdjustBrushRadiusPixels(Direction * Step);
	}

	void SetBrushResizeCursorHidden(bool bHidden)
	{
		if (bBrushResizeCursorHidden == bHidden)
		{
			return;
		}

		if (Owner && Owner->ViewportWidget.IsValid())
		{
			if (bHidden)
			{
				Owner->ViewportWidget->SetCursor(EMouseCursor::None);
			}
			else
			{
				Owner->ViewportWidget->SetCursor(EMouseCursor::Default);
			}
		}

		if (TSharedPtr<ICursor> PlatformCursor = FSlateApplication::Get().GetPlatformCursor())
		{
			PlatformCursor->Show(!bHidden);
		}

		bBrushResizeCursorHidden = bHidden;
	}

	void HandleMouseMove(FViewport* Viewport, const FVector2D& MousePosition)
	{
		if (!Owner)
		{
			return;
		}

		if (UQuickSDFPaintTool* Tool = Owner->GetPaintTool())
		{
			Tool->SetTextureCanvasCursorActive(true);
		}

		if (bPanning)
		{
			Owner->GetViewState().Pan += MousePosition - LastMousePosition;
			LastMousePosition = MousePosition;
			if (Viewport)
			{
				Viewport->InvalidateDisplay();
			}
			return;
		}

		if (bBrushResizing)
		{
			FVector2f ResizeUV;
			if (Owner->IsViewportPositionInsideTexture(BrushResizeStartMousePosition, &ResizeUV))
			{
				Owner->SetHoverUV(ResizeUV);
			}
			UpdateBrushResize();
			if (Viewport)
			{
				Viewport->InvalidateDisplay();
			}
			return;
		}

		FVector2f UV;
		if (Owner->IsViewportPositionInsideTexture(MousePosition, &UV))
		{
			Owner->SetHoverUV(UV);
			if (UQuickSDFPaintTool* Tool = Owner->GetPaintTool())
			{
				FQuickSDFTextureCanvasStrokeModifiers Modifiers;
				Modifiers.bPaintShadow = IsShiftDown(Viewport);
				if (bPainting)
				{
					Tool->UpdateTextureCanvasStroke(UV, MousePosition, Modifiers);
				}
				else
				{
					Tool->UpdateTextureCanvasHover(UV, MousePosition);
				}
			}
		}
		else if (!bPainting)
		{
			Owner->ClearHoverUV();
		}

		if (Viewport)
		{
			Viewport->InvalidateDisplay();
		}
	}

	bool TryUseFreshExternalPenPosition(FVector2D& InOutMousePosition, FVector2D* OutAbsolutePosition = nullptr) const
	{
		if (!bHasFreshExternalPenViewportPosition ||
			(!bLastExternalPenOverViewport && !bLastExternalPenInContact && !bPaintingFromExternalPen && !bBrushResizingFromExternalPen) ||
			FPlatformTime::Seconds() - LastExternalPenUpdateTime > QuickSDFCanvasExternalPenFreshSeconds)
		{
			return false;
		}

		FVector2D ViewportPosition = LastExternalPenViewportPosition;
		if (Owner)
		{
			const bool bRequireUnderViewport = !bLastExternalPenInContact && !bPaintingFromExternalPen && !bBrushResizingFromExternalPen;
			if (!Owner->TryResolveAbsoluteViewportPosition(LastExternalPenAbsolutePosition, ViewportPosition, bRequireUnderViewport))
			{
				return false;
			}
		}

		InOutMousePosition = ViewportPosition;
		if (OutAbsolutePosition)
		{
			*OutAbsolutePosition = LastExternalPenAbsolutePosition;
		}
		return true;
	}

	void CreateCheckerTexture()
	{
		constexpr int32 Size = 64;
		CheckerTexture = UTexture2D::CreateTransient(Size, Size, PF_B8G8R8A8);
		if (!CheckerTexture || !CheckerTexture->GetPlatformData() || CheckerTexture->GetPlatformData()->Mips.Num() == 0)
		{
			return;
		}

		CheckerTexture->MipGenSettings = TMGS_NoMipmaps;
		CheckerTexture->Filter = TF_Nearest;
		CheckerTexture->SRGB = false;

		TArray<FColor> Pixels;
		Pixels.SetNum(Size * Size);
		const FColor A(42, 45, 47, 255);
		const FColor B(62, 66, 69, 255);
		for (int32 Y = 0; Y < Size; ++Y)
		{
			for (int32 X = 0; X < Size; ++X)
			{
				const bool bEven = ((X / 8) + (Y / 8)) % 2 == 0;
				Pixels[Y * Size + X] = bEven ? A : B;
			}
		}

		FTexture2DMipMap& Mip = CheckerTexture->GetPlatformData()->Mips[0];
		void* Data = Mip.BulkData.Lock(LOCK_READ_WRITE);
		if (Data)
		{
			FMemory::Memcpy(Data, Pixels.GetData(), Pixels.Num() * sizeof(FColor));
		}
		Mip.BulkData.Unlock();
		CheckerTexture->UpdateResource();
	}

	void DrawTextureLayer(FCanvas* Canvas, UTexture* Texture, const FLinearColor& Tint, ESimpleElementBlendMode BlendMode) const
	{
		if (!Texture || !Texture->GetResource())
		{
			return;
		}

		const FIntPoint TextureSize = Owner->GetTextureSize();
		if (TextureSize.X <= 0 || TextureSize.Y <= 0)
		{
			return;
		}

		const FQuickSDFTextureCanvasViewState& ViewState = Owner->GetViewState();
		const double Zoom = Owner->GetEffectiveZoom();
		const FVector2D DisplaySize(TextureSize.X * Zoom, TextureSize.Y * Zoom);
		const FVector2D TopLeft = Owner->GetCanvasCenter() - DisplaySize * 0.5;
		const FVector2D UV0(ViewState.bFlipX ? 1.0 : 0.0, ViewState.bFlipY ? 1.0 : 0.0);
		const FVector2D UV1(ViewState.bFlipX ? 0.0 : 1.0, ViewState.bFlipY ? 0.0 : 1.0);

		FCanvasTileItem Tile(TopLeft, Texture->GetResource(), DisplaySize, UV0, UV1, Tint);
		Tile.PivotPoint = FVector2D(0.5, 0.5);
		Tile.Rotation = FRotator(0.0, ViewState.RotationDegrees, 0.0);
		Tile.BlendMode = BlendMode;
		Canvas->DrawItem(Tile);
	}

	void DrawActiveMaskLayer(FCanvas* Canvas, UQuickSDFPaintTool* Tool, UTextureRenderTarget2D* ActiveRT) const
	{
		if (!Tool || !ActiveRT || !ActiveRT->GetResource())
		{
			return;
		}

		const FIntPoint TextureSize = Owner->GetTextureSize();
		const FQuickSDFTextureCanvasViewState& ViewState = Owner->GetViewState();
		const double Zoom = Owner->GetEffectiveZoom();
		const FVector2D DisplaySize(TextureSize.X * Zoom, TextureSize.Y * Zoom);
		const FVector2D TopLeft = Owner->GetCanvasCenter() - DisplaySize * 0.5;
		const FVector2D UV0(ViewState.bFlipX ? 1.0 : 0.0, ViewState.bFlipY ? 1.0 : 0.0);
		const FVector2D UV1(ViewState.bFlipX ? 0.0 : 1.0, ViewState.bFlipY ? 0.0 : 1.0);

		if (UMaterialInstanceDynamic* PreviewMaterial = Tool->GetCanvasMaskPreviewMaterial(ActiveRT))
		{
			FCanvasTileItem Tile(TopLeft, PreviewMaterial->GetRenderProxy(), DisplaySize, UV0, UV1);
			Tile.PivotPoint = FVector2D(0.5, 0.5);
			Tile.Rotation = FRotator(0.0, ViewState.RotationDegrees, 0.0);
			Tile.BlendMode = SE_BLEND_Opaque;
			Canvas->DrawItem(Tile);
			return;
		}

		DrawTextureLayer(Canvas, ActiveRT, FLinearColor::White, SE_BLEND_Opaque);
	}

	void DrawSolidTextureBackground(FCanvas* Canvas) const
	{
		const FIntPoint TextureSize = Owner->GetTextureSize();
		const double Zoom = Owner->GetEffectiveZoom();
		const FVector2D DisplaySize(TextureSize.X * Zoom, TextureSize.Y * Zoom);
		const FVector2D TopLeft = Owner->GetCanvasCenter() - DisplaySize * 0.5;
		FCanvasTileItem Tile(TopLeft, DisplaySize, FLinearColor(0.19f, 0.20f, 0.21f, 1.0f));
		Tile.PivotPoint = FVector2D(0.5, 0.5);
		Tile.Rotation = FRotator(0.0, Owner->GetViewState().RotationDegrees, 0.0);
		Tile.BlendMode = SE_BLEND_Opaque;
		Canvas->DrawItem(Tile);
	}

	void DrawTextureBorder(FCanvas* Canvas) const
	{
		const FIntPoint TextureSize = Owner->GetTextureSize();
		const FVector2D A = Owner->TexturePixelToViewport(FVector2D(0.0, 0.0));
		const FVector2D B = Owner->TexturePixelToViewport(FVector2D(TextureSize.X, 0.0));
		const FVector2D C = Owner->TexturePixelToViewport(FVector2D(TextureSize.X, TextureSize.Y));
		const FVector2D D = Owner->TexturePixelToViewport(FVector2D(0.0, TextureSize.Y));
		const FLinearColor BorderColor(0.88f, 0.90f, 0.92f, 0.72f);
		DrawCanvasLine(Canvas, A, B, BorderColor, 1.5f);
		DrawCanvasLine(Canvas, B, C, BorderColor, 1.5f);
		DrawCanvasLine(Canvas, C, D, BorderColor, 1.5f);
		DrawCanvasLine(Canvas, D, A, BorderColor, 1.5f);
	}

	void DrawPixelGrid(FCanvas* Canvas) const
	{
		if (!Owner->GetViewState().bShowPixelGrid)
		{
			return;
		}

		const double Zoom = Owner->GetEffectiveZoom();
		if (Zoom < 8.0)
		{
			return;
		}

		const FIntPoint TextureSize = Owner->GetTextureSize();
		const int32 Step = FMath::Max(1, FMath::CeilToInt(static_cast<double>(FMath::Max(TextureSize.X, TextureSize.Y)) / 2048.0));
		const FLinearColor GridColor(1.0f, 1.0f, 1.0f, Zoom >= 16.0 ? 0.16f : 0.08f);
		for (int32 X = 0; X <= TextureSize.X; X += Step)
		{
			DrawCanvasLine(
				Canvas,
				Owner->TexturePixelToViewport(FVector2D(X, 0.0)),
				Owner->TexturePixelToViewport(FVector2D(X, TextureSize.Y)),
				GridColor,
				1.0f);
		}
		for (int32 Y = 0; Y <= TextureSize.Y; Y += Step)
		{
			DrawCanvasLine(
				Canvas,
				Owner->TexturePixelToViewport(FVector2D(0.0, Y)),
				Owner->TexturePixelToViewport(FVector2D(TextureSize.X, Y)),
				GridColor,
				1.0f);
		}
	}

	void DrawBrushCursor(FViewport* Viewport, FCanvas* Canvas) const
	{
		if (!Owner->bHasHoverUV)
		{
			return;
		}

		UQuickSDFPaintTool* Tool = Owner->GetPaintTool();
		if (!Tool)
		{
			return;
		}

		const FIntPoint TextureSize = Owner->GetTextureSize();
		const FVector2D Pixel(Owner->HoverUV.X * TextureSize.X, Owner->HoverUV.Y * TextureSize.Y);
		const FVector2D Center = Owner->TexturePixelToViewport(Pixel);
		const double Radius = Tool->GetCurrentTextureCanvasBrushRadiusPixels() * Owner->GetEffectiveZoom();
		const bool bShadow = IsShiftDown(Viewport);
		const FLinearColor Outer = bShadow
			? FLinearColor(0.02f, 0.02f, 0.02f, 0.95f)
			: FLinearColor(1.0f, 0.92f, 0.26f, 0.95f);
		DrawCanvasCircle(Canvas, Center, Radius, FLinearColor(0.0f, 0.0f, 0.0f, 0.55f), 4.0f);
		DrawCanvasCircle(Canvas, Center, Radius, Outer, 2.0f);
	}

	SQuickSDFPaintCanvas* Owner = nullptr;
	TObjectPtr<UTexture2D> CheckerTexture;
	FVector2D LastMousePosition = FVector2D::ZeroVector;
	FVector2D BrushResizeStartMousePosition = FVector2D::ZeroVector;
	FVector2D BrushResizeAnchorAbsolutePosition = FVector2D::ZeroVector;
	double BrushResizeStartRadiusPixels = 1.0;
	double BrushResizeAccumulatedDeltaX = 0.0;
	bool bPainting = false;
	bool bPanning = false;
	bool bBrushResizing = false;
	bool bBrushResizingFromExternalPen = false;
	bool bBrushResizeCursorHidden = false;
	bool bPaintingFromExternalPen = false;
	bool bHasFreshExternalPenViewportPosition = false;
	bool bLastExternalPenInContact = false;
	bool bLastExternalPenOverViewport = false;
	FVector2D LastExternalPenViewportPosition = FVector2D::ZeroVector;
	FVector2D LastExternalPenAbsolutePosition = FVector2D::ZeroVector;
	double LastExternalPenUpdateTime = -1000.0;
};

void SQuickSDFPaintCanvas::Construct(const FArguments& InArgs)
{
	GActivePaintCanvas = this;

	ViewportWidget = SNew(SViewport)
		.EnableGammaCorrection(false)
		.EnableBlending(false)
		.ShowEffectWhenDisabled(false)
		.IgnoreTextureAlpha(false);

	ViewportClient = MakeShared<FQuickSDFPaintCanvasViewportClient>(this);
	SceneViewport = MakeShared<FSceneViewport>(ViewportClient.Get(), ViewportWidget);
	ViewportWidget->SetViewportInterface(SceneViewport.ToSharedRef());

	ChildSlot
	[
		SNew(SVerticalBox)
		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			BuildToolbar()
		]
		+ SVerticalBox::Slot()
		.FillHeight(1.0f)
		[
			SNew(SOverlay)
			+ SOverlay::Slot()
			[
				ViewportWidget.ToSharedRef()
			]
			+ SOverlay::Slot()
			.HAlign(HAlign_Right)
			.VAlign(VAlign_Fill)
			.Padding(0.0f, 0.0f, 2.0f, 14.0f)
			[
				SAssignNew(VerticalScrollBar, SScrollBar)
				.Orientation(Orient_Vertical)
				.Thickness(FVector2D(10.0f, 10.0f))
				.OnUserScrolled(this, &SQuickSDFPaintCanvas::HandleVerticalScrollBarScrolled)
			]
			+ SOverlay::Slot()
			.HAlign(HAlign_Fill)
			.VAlign(VAlign_Bottom)
			.Padding(0.0f, 0.0f, 14.0f, 2.0f)
			[
				SAssignNew(HorizontalScrollBar, SScrollBar)
				.Orientation(Orient_Horizontal)
				.Thickness(FVector2D(10.0f, 10.0f))
				.OnUserScrolled(this, &SQuickSDFPaintCanvas::HandleHorizontalScrollBarScrolled)
			]
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			BuildStatusBar()
		]
	];
}

SQuickSDFPaintCanvas::~SQuickSDFPaintCanvas()
{
	if (GActivePaintCanvas == this)
	{
		GActivePaintCanvas = nullptr;
	}

	if (ViewportClient.IsValid())
	{
		ViewportClient->EndActiveInput();
	}
	SceneViewport.Reset();
	ViewportClient.Reset();
}

void SQuickSDFPaintCanvas::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	SCompoundWidget::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);
	if (ViewportClient.IsValid() && (ViewportClient->IsExternalPenPainting() || ViewportClient->IsExternalPenBrushResizing()))
	{
		ViewportClient->TickExternalPenPointer();
	}
	const bool bExternalPenOverCanvas = ViewportClient.IsValid() && ViewportClient->HasFreshExternalPenOverViewport();
	const bool bExternalPenPainting = ViewportClient.IsValid() && ViewportClient->IsExternalPenPainting();
	const bool bCursorOverCanvas = AllottedGeometry.IsUnderLocation(FSlateApplication::Get().GetCursorPos()) || bExternalPenOverCanvas;
	if (UQuickSDFPaintTool* Tool = GetPaintTool())
	{
		Tool->SetTextureCanvasCursorActive(bCursorOverCanvas);
	}
	if (!bCursorOverCanvas && !bExternalPenPainting)
	{
		ClearHoverUV();
	}
	UpdateScrollBars();
	if (SceneViewport.IsValid())
	{
		SceneViewport->InvalidateDisplay();
	}
}

TSharedRef<SWidget> SQuickSDFPaintCanvas::BuildToolbar()
{
	return SNew(SBorder)
		.BorderImage(FAppStyle::GetBrush("WhiteBrush"))
		.BorderBackgroundColor(FLinearColor(0.055f, 0.058f, 0.064f, 1.0f))
		.Padding(FMargin(6.0f, 5.0f))
		[
			SNew(SScrollBox)
			.Orientation(Orient_Horizontal)
			+ SScrollBox::Slot()
			.Padding(0.0f, 0.0f, 8.0f, 0.0f)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("CanvasTitle", "Quick SDF 2D Canvas"))
				.Font(FAppStyle::GetFontStyle("SmallFontBold"))
				.ColorAndOpacity(FLinearColor(0.88f, 0.90f, 0.92f, 1.0f))
			]
			+ SScrollBox::Slot()
			.Padding(0.0f, 0.0f, 5.0f, 0.0f)
			[
				BuildTextureSetSelector()
			]
			+ SScrollBox::Slot()
			.Padding(0.0f, 0.0f, 5.0f, 0.0f)
			[
				BuildAngleSelector()
			]
			+ SScrollBox::Slot()
			.Padding(0.0f, 0.0f, 5.0f, 0.0f)
			[
				BuildBrushSizeControl()
			]
			+ SScrollBox::Slot()
			.Padding(0.0f, 0.0f, 5.0f, 0.0f)
			[
				QuickSDFToolUI::MakeMaterialPreviewModeSelector([]() { return QuickSDFToolUI::GetActivePaintTool(); })
			]
			+ SScrollBox::Slot()
			.Padding(0.0f, 0.0f, 8.0f, 0.0f)
			[
				QuickSDFToolUI::MakePaintToggleBar([]() { return QuickSDFToolUI::GetActivePaintTool(); })
			]
			+ SScrollBox::Slot()
			.Padding(0.0f, 0.0f, 4.0f, 0.0f)
			[
				MakeCanvasButton(LOCTEXT("FitButton", "Fit"), LOCTEXT("FitTooltip", "Fit the active mask into the canvas."), FOnClicked::CreateLambda([this]() { FitCanvas(); return FReply::Handled(); }))
			]
			+ SScrollBox::Slot()
			.Padding(0.0f, 0.0f, 4.0f, 0.0f)
			[
				MakeCanvasButton(LOCTEXT("ActualSizeButton", "100%"), LOCTEXT("ActualSizeTooltip", "Show one texture pixel as one canvas pixel."), FOnClicked::CreateLambda([this]() { SetActualSize(); return FReply::Handled(); }))
			]
			+ SScrollBox::Slot()
			.Padding(0.0f, 0.0f, 4.0f, 0.0f)
			[
				MakeCanvasButton(LOCTEXT("RotateLeftButton", "-90"), LOCTEXT("RotateLeftTooltip", "Rotate the canvas view counter-clockwise."), FOnClicked::CreateLambda([this]() { RotateBy(-90.0); return FReply::Handled(); }))
			]
			+ SScrollBox::Slot()
			.Padding(0.0f, 0.0f, 4.0f, 0.0f)
			[
				MakeCanvasButton(LOCTEXT("RotateRightButton", "+90"), LOCTEXT("RotateRightTooltip", "Rotate the canvas view clockwise."), FOnClicked::CreateLambda([this]() { RotateBy(90.0); return FReply::Handled(); }))
			]
			+ SScrollBox::Slot()
			.Padding(0.0f, 0.0f, 4.0f, 0.0f)
			[
				MakeCanvasButton(LOCTEXT("FlipXButton", "Flip X"), LOCTEXT("FlipXTooltip", "Flip the canvas view horizontally."), FOnClicked::CreateLambda([this]() { ToggleFlipX(); return FReply::Handled(); }))
			]
			+ SScrollBox::Slot()
			.Padding(0.0f, 0.0f, 4.0f, 0.0f)
			[
				MakeCanvasButton(LOCTEXT("FlipYButton", "Flip Y"), LOCTEXT("FlipYTooltip", "Flip the canvas view vertically."), FOnClicked::CreateLambda([this]() { ToggleFlipY(); return FReply::Handled(); }))
			]
			+ SScrollBox::Slot()
			.Padding(0.0f, 0.0f, 4.0f, 0.0f)
			[
				MakeCanvasButton(LOCTEXT("CheckerButton", "Checker"), LOCTEXT("CheckerTooltip", "Toggle the checkerboard background."), FOnClicked::CreateLambda([this]() { ToggleCheckerboard(); return FReply::Handled(); }))
			]
			+ SScrollBox::Slot()
			[
				MakeCanvasButton(LOCTEXT("PixelGridButton", "Grid"), LOCTEXT("PixelGridTooltip", "Toggle the high-zoom pixel grid."), FOnClicked::CreateLambda([this]() { TogglePixelGrid(); return FReply::Handled(); }))
			]
		];
}

TSharedRef<SWidget> SQuickSDFPaintCanvas::BuildBrushSizeControl()
{
	return SNew(SHorizontalBox)
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		.Padding(0.0f, 0.0f, 3.0f, 0.0f)
		[
			SNew(SImage)
			.Image(FQuickSDFToolStyle::GetBrush("QuickSDF.PaintTextureColor"))
			.ToolTipText(LOCTEXT("BrushSizeIconTooltip", "Brush size"))
		]
		+ SHorizontalBox::Slot()
		.AutoWidth()
		[
			SNew(SBox)
			.WidthOverride(84.0f)
			[
				SNew(SSpinBox<float>)
				.MinValue(1.0f)
				.MaxValue(4096.0f)
				.MinSliderValue(1.0f)
				.MaxSliderValue(256.0f)
				.Delta(1.0f)
				.Value_Lambda([this]()
				{
					return static_cast<float>(GetBrushRadiusPixels());
				})
				.OnValueChanged_Lambda([this](float NewValue)
				{
					SetBrushRadiusPixels(NewValue);
				})
				.ToolTipText(LOCTEXT("BrushSizeSpinBoxTooltip", "Brush size in texture pixels. Ctrl + Mouse Wheel, [ and ], or Ctrl + F then move horizontally also adjust it."))
			]
		];
}

TSharedRef<SWidget> SQuickSDFPaintCanvas::BuildStatusBar()
{
	return SNew(SBorder)
		.BorderImage(FAppStyle::GetBrush("WhiteBrush"))
		.BorderBackgroundColor(FLinearColor(0.045f, 0.048f, 0.052f, 1.0f))
		.Padding(FMargin(8.0f, 3.0f))
		[
			SNew(STextBlock)
			.Text(this, &SQuickSDFPaintCanvas::GetStatusText)
			.Font(FAppStyle::GetFontStyle("SmallFont"))
			.ColorAndOpacity(FLinearColor(0.78f, 0.80f, 0.82f, 1.0f))
		];
}

TSharedRef<SWidget> SQuickSDFPaintCanvas::BuildTextureSetSelector()
{
	return SNew(SComboButton)
		.ContentPadding(FMargin(7.0f, 2.0f))
		.ToolTipText(LOCTEXT("TextureSetSelectorTooltip", "Choose the active material texture set."))
		.OnGetMenuContent(this, &SQuickSDFPaintCanvas::MakeTextureSetMenu)
		.ButtonContent()
		[
			SNew(STextBlock)
			.Text_Lambda([this]()
			{
				UQuickSDFPaintTool* Tool = GetPaintTool();
				return Tool ? Tool->GetActiveTextureSetLabel() : LOCTEXT("NoTextureSet", "No Texture Set");
			})
			.Font(FAppStyle::GetFontStyle("SmallFont"))
		];
}

TSharedRef<SWidget> SQuickSDFPaintCanvas::BuildAngleSelector()
{
	return SNew(SComboButton)
		.ContentPadding(FMargin(7.0f, 2.0f))
		.ToolTipText(LOCTEXT("AngleSelectorTooltip", "Choose the active paint angle."))
		.OnGetMenuContent(this, &SQuickSDFPaintCanvas::MakeAngleMenu)
		.ButtonContent()
		[
			SNew(STextBlock)
			.Text_Lambda([this]()
			{
				UQuickSDFPaintTool* Tool = GetPaintTool();
				UQuickSDFToolProperties* Properties = Tool ? Tool->Properties : nullptr;
				if (!Properties || !Properties->TargetAngles.IsValidIndex(Properties->EditAngleIndex))
				{
					return LOCTEXT("NoAngle", "Angle --");
				}
				return FText::FromString(FString::Printf(TEXT("Angle %.0f"), Properties->TargetAngles[Properties->EditAngleIndex]));
			})
			.Font(FAppStyle::GetFontStyle("SmallFont"))
		];
}

TSharedRef<SWidget> SQuickSDFPaintCanvas::MakeTextureSetMenu()
{
	FMenuBuilder MenuBuilder(true, nullptr);
	UQuickSDFAsset* Asset = GetActiveQuickSDFAsset();
	if (!Asset || Asset->TextureSets.Num() == 0)
	{
		MenuBuilder.AddMenuEntry(LOCTEXT("NoTextureSetsMenu", "No texture sets"), FText::GetEmpty(), FSlateIcon(), FUIAction());
		return MenuBuilder.MakeWidget();
	}

	for (int32 Index = 0; Index < Asset->TextureSets.Num(); ++Index)
	{
		const FQuickSDFTextureSetData& TextureSet = Asset->TextureSets[Index];
		const FText Label = FText::FromString(FString::Printf(
			TEXT("Slot %d  %s"),
			TextureSet.MaterialSlotIndex,
			*TextureSet.SlotName.ToString()));
		MenuBuilder.AddMenuEntry(
			Label,
			FText::FromString(TextureSet.MaterialName),
			FSlateIcon(),
			FUIAction(
				FExecuteAction::CreateLambda([Index]()
				{
					if (UQuickSDFPaintTool* Tool = QuickSDFToolUI::GetActivePaintTool())
					{
						Tool->SelectTextureSet(Index);
					}
				}),
				FCanExecuteAction(),
				FIsActionChecked::CreateLambda([Index]()
				{
					const UQuickSDFAsset* CurrentAsset = GetActiveQuickSDFAsset();
					return CurrentAsset && CurrentAsset->ActiveTextureSetIndex == Index;
				})),
			NAME_None,
			EUserInterfaceActionType::RadioButton);
	}
	return MenuBuilder.MakeWidget();
}

TSharedRef<SWidget> SQuickSDFPaintCanvas::MakeAngleMenu()
{
	FMenuBuilder MenuBuilder(true, nullptr);
	UQuickSDFPaintTool* Tool = GetPaintTool();
	UQuickSDFToolProperties* Properties = Tool ? Tool->Properties : nullptr;
	if (!Tool || !Properties || Properties->TargetAngles.Num() == 0)
	{
		MenuBuilder.AddMenuEntry(LOCTEXT("NoAnglesMenu", "No angles"), FText::GetEmpty(), FSlateIcon(), FUIAction());
		return MenuBuilder.MakeWidget();
	}

	for (int32 Index = 0; Index < Properties->TargetAngles.Num(); ++Index)
	{
		const float Angle = Properties->TargetAngles[Index];
		MenuBuilder.AddMenuEntry(
			FText::FromString(FString::Printf(TEXT("%.0f deg"), Angle)),
			LOCTEXT("AngleEntryTooltip", "Make this angle active for 2D canvas painting."),
			FSlateIcon(),
			FUIAction(
				FExecuteAction::CreateLambda([Index]()
				{
					UQuickSDFPaintTool* ActiveTool = QuickSDFToolUI::GetActivePaintTool();
					UQuickSDFToolProperties* ActiveProperties = ActiveTool ? ActiveTool->Properties : nullptr;
					if (!ActiveTool || !ActiveProperties || !ActiveProperties->TargetAngles.IsValidIndex(Index))
					{
						return;
					}
					ActiveProperties->EditAngleIndex = Index;
					ActiveTool->SetTimelinePreviewSeekAngleToActiveKey();
					FProperty* Prop = ActiveProperties->GetClass()->FindPropertyByName(GET_MEMBER_NAME_CHECKED(UQuickSDFToolProperties, EditAngleIndex));
					ActiveTool->OnPropertyModified(ActiveProperties, Prop);
					if (GEditor)
					{
						GEditor->RedrawAllViewports(false);
					}
				}),
				FCanExecuteAction(),
				FIsActionChecked::CreateLambda([Index]()
				{
					UQuickSDFPaintTool* ActiveTool = QuickSDFToolUI::GetActivePaintTool();
					const UQuickSDFToolProperties* ActiveProperties = ActiveTool ? ActiveTool->Properties : nullptr;
					return ActiveProperties && ActiveProperties->EditAngleIndex == Index;
				})),
			NAME_None,
			EUserInterfaceActionType::RadioButton);
	}
	return MenuBuilder.MakeWidget();
}

UQuickSDFPaintTool* SQuickSDFPaintCanvas::GetPaintTool() const
{
	return QuickSDFToolUI::GetActivePaintTool();
}

UTextureRenderTarget2D* SQuickSDFPaintCanvas::GetActiveRenderTarget() const
{
	UQuickSDFPaintTool* Tool = GetPaintTool();
	return Tool ? Tool->GetActiveRenderTarget() : nullptr;
}

FVector2D SQuickSDFPaintCanvas::GetViewportSize() const
{
	if (!SceneViewport.IsValid())
	{
		return FVector2D(1.0, 1.0);
	}
	const FIntPoint Size = SceneViewport->GetSizeXY();
	return FVector2D(FMath::Max(Size.X, 1), FMath::Max(Size.Y, 1));
}

FIntPoint SQuickSDFPaintCanvas::GetTextureSize() const
{
	UTextureRenderTarget2D* RT = GetActiveRenderTarget();
	return RT ? FIntPoint(FMath::Max(RT->SizeX, 1), FMath::Max(RT->SizeY, 1)) : FIntPoint(1, 1);
}

double SQuickSDFPaintCanvas::GetFitZoom() const
{
	const FIntPoint TextureSize = GetTextureSize();
	const FVector2D ViewportSize = GetViewportSize();
	const double AvailableW = FMath::Max(ViewportSize.X - 48.0, 1.0);
	const double AvailableH = FMath::Max(ViewportSize.Y - 48.0, 1.0);
	return FMath::Clamp(FMath::Min(AvailableW / TextureSize.X, AvailableH / TextureSize.Y), 0.02, 64.0);
}

double SQuickSDFPaintCanvas::GetEffectiveZoom() const
{
	return ViewState.bFitToViewport ? GetFitZoom() : FMath::Clamp(ViewState.Zoom, 0.02, 64.0);
}

FVector2D SQuickSDFPaintCanvas::GetCanvasCenter() const
{
	return GetViewportSize() * 0.5 + ViewState.Pan;
}

FVector2D SQuickSDFPaintCanvas::TexturePixelToViewport(const FVector2D& Pixel) const
{
	const FIntPoint TextureSize = GetTextureSize();
	const FVector2D HalfSize(TextureSize.X * 0.5, TextureSize.Y * 0.5);
	FVector2D Local = Pixel - HalfSize;
	if (ViewState.bFlipX)
	{
		Local.X = -Local.X;
	}
	if (ViewState.bFlipY)
	{
		Local.Y = -Local.Y;
	}

	const double Radians = FMath::DegreesToRadians(ViewState.RotationDegrees);
	const double C = FMath::Cos(Radians);
	const double S = FMath::Sin(Radians);
	const FVector2D Rotated(Local.X * C - Local.Y * S, Local.X * S + Local.Y * C);
	return GetCanvasCenter() + Rotated * GetEffectiveZoom();
}

bool SQuickSDFPaintCanvas::ViewportToTextureUV(const FVector2D& ViewportPosition, FVector2f& OutUV) const
{
	const FIntPoint TextureSize = GetTextureSize();
	const FVector2D HalfSize(TextureSize.X * 0.5, TextureSize.Y * 0.5);
	const double Zoom = FMath::Max(GetEffectiveZoom(), 0.0001);
	FVector2D Local = (ViewportPosition - GetCanvasCenter()) / Zoom;

	const double Radians = FMath::DegreesToRadians(ViewState.RotationDegrees);
	const double C = FMath::Cos(Radians);
	const double S = FMath::Sin(Radians);
	FVector2D Unrotated(Local.X * C + Local.Y * S, -Local.X * S + Local.Y * C);
	if (ViewState.bFlipX)
	{
		Unrotated.X = -Unrotated.X;
	}
	if (ViewState.bFlipY)
	{
		Unrotated.Y = -Unrotated.Y;
	}

	const FVector2D Pixel = Unrotated + HalfSize;
	const bool bInside =
		Pixel.X >= 0.0 && Pixel.Y >= 0.0 &&
		Pixel.X <= TextureSize.X && Pixel.Y <= TextureSize.Y;
	OutUV = FVector2f(
		static_cast<float>(FMath::Clamp(Pixel.X / TextureSize.X, 0.0, 1.0)),
		static_cast<float>(FMath::Clamp(Pixel.Y / TextureSize.Y, 0.0, 1.0)));
	return bInside;
}

bool SQuickSDFPaintCanvas::IsViewportPositionInsideTexture(const FVector2D& ViewportPosition, FVector2f* OutUV) const
{
	FVector2f UV;
	const bool bInside = ViewportToTextureUV(ViewportPosition, UV);
	if (OutUV)
	{
		*OutUV = UV;
	}
	return bInside;
}

bool SQuickSDFPaintCanvas::TryResolveAbsoluteViewportPosition(
	const FVector2D& AbsoluteScreenPosition,
	FVector2D& OutViewportPosition,
	bool bRequireUnderViewport,
	bool* bOutUnderViewport) const
{
	if (!ViewportWidget.IsValid() || !SceneViewport.IsValid())
	{
		if (bOutUnderViewport)
		{
			*bOutUnderViewport = false;
		}
		return false;
	}

	const FGeometry ViewportGeometry = ViewportWidget->GetTickSpaceGeometry();
	const bool bUnderViewport = ViewportGeometry.IsUnderLocation(AbsoluteScreenPosition);
	if (bOutUnderViewport)
	{
		*bOutUnderViewport = bUnderViewport;
	}
	if (bRequireUnderViewport && !bUnderViewport)
	{
		return false;
	}

	const FVector2D LocalPosition = ViewportGeometry.AbsoluteToLocal(AbsoluteScreenPosition);
	const FVector2D GeometrySize = ViewportGeometry.GetLocalSize();
	const FIntPoint ViewportSize = SceneViewport->GetSizeXY();
	OutViewportPosition = LocalPosition;
	if (GeometrySize.X > KINDA_SMALL_NUMBER && GeometrySize.Y > KINDA_SMALL_NUMBER && ViewportSize.X > 0 && ViewportSize.Y > 0)
	{
		OutViewportPosition.X = LocalPosition.X * static_cast<double>(ViewportSize.X) / GeometrySize.X;
		OutViewportPosition.Y = LocalPosition.Y * static_cast<double>(ViewportSize.Y) / GeometrySize.Y;
	}
	return true;
}

bool SQuickSDFPaintCanvas::UpdateExternalPenPointerState(const FVector2D& AbsoluteScreenPosition, bool bInContact)
{
	return ViewportClient.IsValid()
		? ViewportClient->HandleExternalPenPointer(AbsoluteScreenPosition, bInContact)
		: false;
}

bool SQuickSDFPaintCanvas::RequestBrushResizeFromHoveredCanvas(const FVector2D& AbsoluteScreenPosition, bool bFromExternalPen)
{
	return ViewportClient.IsValid()
		? ViewportClient->RequestBrushResize(AbsoluteScreenPosition, bFromExternalPen)
		: false;
}

void SQuickSDFPaintCanvas::FitCanvas()
{
	ViewState.bFitToViewport = true;
	ViewState.Pan = FVector2D::ZeroVector;
}

void SQuickSDFPaintCanvas::SetActualSize()
{
	ViewState.bFitToViewport = false;
	ViewState.Zoom = 1.0;
	ViewState.Pan = FVector2D::ZeroVector;
}

void SQuickSDFPaintCanvas::ZoomBy(double Factor, const FVector2D& PivotViewportPosition)
{
	FVector2f PivotUV;
	const bool bHasPivot = ViewportToTextureUV(PivotViewportPosition, PivotUV);
	const FIntPoint TextureSize = GetTextureSize();
	ViewState.bFitToViewport = false;
	ViewState.Zoom = FMath::Clamp(GetEffectiveZoom() * Factor, 0.02, 64.0);
	if (bHasPivot)
	{
		const FVector2D PivotPixel(PivotUV.X * TextureSize.X, PivotUV.Y * TextureSize.Y);
		const FVector2D NewPivotViewportPosition = TexturePixelToViewport(PivotPixel);
		ViewState.Pan += PivotViewportPosition - NewPivotViewportPosition;
	}
}

void SQuickSDFPaintCanvas::RotateBy(double DeltaDegrees)
{
	ViewState.RotationDegrees = FMath::Fmod(ViewState.RotationDegrees + DeltaDegrees + 360.0, 360.0);
}

void SQuickSDFPaintCanvas::ToggleFlipX()
{
	ViewState.bFlipX = !ViewState.bFlipX;
}

void SQuickSDFPaintCanvas::ToggleFlipY()
{
	ViewState.bFlipY = !ViewState.bFlipY;
}

void SQuickSDFPaintCanvas::ToggleCheckerboard()
{
	ViewState.bShowCheckerboard = !ViewState.bShowCheckerboard;
}

void SQuickSDFPaintCanvas::TogglePixelGrid()
{
	ViewState.bShowPixelGrid = !ViewState.bShowPixelGrid;
}

void SQuickSDFPaintCanvas::SetHoverUV(const FVector2f& UV)
{
	HoverUV = UV;
	bHasHoverUV = true;
}

void SQuickSDFPaintCanvas::ClearHoverUV()
{
	bHasHoverUV = false;
}

double SQuickSDFPaintCanvas::GetBrushRadiusPixels() const
{
	UQuickSDFPaintTool* Tool = GetPaintTool();
	return Tool ? Tool->GetTextureCanvasBrushRadiusPixels() : 1.0;
}

void SQuickSDFPaintCanvas::SetBrushRadiusPixels(double NewRadiusPixels)
{
	if (UQuickSDFPaintTool* Tool = GetPaintTool())
	{
		Tool->SetTextureCanvasBrushRadiusPixels(NewRadiusPixels);
	}
}

void SQuickSDFPaintCanvas::AdjustBrushRadiusPixels(double DeltaPixels)
{
	SetBrushRadiusPixels(GetBrushRadiusPixels() + DeltaPixels);
}

void SQuickSDFPaintCanvas::UpdateScrollBars()
{
	if (!HorizontalScrollBar.IsValid() || !VerticalScrollBar.IsValid())
	{
		return;
	}

	const FIntPoint TextureSize = GetTextureSize();
	const FVector2D ViewportSize = GetViewportSize();
	const double Zoom = GetEffectiveZoom();
	const double DisplayW = TextureSize.X * Zoom;
	const double DisplayH = TextureSize.Y * Zoom;
	const double TravelX = FMath::Max(DisplayW - ViewportSize.X + 96.0, 1.0);
	const double TravelY = FMath::Max(DisplayH - ViewportSize.Y + 96.0, 1.0);
	const float ThumbX = static_cast<float>(FMath::Clamp(ViewportSize.X / FMath::Max(DisplayW + 96.0, 1.0), 0.06, 1.0));
	const float ThumbY = static_cast<float>(FMath::Clamp(ViewportSize.Y / FMath::Max(DisplayH + 96.0, 1.0), 0.06, 1.0));
	const float OffsetX = static_cast<float>(FMath::Clamp(0.5 - ViewState.Pan.X / TravelX, 0.0, 1.0));
	const float OffsetY = static_cast<float>(FMath::Clamp(0.5 - ViewState.Pan.Y / TravelY, 0.0, 1.0));

	TGuardValue<bool> Guard(bUpdatingScrollBars, true);
	HorizontalScrollBar->SetState(OffsetX, ThumbX);
	VerticalScrollBar->SetState(OffsetY, ThumbY);
}

void SQuickSDFPaintCanvas::HandleHorizontalScrollBarScrolled(float ScrollOffset)
{
	if (bUpdatingScrollBars)
	{
		return;
	}

	const FIntPoint TextureSize = GetTextureSize();
	const FVector2D ViewportSize = GetViewportSize();
	const double TravelX = FMath::Max(TextureSize.X * GetEffectiveZoom() - ViewportSize.X + 96.0, 1.0);
	ViewState.bFitToViewport = false;
	ViewState.Pan.X = (0.5 - static_cast<double>(ScrollOffset)) * TravelX;
}

void SQuickSDFPaintCanvas::HandleVerticalScrollBarScrolled(float ScrollOffset)
{
	if (bUpdatingScrollBars)
	{
		return;
	}

	const FIntPoint TextureSize = GetTextureSize();
	const FVector2D ViewportSize = GetViewportSize();
	const double TravelY = FMath::Max(TextureSize.Y * GetEffectiveZoom() - ViewportSize.Y + 96.0, 1.0);
	ViewState.bFitToViewport = false;
	ViewState.Pan.Y = (0.5 - static_cast<double>(ScrollOffset)) * TravelY;
}

FText SQuickSDFPaintCanvas::GetStatusText() const
{
	UQuickSDFPaintTool* Tool = GetPaintTool();
	UTextureRenderTarget2D* RT = GetActiveRenderTarget();
	UQuickSDFToolProperties* Properties = Tool ? Tool->Properties : nullptr;
	const FString ResolutionText = RT ? FString::Printf(TEXT("%dx%d"), RT->SizeX, RT->SizeY) : FString(TEXT("--"));
	const FString ZoomText = FString::Printf(TEXT("%.0f%%"), GetEffectiveZoom() * 100.0);

	FString CursorText(TEXT("UV --  Pixel --"));
	if (bHasHoverUV && RT)
	{
		const int32 PixelX = FMath::Clamp(FMath::FloorToInt(HoverUV.X * RT->SizeX), 0, RT->SizeX - 1);
		const int32 PixelY = FMath::Clamp(FMath::FloorToInt(HoverUV.Y * RT->SizeY), 0, RT->SizeY - 1);
		CursorText = FString::Printf(TEXT("UV %.3f, %.3f  Pixel %d, %d"), HoverUV.X, HoverUV.Y, PixelX, PixelY);
	}

	FString AngleText(TEXT("Angle --"));
	if (Properties && Properties->TargetAngles.IsValidIndex(Properties->EditAngleIndex))
	{
		AngleText = FString::Printf(TEXT("Angle %.0f deg"), Properties->TargetAngles[Properties->EditAngleIndex]);
	}

	const FString TargetModeText = Properties
		? QuickSDFToolUI::GetPaintTargetModeLabel(QuickSDFToolUI::GetPaintTargetMode(Properties)).ToString()
		: FString(TEXT("--"));

	const UQuickSDFAsset* Asset = GetActiveQuickSDFAsset();
	const FQuickSDFTextureSetData* ActiveSet = Asset ? Asset->GetActiveTextureSet() : nullptr;
	const FString DirtyText = ActiveSet && ActiveSet->bDirty ? FString(TEXT("Dirty")) : FString(TEXT("Clean"));
	const FString PreviewText = Tool ? Tool->GetMaterialPreviewStatusText().ToString() : FString(TEXT("No Tool"));
	const FString BrushText = FString::Printf(TEXT("Brush %.0f px"), GetBrushRadiusPixels());

	return FText::FromString(FString::Printf(
		TEXT("Resolution %s   Zoom %s   %s   %s   %s   Target %s   %s   Preview %s"),
		*ResolutionText,
		*ZoomText,
		*CursorText,
		*BrushText,
		*AngleText,
		*TargetModeText,
		*DirtyText,
		*PreviewText));
}

#undef LOCTEXT_NAMESPACE
