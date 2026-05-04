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
	void RestoreMaskStateByGuid(const TArray<FGuid>& MaskGuids, const TArray<float>& Angles, const TArray<class UTexture2D*>& Textures, const TArray<bool>& AllowSourceTextureOverwrites, const TArray<TArray<FColor>>& PixelsByMask);
	
	void GenerateSDF();
	void GenerateSDFToFile();
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
	int32 GetMaskRevision() const { return MaskRevision; }
	bool SelectTextureSet(int32 TextureSetIndex);
	void RefreshTextureSetsForCurrentComponent();
	class UMeshComponent* GetCurrentComponent() const { return CurrentComponent.Get(); }
	FText GetActiveTextureSetLabel() const;
	FText GetTextureSetStatusText(int32 TextureSetIndex) const;
	FText GetTextureSetStatusTooltip(int32 TextureSetIndex) const;
	FLinearColor GetTextureSetStatusColor(int32 TextureSetIndex) const;
	class UTexture2D* GetActiveFinalSDFTexture() const;
	bool CanUseGeneratedSDFPreview() const;
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
	void ApplyTargetMaterialSlotIsolation();
	bool IsTriangleInTargetMaterialSlot(int32 TriangleID) const;
	bool TryMakeStrokeSample(const FRay& Ray, FQuickSDFStrokeSample& OutSample);
	bool TryMakePreviewStrokeSample(const FVector2D& ScreenPosition, FQuickSDFStrokeSample& OutSample) const;
	bool CanInterpolateStrokeSamples(const FQuickSDFStrokeSample& A, const FQuickSDFStrokeSample& B) const;
	void StampSample(const FQuickSDFStrokeSample& Sample);
	void StampSamples(const TArray<FQuickSDFStrokeSample>& Samples);
	void StampProjectedSamples(const TArray<FQuickSDFStrokeSample>& Samples);
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
	void AddKeyframeInternal(float RequestedAngle, bool bUseRequestedAngle, const TArray<FColor>* SourcePixels = nullptr);
	void SyncPropertiesFromActiveAsset();
	void SyncActiveTextureSetFromProperties();
	void InitializeDefaultAngleData(TArray<FQuickSDFAngleData>& AngleData, bool bResetExisting) const;
	void InvalidateUVOverlayCache();
	class UTextureRenderTarget2D* GetUVOverlayRenderTarget();
	void RebuildUVOverlayRenderTarget(int32 Width, int32 Height);
	void DrawQuickLineHUDPreview(class FCanvas* Canvas);
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
	void StampQuickLineSegment(const FQuickSDFStrokeSample& StartSample, const FQuickSDFStrokeSample& EndSample);
	void StampQuickLineSurfaceSegment(const FQuickSDFStrokeSample& StartSample, const FQuickSDFStrokeSample& EndSample);
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
	class UTexture2D* BrushMaskTexture;

	UPROPERTY(Transient)
	TObjectPtr<UQuickSDFBrushResizeInputBehavior> BrushResizeBehavior;

	UPROPERTY(Transient)
	TObjectPtr<UTextureRenderTarget2D> UVOverlayRenderTarget;

	TArray<FQuickSDFStrokeSample> StrokeSamples;
	FQuickSDFStrokeSample LastStampedSample;
	FQuickSDFStrokeSample FilteredStrokeSample;
	bool bHasLastStampedSample = false;
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
	float BrushResizeSensitivity = 0.1f;
	bool bBrushResizeTransactionOpen = false;
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
