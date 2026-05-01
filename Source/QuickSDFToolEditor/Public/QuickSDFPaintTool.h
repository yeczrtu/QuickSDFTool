#pragma once

#include "CoreMinimal.h"
#include "BaseBehaviors/AnyButtonInputBehavior.h"
#include "BaseTools/BaseBrushTool.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "DynamicMesh/DynamicMeshAABBTree3.h"
#include "InteractiveToolBuilder.h"
#include "InteractiveToolActionSet.h"
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
	friend class SQuickSDFTimeline;
	
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
	bool ApplyTextureSlotChange(const FGuid& AngleGuid, int32 FallbackIndex, class UTexture2D* Texture, const TArray<FColor>& Pixels);
	void RestoreMaskStateByGuid(const TArray<FGuid>& MaskGuids, const TArray<float>& Angles, const TArray<class UTexture2D*>& Textures, const TArray<TArray<FColor>>& PixelsByMask);
	
	void GenerateSDF();
	void CreateQuickThresholdMap();
	void ImportEditedMasks();
	bool ImportEditedMasksFromTextures(const TArray<UTexture2D*>& InTextures);
	void SaveQuickSDFAsset();
	bool CaptureRenderTargetPixels(class UTextureRenderTarget2D* RenderTarget, TArray<FColor>& OutPixels) const;
	class UTextureRenderTarget2D* GetActiveRenderTarget() const;
	void EnsureInitialMasksReady();
	void RebakeCurrentMask();
	void RebakeAllMasks();
	void CompleteToEightMasks();
	void RedistributeAnglesEvenly();
	void FillMaskColor(bool bFillAllAngles, const FLinearColor& FillColor);
	void MarkMasksChanged();
	int32 GetMaskRevision() const { return MaskRevision; }
	
	void AddKeyframe();
	void AddKeyframeAtAngle(float Angle);
	void DuplicateKeyframeAtAngle(float Angle);
	void RemoveKeyframe(int32 Index);
	
	void FillOriginalShading(int32 AngleIndex);
	void FillOriginalShadingAll();
	
	UPROPERTY(Transient)
	TObjectPtr<UQuickSDFToolProperties> Properties;

protected:
	void BuildBrushMaskTexture();
	void RefreshPreviewMaterial();
	FQuickSDFStrokeSample SmoothStrokeSample(const FQuickSDFStrokeSample& RawSample);
	void ChangeTargetComponent(class UMeshComponent* NewComponent);
	void ApplyTargetMaterialSlotIsolation();
	bool IsTriangleInTargetMaterialSlot(int32 TriangleID) const;
	bool TryMakeStrokeSample(const FRay& Ray, FQuickSDFStrokeSample& OutSample);
	bool TryMakePreviewStrokeSample(const FVector2D& ScreenPosition, FQuickSDFStrokeSample& OutSample) const;
	void StampSample(const FQuickSDFStrokeSample& Sample);
	void StampSamples(const TArray<FQuickSDFStrokeSample>& Samples);
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
	bool RestoreRenderTargetTexture(class UTextureRenderTarget2D* RenderTarget, class UTexture2D* Texture) const;
	class UTexture2D* CreateTransientTextureFromPixels(const TArray<FColor>& Pixels, int32 Width, int32 Height) const;
	bool ApplyPixelsWithUndo(int32 AngleIndex, const TArray<FColor>& Pixels, const FText& ChangeDescription);
	bool CopyNearestMaskToAngle(int32 DestinationIndex);
	void AddKeyframeInternal(float RequestedAngle, bool bUseRequestedAngle, const TArray<FColor>* SourcePixels = nullptr);
	void SyncPropertiesFromActiveAsset();
	void InvalidateUVOverlayCache();
	class UTextureRenderTarget2D* GetUVOverlayRenderTarget();
	void RebuildUVOverlayRenderTarget(int32 Width, int32 Height);
	bool RestoreStrokeStartPixels() const;
	void BeginStrokeTransaction();
	void EndStrokeTransaction();
	void ResetStrokeState();
	void InitializeRenderTargets();
	double GetToolCurrentTime() const;
	void UpdateQuickLineHoldState(const FVector2D& ScreenPosition);
	void TryActivateQuickLine();
	void RedrawQuickLinePreview();
	void StampQuickLineSegment(const FQuickSDFStrokeSample& StartSample, const FQuickSDFStrokeSample& EndSample);
	FQuickSDFStrokeSample TransformQuickLineSample(const FQuickSDFStrokeSample& SourceSample) const;

	TSharedPtr<UE::Geometry::FDynamicMesh3> TargetMesh;
	TSharedPtr<UE::Geometry::FDynamicMeshAABBTree3> TargetMeshSpatial;
	TMap<int32, int32> TargetTriangleMaterialSlots;

	TWeakObjectPtr<class UMeshComponent> CurrentComponent;
	TSet<TWeakObjectPtr<class UMeshComponent>> InitialBakeComponents;

	UPROPERTY()
	TArray<UMaterialInterface*> OriginalMaterials;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInterface> PreviewBaseMaterial;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> PreviewMaterial;

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
	int32 MaskRevision = 0;
	int32 CachedUVOverlayUVChannel = INDEX_NONE;
	int32 CachedUVOverlayMaterialSlot = INDEX_NONE;
	bool bCachedUVOverlayIsolateTargetMaterialSlot = false;
	FIntPoint CachedUVOverlaySize = FIntPoint::ZeroValue;
	int32 StrokeTransactionAngleIndex = INDEX_NONE;
	TArray<int32> StrokeTransactionAngleIndices;
	TArray<FColor> StrokeBeforePixels;
	TArray<TArray<FColor>> StrokeBeforePixelsByAngle;

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
