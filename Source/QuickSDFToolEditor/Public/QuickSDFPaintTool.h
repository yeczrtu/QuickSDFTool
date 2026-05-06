#pragma once

#include "CoreMinimal.h"
#include "BaseBehaviors/AnyButtonInputBehavior.h"
#include "BaseTools/BaseBrushTool.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "DynamicMesh/DynamicMeshAABBTree3.h"
#include "InteractiveToolBuilder.h"
#include "InteractiveToolActionSet.h"
#include "QuickSDFAsset.h"
#include "QuickSDFToolTypes.h"
#include "QuickSDFToolProperties.h"
#include "Components/MeshComponent.h"
#include "QuickSDFPaintTool.generated.h"

UCLASS()
class UQuickSDFBrushResizeInputBehavior : public UAnyButtonInputBehavior
{
	GENERATED_BODY()

public:
	//TODO:入力系をコマンドに移動させる
	void Initialize(class UQuickSDFPaintTool* InTool);

	virtual EInputDevices GetSupportedDevices() override;
	virtual bool IsPressed(const FInputDeviceState& Input) override;
	virtual bool IsReleased(const FInputDeviceState& Input) override;
	virtual FInputCaptureRequest WantsCapture(const FInputDeviceState& Input) override;
	virtual FInputCaptureUpdate BeginCapture(const FInputDeviceState& Input, EInputCaptureSide Side) override;
	virtual FInputCaptureUpdate UpdateCapture(const FInputDeviceState& Input, const FInputCaptureData& Data) override;
	virtual void ForceEndCapture(const FInputCaptureData& Data) override;

protected:
	UPROPERTY()
	TObjectPtr<class UQuickSDFPaintTool> BrushTool;
};

struct FOneEuroFilter
{
	double MinCutoff;
	double Beta;
	double DCutoff;
	FVector3d LastValue;
	FVector3d LastDerivative;
	bool bFirstUpdate;

	FOneEuroFilter(double InMinCutoff = 1.0, double InBeta = 0.007, double InDCutoff = 4.0)
		: MinCutoff(InMinCutoff), Beta(InBeta), DCutoff(InDCutoff), bFirstUpdate(true) {}

	void Reset() { bFirstUpdate = true; }

	double Alpha(double InCutoff, double InDeltaTime)
	{
		double Tau = 1.0 / (2.0 * PI * InCutoff);
		return 1.0 / (1.0 + Tau / InDeltaTime);
	}

	FVector3d Update(FVector3d InValue, double InDeltaTime)
	{
		if (bFirstUpdate)
		{
			bFirstUpdate = false;
			LastValue = InValue;
			LastDerivative = FVector3d::Zero();
			return InValue;
		}

		if (InDeltaTime <= 0.0) return LastValue;
		
		FVector3d Derivative = (InValue - LastValue) / InDeltaTime;
		double DAlpha = Alpha(DCutoff, InDeltaTime);
		FVector3d FilteredDerivative = FMath::Lerp(LastDerivative, Derivative, DAlpha);
		LastDerivative = FilteredDerivative;
		
		double Cutoff = MinCutoff + Beta * FilteredDerivative.Size();
		double A = Alpha(Cutoff, InDeltaTime);
		FVector3d FilteredValue = FMath::Lerp(LastValue, InValue, A);
		LastValue = FilteredValue;

		return FilteredValue;
	}
};

struct FQuickSDFSurfaceBrushParams
{
	FVector3d Center = FVector3d::Zero();
	FVector3d LineStart = FVector3d::Zero();
	FVector3d LineEnd = FVector3d::Zero();
	FVector3d Normal = FVector3d(0.0, 0.0, 1.0);
	FMatrix WorldToBrushMatrix = FMatrix::Identity;
	float Radius = 1.0f;
	float RadialFalloffRange = 0.0f;
	float Depth = 1.0f;
	float DepthFalloffRange = 0.0f;
	float Strength = 1.0f;
	float AntialiasWidth = 0.0f;
	float LineLength = 0.0f;
	bool bIsLine = false;
	FLinearColor Color = FLinearColor::White;
	int32 PaintChartID = INDEX_NONE;
};

struct FQuickSDFSurfacePaintTriangle
{
	FVector2D UVs[3];
	FVector2D PixelPositions[3];
	FVector3d WorldPositions[3];
};

struct FQuickSDFProjectedPaintParams
{
	FVector3d ProjectionOrigin = FVector3d::Zero();
	FVector3d ProjectionAxisX = FVector3d(1.0, 0.0, 0.0);
	FVector3d ProjectionAxisY = FVector3d(0.0, 1.0, 0.0);
	FVector3d ProjectionNormal = FVector3d(0.0, 0.0, 1.0);
	float Radius = 1.0f;
	float RadialFalloffRange = 0.0f;
	float Depth = 1.0f;
	float DepthFalloffRange = 0.0f;
	float Strength = 1.0f;
	float AntialiasWidth = 0.0f;
	FLinearColor Color = FLinearColor::White;
	int32 PaintChartID = INDEX_NONE;
};

struct FQuickSDFProjectedStrokePoint
{
	FVector3d WorldPosition = FVector3d::Zero();
	FVector2f ProjectedPosition = FVector2f::Zero();
	FVector3d Normal = FVector3d(0.0, 0.0, 1.0);
	float Pressure = 1.0f;
};

struct FQuickSDFProjectedPaintTriangle
{
	FVector2D UVs[3];
	FVector2D PixelPositions[3];
	FVector3d WorldPositions[3];
	int32 PaintChartID = INDEX_NONE;
};

struct FQuickSDFScreenProjectionPaintParams
{
	FVector3d ViewOrigin = FVector3d::Zero();
	FVector3d ViewRight = FVector3d(1.0, 0.0, 0.0);
	FVector3d ViewUp = FVector3d(0.0, 0.0, 1.0);
	FVector3d ViewForward = FVector3d(0.0, 1.0, 0.0);
	FVector2f ScreenOffset = FVector2f::ZeroVector;
	FVector2f ProjectionScale = FVector2f(1.0f, 1.0f);
	FVector2f ViewportSize = FVector2f(1.0f, 1.0f);
	float BrushRadiusPixels = 32.0f;
	float BrushRadialFalloffRangePixels = 0.0f;
	float BrushDepthWorld = 1.0f;
	float BrushDepthFalloffRangeWorld = 0.0f;
	float BrushAntialiasWidthPixels = 1.0f;
	float MaxWorldQueryRadius = 1.0f;
	float Strength = 1.0f;
	float FacingThreshold = 0.02f;
	bool bOrthographic = false;
	FLinearColor Color = FLinearColor::White;
};

struct FQuickSDFScreenProjectionStrokePoint
{
	FVector3d WorldPosition = FVector3d::Zero();
	FVector2f ScreenPosition = FVector2f::ZeroVector;
	float ViewDepth = 0.0f;
	float Pressure = 1.0f;
};

struct FQuickSDFScreenProjectionPaintTriangle
{
	FVector2D UVs[3];
	FVector2D PixelPositions[3];
	FVector3d WorldPositions[3];
	FVector3d WorldNormal = FVector3d(0.0, 0.0, 1.0);
};

UCLASS()
class UQuickSDFPaintTool : public UBaseBrushTool
{
	GENERATED_BODY()
	friend class UQuickSDFBrushResizeInputBehavior;
	friend class UQuickSDFPreviewWidget;
	
public:
	UQuickSDFPaintTool();

	virtual void RegisterActions(FInteractiveToolActionSet& ActionSet) override;
	virtual void Setup() override;
	virtual void Shutdown(EToolShutdownType ShutdownType) override;
	virtual void OnTick(float DeltaTime) override;
	
	virtual FInputRayHit CanBeginClickDragSequence(const FInputDeviceRay& PressPos) override;
	virtual void OnClickPress(const FInputDeviceRay& PressPos) override;
	virtual void OnClickDrag(const FInputDeviceRay& DragPos) override;
	virtual void OnClickRelease(const FInputDeviceRay& ReleasePos) override;

	virtual void OnBeginDrag(const FRay& Ray) override;
	virtual void OnUpdateDrag(const FRay& Ray) override;
	virtual void OnEndDrag(const FRay& Ray) override;

	virtual bool HitTest(const FRay& Ray, FHitResult& OutHit) override;
	virtual FInputRayHit BeginHoverSequenceHitTest(const FInputDeviceRay& PressPos) override;
	virtual bool OnUpdateHover(const FInputDeviceRay& DevicePos) override;
	virtual void OnEndHover() override;
	virtual void UpdateBrushStampIndicator() override;
	
	virtual void OnPropertyModified(UObject* PropertySet, FProperty* Property) override;
	virtual void DrawHUD( FCanvas* Canvas, IToolsContextRenderAPI* RenderAPI ) override;
	bool ApplyRenderTargetPixels(int32 AngleIndex, const TArray<FColor>& Pixels);
	bool ApplyRenderTargetPixelsByGuid(const FGuid& AngleGuid, const TArray<FColor>& Pixels);
	bool ApplyRenderTargetPixelsInRect(int32 AngleIndex, const FIntRect& Rect, const TArray<FColor>& Pixels);
	bool ApplyRenderTargetPixelsInRectByGuid(const FGuid& AngleGuid, int32 FallbackIndex, const FIntRect& Rect, const TArray<FColor>& Pixels);
	bool ApplyTextureSlotChange(const FGuid& AngleGuid, int32 FallbackIndex, class UTexture2D* Texture, bool bAllowSourceTextureOverwrite, const TArray<FColor>& Pixels);
	void RestoreMaskStateByGuid(const TArray<FGuid>& MaskGuids, const TArray<float>& Angles, const TArray<float>& AngleOffsetDeltas, const TArray<class UTexture2D*>& Textures, const TArray<bool>& AllowSourceTextureOverwrites, const TArray<TArray<FColor>>& PixelsByMask);
	
	void GenerateSDF();
	void GenerateSDFToFile();
	void ConvertIntermediateSDF();
	void ConvertIntermediateSDF(EQuickSDFThresholdMapOutputMode OutputMode);
	void CreateQuickThresholdMap();
	void ImportEditedMasks();
	bool ImportEditedMasksFromTextures(const TArray<UTexture2D*>& InTextures);
	bool ImportEditedMasksFromTexturesWithAngles(const TArray<UTexture2D*>& InTextures, const TArray<float>& InAngles);
	bool AssignMaskTextureToAngle(int32 AngleIndex, UTexture2D* Texture, bool bAllowSourceTextureOverwrite = false);
	void OverwriteSourceTextures();
	void RequestImportPanel();
	bool ConsumeImportPanelRequest();
	void SaveQuickSDFAsset();
	void ValidateMonotonicGuard();
	bool CaptureRenderTargetPixels(class UTextureRenderTarget2D* RenderTarget, TArray<FColor>& OutPixels) const;
	class UTextureRenderTarget2D* GetActiveRenderTarget() const;
	void EnsureInitialMasksReady();
	void RebakeCurrentMask();
	void RebakeAllMasks();
	void BakeSelectedTextureSet();
	void GenerateSelectedTextureSetSDF();
	void CompleteToEightMasks();
	void RedistributeAnglesEvenly();
	void FillMaskColor(bool bFillAllAngles, const FLinearColor& FillColor);
	void MarkMasksChanged();
	void RequestBrushResizeMode();
	bool IsBrushResizeModeActive() const { return bAdjustingBrushRadius; }
	void ConfirmBrushResizeMode();
	void CancelBrushResizeMode();
	int32 GetMaskRevision() const { return MaskRevision; }
	bool SelectTextureSet(int32 TextureSetIndex);
	void RefreshTextureSetsForCurrentComponent();
	class UMeshComponent* GetCurrentComponent() const { return CurrentComponent.Get(); }
	bool TryGetBrushFocusTarget(FVector& OutWorldPosition, float& OutWorldRadius) const;
	FText GetActiveTextureSetLabel() const;
	FText GetTextureSetStatusText(int32 TextureSetIndex) const;
	FText GetTextureSetStatusTooltip(int32 TextureSetIndex) const;
	FLinearColor GetTextureSetStatusColor(int32 TextureSetIndex) const;
	class UTexture2D* GetActiveFinalSDFTexture() const;
	bool CanUseGeneratedSDFPreview() const;
	bool CanUseLiveSDFPreview() const;
	void RefreshTimelinePreviewMaterial();
	FText GetGeneratedSDFPreviewUnavailableText() const;
	FText GetMaterialPreviewStatusText() const;
	FQuickSDFAutoSymmetryResult ResolveEffectiveSymmetryMode(bool bAllowExpensiveAnalysis = true);
	
	void AddKeyframe();
	void AddKeyframeAtAngle(float Angle);
	void DuplicateKeyframeAtAngle(float Angle);
	void RemoveKeyframe(int32 Index);
	
	void FillOriginalShading(int32 AngleIndex);
	void FillOriginalShadingAll();
	
	UPROPERTY(Transient)
	TObjectPtr<UQuickSDFToolProperties> Properties;

protected:
	void GenerateSDFInternal(bool bSaveAsset, bool bPromptForFileExport);
	void BuildBrushMaskTexture();
	void RefreshPreviewMaterial();
	void ClearPreviewMaterialDirtyState() const;
	FQuickSDFStrokeSample SmoothStrokeSample(const FQuickSDFStrokeSample& RawSample);
	void ChangeTargetComponent(class UMeshComponent* NewComponent);
	void RestoreOriginalComponentMaterials();
	void RestoreOriginalComponentMaterialSlots();
	void RestoreOriginalComponentOverlayMaterial();
	void RestoreOriginalComponentMaterialSlotOverlayMaterials();
	void ApplyMaterialPreviewMode();
	void UpdatePreviewMaterialParameters(class UMaterialInstanceDynamic* Material);
	void UpdateGeneratedSDFMaterialParameters();
	void ShowGeneratedSDFPreviewNotification(EQuickSDFMaterialPreviewMode PreviousMode, class UTexture2D* FinalTexture);
	void ShowTextureSetWarningNotification(const FQuickSDFTextureSetData& TextureSet);
	void MarkLiveSDFPreviewDirty();
	void UpdateLiveSDFPreview();
	bool QueueLiveSDFPreviewRender();
	void OnLiveSDFPreviewRenderComplete(int32 CompletedRevision);
	FIntPoint GetLiveSDFPreviewSize() const;
	void ResetLiveSDFPreviewState();
	void ApplyTargetMaterialSlotIsolation();
	bool IsTriangleInTargetMaterialSlot(int32 TriangleID) const;
	bool TryMakeStrokeSample(const FRay& Ray, FQuickSDFStrokeSample& OutSample);
	bool TryMakePreviewStrokeSample(const FVector2D& ScreenPosition, FQuickSDFStrokeSample& OutSample) const;
	EQuickSDFMeshPaintMode GetMeshPaintMode() const;
	bool ShouldUseSurfaceSpacePaint() const;
	bool ShouldUseProjectedSurfacePaint() const;
	bool ShouldUseScreenProjectionPaint() const;
	bool ShouldUseAnySurfaceProjectionPaint() const;
	float GetScreenProjectionBrushRadiusPixels() const;
	bool CanInterpolateStrokeSamples(const FQuickSDFStrokeSample& A, const FQuickSDFStrokeSample& B) const;
	void StampSample(const FQuickSDFStrokeSample& Sample);
	void StampSamples(const TArray<FQuickSDFStrokeSample>& Samples);
	bool BuildSurfaceBrushParams(const FQuickSDFStrokeSample& Sample, class UTextureRenderTarget2D* RenderTarget, FQuickSDFSurfaceBrushParams& OutParams) const;
	bool GatherSurfacePaintTriangles(const FQuickSDFSurfaceBrushParams& BrushParams, class UTextureRenderTarget2D* RenderTarget, TArray<FQuickSDFSurfacePaintTriangle>& OutTriangles, FIntRect& OutDirtyRect);
	bool PaintSurfaceBrushToRenderTarget(class UTextureRenderTarget2D* RenderTarget, const FQuickSDFStrokeSample& Sample, FIntRect* OutDirtyRect);
	bool BuildSurfaceLineBrushParams(const FQuickSDFStrokeSample& StartSample, const FQuickSDFStrokeSample& EndSample, class UTextureRenderTarget2D* RenderTarget, FQuickSDFSurfaceBrushParams& OutParams) const;
	bool GatherSurfaceLinePaintTriangles(const FQuickSDFSurfaceBrushParams& BrushParams, class UTextureRenderTarget2D* RenderTarget, TArray<FQuickSDFSurfacePaintTriangle>& OutTriangles, FIntRect& OutDirtyRect);
	bool GatherSurfacePolylinePaintTriangles(const TArray<FQuickSDFStrokeSample>& Samples, const FQuickSDFSurfaceBrushParams& BrushParams, class UTextureRenderTarget2D* RenderTarget, TArray<FQuickSDFSurfacePaintTriangle>& OutTriangles, FIntRect& OutDirtyRect);
	bool PaintSurfaceLineToRenderTarget(class UTextureRenderTarget2D* RenderTarget, const FQuickSDFStrokeSample& StartSample, const FQuickSDFStrokeSample& EndSample, FIntRect* OutDirtyRect);
	bool PaintSurfaceLineSegmentsToRenderTarget(class UTextureRenderTarget2D* RenderTarget, const TArray<FQuickSDFStrokeSample>& Samples, FIntRect* OutDirtyRect);
	bool PaintSurfaceBrushesToRenderTarget(class UTextureRenderTarget2D* RenderTarget, const TArray<FQuickSDFStrokeSample>& Samples, FIntRect* OutDirtyRect);
	bool PaintSurfacePolylineToRenderTarget(class UTextureRenderTarget2D* RenderTarget, const TArray<FQuickSDFStrokeSample>& Samples, FIntRect* OutDirtyRect);
	bool PaintUVBrushesToRenderTarget(class UTextureRenderTarget2D* RenderTarget, const TArray<FQuickSDFStrokeSample>& Samples, const TArray<FVector2D>& PixelSizes, FIntRect* OutDirtyRect);
	bool BuildProjectedPaintParams(const TArray<FQuickSDFStrokeSample>& Samples, class UTextureRenderTarget2D* RenderTarget, FQuickSDFProjectedPaintParams& OutParams, TArray<FQuickSDFProjectedStrokePoint>& OutStrokePoints) const;
	bool BuildProjectedStrokePoints(const TArray<FQuickSDFStrokeSample>& Samples, const FQuickSDFProjectedPaintParams& PaintParams, TArray<FQuickSDFProjectedStrokePoint>& OutStrokePoints) const;
	bool GatherProjectedPaintTriangles(const TArray<FQuickSDFStrokeSample>& Samples, const FQuickSDFProjectedPaintParams& PaintParams, class UTextureRenderTarget2D* RenderTarget, TArray<FQuickSDFProjectedPaintTriangle>& OutTriangles, FIntRect& OutDirtyRect);
	class UTextureRenderTarget2D* GetOrCreateProjectedPaintCoverageRenderTarget(int32 TargetWidth, int32 TargetHeight);
	bool PaintProjectedSurfaceStrokeToRenderTarget(class UTextureRenderTarget2D* RenderTarget, const TArray<FQuickSDFStrokeSample>& Samples, FIntRect* OutDirtyRect);
	bool DrawProjectedSurfaceStrokeChunkCoverage(class FCanvas& CoverageCanvas, class UTextureRenderTarget2D* RenderTarget, const TArray<FQuickSDFStrokeSample>& Samples, const FQuickSDFProjectedPaintParams& PaintParams, const TArray<FQuickSDFProjectedStrokePoint>& StrokePoints, int32 CoverageScale, FIntRect* OutDirtyRect);
	bool ResolveProjectedPaintCoverageToRenderTarget(class UTextureRenderTarget2D* RenderTarget, class UTextureRenderTarget2D* CoverageRenderTarget, const FQuickSDFProjectedPaintParams& PaintParams, const FIntRect& DirtyRect, FIntRect* OutDirtyRect);
	bool PaintProjectedSurfaceStrokeChunkToRenderTarget(class UTextureRenderTarget2D* RenderTarget, const TArray<FQuickSDFStrokeSample>& Samples, const FQuickSDFProjectedPaintParams& PaintParams, const TArray<FQuickSDFProjectedStrokePoint>& StrokePoints, FIntRect* OutDirtyRect);
	bool InitializeScreenProjectionPaintFrame(const FQuickSDFStrokeSample& AnchorSample);
	bool BuildScreenProjectionPaintParams(const TArray<FQuickSDFStrokeSample>& Samples, class UTextureRenderTarget2D* RenderTarget, FQuickSDFScreenProjectionPaintParams& OutParams, TArray<FQuickSDFScreenProjectionStrokePoint>& OutStrokePoints);
	bool BuildScreenProjectionStrokePoints(const TArray<FQuickSDFStrokeSample>& Samples, const FQuickSDFScreenProjectionPaintParams& PaintParams, TArray<FQuickSDFScreenProjectionStrokePoint>& OutStrokePoints) const;
	bool ProjectWorldToScreenProjection(const FQuickSDFScreenProjectionPaintParams& PaintParams, const FVector3d& WorldPosition, FVector2f& OutScreenPosition, float* OutViewDepth = nullptr) const;
	bool GatherScreenProjectionPaintTriangles(const TArray<FQuickSDFStrokeSample>& Samples, const FQuickSDFScreenProjectionPaintParams& PaintParams, class UTextureRenderTarget2D* RenderTarget, TArray<FQuickSDFScreenProjectionPaintTriangle>& OutTriangles, FIntRect& OutDirtyRect);
	bool PaintScreenProjectionStrokeToRenderTarget(class UTextureRenderTarget2D* RenderTarget, const TArray<FQuickSDFStrokeSample>& Samples, FIntRect* OutDirtyRect);
	bool DrawScreenProjectionStrokeChunkCoverage(class FCanvas& CoverageCanvas, class UTextureRenderTarget2D* RenderTarget, const TArray<FQuickSDFStrokeSample>& Samples, const FQuickSDFScreenProjectionPaintParams& PaintParams, const TArray<FQuickSDFScreenProjectionStrokePoint>& StrokePoints, int32 CoverageScale, FIntRect* OutDirtyRect);
	bool ResolveScreenProjectionPaintCoverageToRenderTarget(class UTextureRenderTarget2D* RenderTarget, class UTextureRenderTarget2D* CoverageRenderTarget, const FQuickSDFScreenProjectionPaintParams& PaintParams, const FIntRect& DirtyRect, FIntRect* OutDirtyRect);
	bool ProjectSurfaceStrokeSample(const FQuickSDFStrokeSample& Sample, double MaxWorldDistance, FQuickSDFStrokeSample& OutSample);
	void AppendStrokeSample(const FQuickSDFStrokeSample& Sample);
	void StampLinearSegment(const FQuickSDFStrokeSample& StartSample, const FQuickSDFStrokeSample& EndSample);
	void FlushStrokeTail();
	void StampInterpolatedSegment(
		const FQuickSDFStrokeSample& P0,
		const FQuickSDFStrokeSample& P1,
		const FQuickSDFStrokeSample& P2,
		const FQuickSDFStrokeSample& P3);
	FVector2D GetPreviewOrigin() const;
	FVector2D GetPreviewSize() const;
	FVector2D ConvertInputScreenToCanvasSpace(const FVector2D& ScreenPosition) const;
	bool IsInPreviewBounds(const FVector2D& ScreenPosition) const;
	FVector2f ScreenToPreviewUV(const FVector2D& ScreenPosition) const;
	FVector2D GetBrushPixelSize(class UTextureRenderTarget2D* RenderTarget) const;
	double GetCurrentStrokeSpacing(class UTextureRenderTarget2D* RenderTarget) const;
	double GetEffectiveBrushRadius() const;
	FVector2D GetSamplePixelPosition(const FQuickSDFStrokeSample& Sample, class UTextureRenderTarget2D* RenderTarget) const;
	double GetSamplePixelDistance(const FQuickSDFStrokeSample& A, const FQuickSDFStrokeSample& B, class UTextureRenderTarget2D* RenderTarget) const;
	FQuickSDFStrokeSample LerpStrokeSample(const FQuickSDFStrokeSample& A, const FQuickSDFStrokeSample& B, double Alpha) const;
	bool IsPaintingShadow() const;
	TArray<int32> GetPaintTargetAngleIndices() const;
	void BeginBrushResizeMode();
	void UpdateBrushResizeFromCursor();
	void EndBrushResizeMode();
	bool RestoreRenderTargetPixels(class UTextureRenderTarget2D* RenderTarget, const TArray<FColor>& Pixels) const;
	bool RestoreRenderTargetPixelsInRect(class UTextureRenderTarget2D* RenderTarget, const FIntRect& Rect, const TArray<FColor>& Pixels) const;
	bool RestoreRenderTargetTexture(class UTextureRenderTarget2D* RenderTarget, class UTexture2D* Texture) const;
	bool CopyRenderTargetToRenderTarget(class UTextureRenderTarget2D* SourceRenderTarget, class UTextureRenderTarget2D* DestinationRenderTarget) const;
	class UTexture2D* CreateTransientTextureFromPixels(const TArray<FColor>& Pixels, int32 Width, int32 Height) const;
	bool ApplyPixelsWithUndo(int32 AngleIndex, const TArray<FColor>& Pixels, const FText& ChangeDescription);
	bool CopyNearestMaskToAngle(int32 DestinationIndex);
	void AddKeyframeInternal(float RequestedAngle, bool bUseRequestedAngle, const TArray<FColor>* SourcePixels = nullptr, float InitialAngleOffsetDeltaDegrees = 0.0f);
	void SyncPropertiesFromActiveAsset();
	void SyncActiveTextureSetFromProperties();
	void InitializeDefaultAngleData(TArray<FQuickSDFAngleData>& AngleData, bool bResetExisting) const;
	void InvalidateUVOverlayCache();
	class UTextureRenderTarget2D* GetUVOverlayRenderTarget();
	void RebuildUVOverlayRenderTarget(int32 Width, int32 Height);
	void DrawQuickLineHUDPreview(class FCanvas* Canvas);
	void DrawScreenProjectionBrushHUD(class FCanvas* Canvas);
	bool RestoreStrokeStartPixels() const;
	void BeginStrokeTransaction();
	void EndStrokeTransaction();
	bool ApplyMonotonicGuardToStroke(class UQuickSDFAsset* Asset);
	int32 ValidateMonotonicGuardForAsset(class UQuickSDFAsset* Asset, int32* OutTransitionViolations = nullptr) const;
	void WarnIfMonotonicGuardViolations(const FText& Context);
	void ResetStrokeState();
	void InvalidateAutoSymmetryCache();
	void InvalidatePaintChartCache();
	void EnsurePaintChartCache();
	int32 GetPaintChartIDForTriangle(int32 TriangleID);
	void AddStrokeDirtyRect(class UTextureRenderTarget2D* RenderTarget, const FIntRect& Rect);
	void InitializeRenderTargets();
	double GetToolCurrentTime() const;
	void UpdateQuickLineHoldState(const FVector2D& ScreenPosition);
	void TryActivateQuickLine();
	void RedrawQuickLinePreview(bool bForce = false);
	void StampQuickLineSegment(const FQuickSDFStrokeSample& StartSample, const FQuickSDFStrokeSample& EndSample, bool bForce = false);
	void StampQuickLineSurfaceSegment(const FQuickSDFStrokeSample& StartSample, const FQuickSDFStrokeSample& EndSample, bool bForce = false);
	void StampQuickLineResampledSamples(const TArray<FQuickSDFStrokeSample>& CurveSamples);
	FQuickSDFStrokeSample TransformQuickLineSample(
		const FQuickSDFStrokeSample& SourceSample,
		const FQuickSDFStrokeSample& SourceStart,
		const FQuickSDFStrokeSample& SourceEnd,
		const FQuickSDFStrokeSample& TargetStart,
		const FQuickSDFStrokeSample& TargetEnd) const;

	TSharedPtr<UE::Geometry::FDynamicMesh3> TargetMesh;
	TSharedPtr<UE::Geometry::FDynamicMeshAABBTree3> TargetMeshSpatial;
	TMap<int32, int32> TargetTriangleMaterialSlots;
	TMap<int32, int32> TargetTrianglePaintChartIDs;

	TWeakObjectPtr<class UMeshComponent> CurrentComponent;
	TSet<TWeakObjectPtr<class UMeshComponent>> InitialBakeComponents;

	UPROPERTY()
	TArray<UMaterialInterface*> OriginalMaterials;

	UPROPERTY()
	TObjectPtr<UMaterialInterface> OriginalOverlayMaterial;

	UPROPERTY()
	TArray<TObjectPtr<UMaterialInterface>> OriginalMaterialSlotOverlayMaterials;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInterface> PreviewBaseMaterial;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> PreviewMaterial;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> PreviewHUDMaterial;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInterface> PreviewOverlayBaseMaterial;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> PreviewOverlayMaterial;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInterface> SDFToonBaseMaterial;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> SDFToonPreviewMaterial;

	UPROPERTY(Transient)
	TObjectPtr<UTextureRenderTarget2D> LiveSDFPreviewRenderTarget;

	UPROPERTY(Transient)
	class UTexture2D* BrushMaskTexture;

	UPROPERTY(Transient)
	TObjectPtr<UTextureRenderTarget2D> ProjectedPaintCoverageRenderTarget;

	UPROPERTY(Transient)
	TObjectPtr<UQuickSDFBrushResizeInputBehavior> BrushResizeBehavior;

	UPROPERTY(Transient)
	TObjectPtr<UTextureRenderTarget2D> UVOverlayRenderTarget;

	TArray<FQuickSDFStrokeSample> StrokeSamples;
	FQuickSDFScreenProjectionPaintParams ActiveScreenProjectionPaintParams;
	FQuickSDFStrokeSample LastStampedSample;
	FQuickSDFStrokeSample FilteredStrokeSample;
	bool bHasLastStampedSample = false;
	bool bHasActiveScreenProjectionPaintParams = false;
	bool bHasFilteredStrokeSample = false;
	double DistanceSinceLastStamp = 0.0;
	FVector2D LastInputScreenPosition = FVector2D::ZeroVector;
	FVector2D PreviewCanvasOrigin = FVector2D(10.0, 10.0);
	FVector2D PreviewCanvasSize = FVector2D(256.0, 256.0);
	EQuickSDFStrokeInputMode PendingStrokeInputMode = EQuickSDFStrokeInputMode::None;
	EQuickSDFStrokeInputMode ActiveStrokeInputMode = EQuickSDFStrokeInputMode::None;
	bool bAdjustingBrushRadius = false;
	FVector2D BrushResizeStartScreenPosition = FVector2D::ZeroVector;
	FBrushStampData BrushResizeStartStamp;
	float BrushResizeStartRadius = 0.0f;
	float BrushResizeSensitivity = 0.025f;
	bool bBrushResizeHadVisibleStamp = false;
	bool bStrokeTransactionActive = false;
	bool bStampingAllPaintTargets = false;
	bool bUseImportedMasksForQuickCreate = false;
	bool bUVOverlayDirty = true;
	bool bQuickLineActive = false;
	bool bHasQuickLineStartSample = false;
	bool bHasQuickLineEndSample = false;
	bool bSuppressMaskPixelUndo = false;
	bool bImportPanelRequested = false;
	bool bHasOriginalOverlayMaterialState = false;
	bool bHasOriginalMaterialSlotOverlayMaterialState = false;
	float OriginalOverlayMaterialMaxDrawDistance = 0.0f;
	int32 MaskRevision = 0;
	int32 LiveSDFSourceRevision = 0;
	int32 LiveSDFPreviewRevision = INDEX_NONE;
	int32 LiveSDFPreviewRequestedRevision = INDEX_NONE;
	bool bLiveSDFPreviewDirty = true;
	bool bLiveSDFPreviewRenderPending = false;
	double LastLiveSDFPreviewRequestTime = -1000.0;
	int32 CachedUVOverlayUVChannel = INDEX_NONE;
	int32 CachedUVOverlayMaterialSlot = INDEX_NONE;
	bool bCachedUVOverlayIsolateTargetMaterialSlot = false;
	FIntPoint CachedUVOverlaySize = FIntPoint::ZeroValue;
	bool bPaintChartCacheDirty = true;
	int32 CachedPaintChartUVChannel = INDEX_NONE;
	int32 CachedPaintChartMaterialSlot = INDEX_NONE;
	FQuickSDFAutoSymmetryResult CachedAutoSymmetryResult;
	bool bCachedAutoSymmetryResultValid = false;
	int32 CachedAutoSymmetryUVChannel = INDEX_NONE;
	int32 CachedAutoSymmetryMaterialSlot = INDEX_NONE;
	bool bCachedAutoSymmetryHasTargetMesh = false;
	int32 StrokeTransactionAngleIndex = INDEX_NONE;
	TArray<int32> StrokeTransactionAngleIndices;
	TArray<FIntRect> StrokeDirtyRectsByAngle;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UTextureRenderTarget2D>> StrokeBeforeRenderTargetsByAngle;
	FIntRect StrokeDirtyRect;
	bool bHasStrokeDirtyRect = false;

	TArray<FQuickSDFStrokeSample> QuickLineSourceSamples;
	FQuickSDFStrokeSample QuickLineStartSample;
	FQuickSDFStrokeSample QuickLineEndSample;
	FQuickSDFStrokeSample LastRawStrokeSample;
	FVector2D QuickLineHoldScreenPosition = FVector2D::ZeroVector;
	double QuickLineLastMoveTime = 0.0;
	bool bHasLastRawStrokeSample = false;
	
	FVector2D FilteredScreenPosition = FVector2D::ZeroVector;
	
private:
	TArray<FQuickSDFStrokeSample> PointBuffer;
	double AccumulatedDistance = 0.0;
	FOneEuroFilter WorldPosFilter;
	FOneEuroFilter UVFilter;
	FVector2D BrushResizeStartAbsolutePosition = FVector2D::ZeroVector;
};
