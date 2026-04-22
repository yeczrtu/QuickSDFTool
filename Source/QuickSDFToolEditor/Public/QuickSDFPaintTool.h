#pragma once

#include "CoreMinimal.h"
#include "BaseBehaviors/AnyButtonInputBehavior.h"
#include "BaseTools/BaseBrushTool.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "DynamicMesh/DynamicMeshAABBTree3.h"
#include "InteractiveToolBuilder.h"
#include "InteractiveToolActionSet.h"
#include "QuickSDFPaintTool.generated.h"

struct FQuickSDFStrokeSample
{
	FVector3d WorldPos = FVector3d(0.0, 0.0, 0.0);
	FVector2f UV = FVector2f::ZeroVector;
};

enum class EQuickSDFStrokeInputMode : uint8
{
	None,
	MeshSurface,
	TexturePreview
};

UCLASS()
class UQuickSDFBrushResizeInputBehavior : public UAnyButtonInputBehavior
{
	GENERATED_BODY()

public:
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
	UPROPERTY(EditAnywhere, Category = "Paint Settings")
	int32 EditAngleIndex = 0;

	UPROPERTY(EditAnywhere, Category = "Paint Settings")
	int32 NumAngles = 8;

	UPROPERTY(EditAnywhere, Category = "Paint Settings")
	int32 UVChannel = 0;

	UPROPERTY(EditAnywhere, Category = "Target Settings")
	FIntPoint Resolution = FIntPoint(1024, 1024);

	UPROPERTY(Transient)
	TArray<class UTextureRenderTarget2D*> TransientRenderTargets;

	UFUNCTION(CallInEditor, Category = "Actions")
	void RotateLight90Deg();

	UFUNCTION(CallInEditor, Category = "Actions")
	void ExportToTexture();
};

UCLASS()
class UQuickSDFPaintTool : public UBaseBrushTool
{
	GENERATED_BODY()
	friend class UQuickSDFBrushResizeInputBehavior;

public:
	UQuickSDFPaintTool();

	virtual void RegisterActions(FInteractiveToolActionSet& ActionSet) override;
	virtual void Setup() override;
	virtual void Shutdown(EToolShutdownType ShutdownType) override;
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
protected:
	UPROPERTY(Transient)
	TObjectPtr<UQuickSDFToolProperties> Properties;

	void BuildBrushMaskTexture();
	void RefreshPreviewMaterial();
	void ChangeTargetComponent(class UPrimitiveComponent* NewComponent);
	bool TryMakeStrokeSample(const FRay& Ray, FQuickSDFStrokeSample& OutSample);
	bool TryMakePreviewStrokeSample(const FVector2D& ScreenPosition, FQuickSDFStrokeSample& OutSample) const;
	void StampSample(const FQuickSDFStrokeSample& Sample);
	void AppendStrokeSample(const FQuickSDFStrokeSample& Sample);
	void StampInterpolatedSegment(
		const FQuickSDFStrokeSample& P0,
		const FQuickSDFStrokeSample& P1,
		const FQuickSDFStrokeSample& P2,
		const FQuickSDFStrokeSample& P3);
	class UTextureRenderTarget2D* GetActiveRenderTarget() const;
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
	bool CaptureRenderTargetPixels(class UTextureRenderTarget2D* RenderTarget, TArray<FColor>& OutPixels) const;
	bool RestoreRenderTargetPixels(class UTextureRenderTarget2D* RenderTarget, const TArray<FColor>& Pixels) const;
	void BeginStrokeTransaction();
	void EndStrokeTransaction();
	void ResetStrokeState();
	void InitializeRenderTargets();
	
	UPROPERTY(Transient)
	class UPrimitiveComponent* CurrentComponent;

	TSharedPtr<UE::Geometry::FDynamicMesh3> TargetMesh;
	TSharedPtr<UE::Geometry::FDynamicMeshAABBTree3> TargetMeshSpatial;

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
	bool bHasLastStampedSample = false;
	double DistanceSinceLastStamp = 0.0;
	FVector2D LastInputScreenPosition = FVector2D::ZeroVector;
	FVector2D PreviewCanvasOrigin = FVector2D(10.0, 10.0);
	FVector2D PreviewCanvasSize = FVector2D(256.0, 256.0);
	EQuickSDFStrokeInputMode PendingStrokeInputMode = EQuickSDFStrokeInputMode::None;
	EQuickSDFStrokeInputMode ActiveStrokeInputMode = EQuickSDFStrokeInputMode::None;
	bool bAdjustingBrushRadius = false;
	FVector2D BrushResizeStartScreenPosition = FVector2D::ZeroVector;
	float BrushResizeStartRadius = 0.0f;
	bool bBrushResizeTransactionOpen = false;
	bool bStrokeTransactionActive = false;
	int32 StrokeTransactionAngleIndex = INDEX_NONE;
	TArray<FColor> StrokeBeforePixels;
};
