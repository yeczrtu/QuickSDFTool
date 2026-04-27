#pragma once

#include "CoreMinimal.h"
#include "BaseBehaviors/AnyButtonInputBehavior.h"
#include "BaseTools/BaseBrushTool.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "DynamicMesh/DynamicMeshAABBTree3.h"
#include "InteractiveToolBuilder.h"
#include "InteractiveToolActionSet.h"
#include "QuickSDFToolTypes.h"
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

UCLASS()
class UQuickSDFToolProperties : public UInteractiveToolPropertySet
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, Category = "Asset Settings")
	class UQuickSDFAsset* TargetAsset = nullptr;

	UPROPERTY(EditAnywhere, Category = "Paint Settings")
	int32 EditAngleIndex = 0;

	UPROPERTY(EditAnywhere, Category = "Paint Settings", meta=(ClampMin="1", UIMin="1"))
	int32 NumAngles = 8;

	UPROPERTY(EditAnywhere, Category = "Paint Settings")
	int32 UVChannel = 0;
	
	UPROPERTY(EditAnywhere, Category = "Paint Settings")
	bool bShowPreview = true;
	
	UPROPERTY(EditAnywhere, Category = "Paint Settings")
	bool bOverlayOriginalShadow = false;
	
	UPROPERTY(EditAnywhere, Category = "Paint Settings")
	bool bOverlayUV = true;

	UPROPERTY(EditAnywhere, Category = "Paint Settings")
	bool bAutoSyncLight = true;

	UPROPERTY(EditAnywhere, Category = "Paint Settings")
	bool bEnableOnionSkin = false;
	
	UPROPERTY(EditAnywhere, Category = "Paint Settings")
	bool bSymmetryMode = true;
	
	UPROPERTY(EditAnywhere, Category = "Paint Settings", meta = (UIMin = "0.0", UIMax = "180.0"))
	TArray<float> TargetAngles;
	
	UPROPERTY(EditAnywhere, Category = "Paint Settings")
	TArray<class UTexture2D*> TargetTextures;

	UPROPERTY(EditAnywhere, Category = "Target Settings")
	FIntPoint Resolution = FIntPoint(1024, 1024);
	
	UPROPERTY(EditAnywhere, Category = "Export Settings", meta=(ClampMin="1", UIMin="1", ClampMax="8", UIMax="8"))
	int32 UpscaleFactor = 1;

	UFUNCTION(CallInEditor, Category = "Actions")
	void ExportToTexture();

	UFUNCTION(CallInEditor, Category = "Actions")
	void GenerateSDFThresholdMap();
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
	virtual bool OnUpdateHover(const FInputDeviceRay& DevicePos) override;
	
	virtual void OnPropertyModified(UObject* PropertySet, FProperty* Property) override;
	virtual void DrawHUD( FCanvas* Canvas, IToolsContextRenderAPI* RenderAPI ) override;
	bool ApplyRenderTargetPixels(int32 AngleIndex, const TArray<FColor>& Pixels);
	
	void GenerateSDF();
	bool CaptureRenderTargetPixels(class UTextureRenderTarget2D* RenderTarget, TArray<FColor>& OutPixels) const;
	class UTextureRenderTarget2D* GetActiveRenderTarget() const;
	
	void AddKeyframe();
	void RemoveKeyframe(int32 Index);
	
	UPROPERTY(Transient)
	TObjectPtr<UQuickSDFToolProperties> Properties;

protected:
	void BuildBrushMaskTexture();
	void RefreshPreviewMaterial();
	FQuickSDFStrokeSample SmoothStrokeSample(const FQuickSDFStrokeSample& RawSample);
	void ChangeTargetComponent(class UMeshComponent* NewComponent);
	bool TryMakeStrokeSample(const FRay& Ray, FQuickSDFStrokeSample& OutSample);
	bool TryMakePreviewStrokeSample(const FVector2D& ScreenPosition, FQuickSDFStrokeSample& OutSample) const;
	void StampSample(const FQuickSDFStrokeSample& Sample);
	void StampSamples(const TArray<FQuickSDFStrokeSample>& Samples);
	void AppendStrokeSample(const FQuickSDFStrokeSample& Sample);
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
	bool IsPaintingShadow() const;
	void BeginBrushResizeMode();
	void UpdateBrushResizeFromCursor();
	void EndBrushResizeMode();
	bool RestoreRenderTargetPixels(class UTextureRenderTarget2D* RenderTarget, const TArray<FColor>& Pixels) const;
	void BeginStrokeTransaction();
	void EndStrokeTransaction();
	void ResetStrokeState();
	void InitializeRenderTargets();

	TSharedPtr<UE::Geometry::FDynamicMesh3> TargetMesh;
	TSharedPtr<UE::Geometry::FDynamicMeshAABBTree3> TargetMeshSpatial;

	TWeakObjectPtr<class UMeshComponent> CurrentComponent;

	UPROPERTY()
	TArray<UMaterialInterface*> OriginalMaterials;

	UPROPERTY()
	UMaterialInstanceDynamic* PreviewMaterial;

	UPROPERTY(Transient)
	class UTexture2D* BrushMaskTexture;

	UPROPERTY(Transient)
	TObjectPtr<UQuickSDFBrushResizeInputBehavior> BrushResizeBehavior;

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
	float BrushResizeStartRadius = 0.0f;
	float BrushResizeSensitivity = 0.1f;
	bool bBrushResizeTransactionOpen = false;
	bool bStrokeTransactionActive = false;
	int32 StrokeTransactionAngleIndex = INDEX_NONE;
	TArray<FColor> StrokeBeforePixels;
	
	UPROPERTY(EditAnywhere, Category = "Brush Feel")
	float StabilizerAmount = 0.2f;
	
	UPROPERTY(EditAnywhere, Category = "Brush Feel")
	float LazyRadius = 5.0f;
	
	FVector2D FilteredScreenPosition = FVector2D::ZeroVector;
	
private:
	TArray<FQuickSDFStrokeSample> PointBuffer;
	double AccumulatedDistance = 0.0;
	FOneEuroFilter WorldPosFilter;
	FOneEuroFilter UVFilter;
	FVector2D BrushResizeStartAbsolutePosition;
};