#pragma once

#include "CoreMinimal.h"
#include "QuickSDFToolTypes.h"
#include "Widgets/SCompoundWidget.h"

class FQuickSDFPaintCanvasViewportClient;
class FSceneViewport;
class SScrollBar;
class SViewport;
class UQuickSDFPaintTool;
class UTextureRenderTarget2D;

namespace QuickSDFPaintCanvas
{
const FName& GetTabId();
void OpenTab();
bool UpdateExternalPenPointerState(const FVector2D& AbsoluteScreenPosition, bool bInContact);
}

class SQuickSDFPaintCanvas : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SQuickSDFPaintCanvas) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	virtual ~SQuickSDFPaintCanvas() override;
	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;

	FQuickSDFTextureCanvasViewState& GetViewState() { return ViewState; }
	const FQuickSDFTextureCanvasViewState& GetViewState() const { return ViewState; }
	UQuickSDFPaintTool* GetPaintTool() const;
	UTextureRenderTarget2D* GetActiveRenderTarget() const;
	FVector2D GetViewportSize() const;
	double GetEffectiveZoom() const;
	FVector2D TexturePixelToViewport(const FVector2D& Pixel) const;
	bool ViewportToTextureUV(const FVector2D& ViewportPosition, FVector2f& OutUV) const;
	bool IsViewportPositionInsideTexture(const FVector2D& ViewportPosition, FVector2f* OutUV = nullptr) const;
	void FitCanvas();
	void SetActualSize();
	void ZoomBy(double Factor, const FVector2D& PivotViewportPosition);
	void RotateBy(double DeltaDegrees);
	void ToggleFlipX();
	void ToggleFlipY();
	void ToggleCheckerboard();
	void TogglePixelGrid();
	void SetHoverUV(const FVector2f& UV);
	void ClearHoverUV();
	bool UpdateExternalPenPointerState(const FVector2D& AbsoluteScreenPosition, bool bInContact);
	double GetBrushRadiusPixels() const;
	void SetBrushRadiusPixels(double NewRadiusPixels);
	void AdjustBrushRadiusPixels(double DeltaPixels);
	FText GetStatusText() const;

private:
	friend class FQuickSDFPaintCanvasViewportClient;

	TSharedRef<SWidget> BuildToolbar();
	TSharedRef<SWidget> BuildStatusBar();
	TSharedRef<SWidget> BuildTextureSetSelector();
	TSharedRef<SWidget> BuildAngleSelector();
	TSharedRef<SWidget> BuildBrushSizeControl();
	TSharedRef<SWidget> MakeTextureSetMenu();
	TSharedRef<SWidget> MakeAngleMenu();
	void UpdateScrollBars();
	void HandleHorizontalScrollBarScrolled(float ScrollOffset);
	void HandleVerticalScrollBarScrolled(float ScrollOffset);
	double GetFitZoom() const;
	FVector2D GetCanvasCenter() const;
	FIntPoint GetTextureSize() const;
	bool TryResolveAbsoluteViewportPosition(
		const FVector2D& AbsoluteScreenPosition,
		FVector2D& OutViewportPosition,
		bool bRequireUnderViewport,
		bool* bOutUnderViewport = nullptr) const;

	TSharedPtr<SViewport> ViewportWidget;
	TSharedPtr<FSceneViewport> SceneViewport;
	TSharedPtr<FQuickSDFPaintCanvasViewportClient> ViewportClient;
	TSharedPtr<SScrollBar> HorizontalScrollBar;
	TSharedPtr<SScrollBar> VerticalScrollBar;

	FQuickSDFTextureCanvasViewState ViewState;
	FVector2f HoverUV = FVector2f::ZeroVector;
	bool bHasHoverUV = false;
	bool bUpdatingScrollBars = false;
};
