#pragma once

#include "CoreMinimal.h"
#include "BaseTools/BaseBrushTool.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "DynamicMesh/DynamicMeshAABBTree3.h"
#include "InteractiveToolBuilder.h"
#include "QuickSDFPaintTool.generated.h"

struct FQuickSDFStrokeSample
{
	FVector3d WorldPos = FVector3d(0.0, 0.0, 0.0);
	FVector2f UV = FVector2f::ZeroVector;
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

	UPROPERTY(EditAnywhere, Category = "Paint Settings")
	bool bPaintShadow = true;

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

public:
	UQuickSDFPaintTool();

	virtual void Setup() override;
	virtual void Shutdown(EToolShutdownType ShutdownType) override;
	virtual FInputRayHit CanBeginClickDragSequence(const FInputDeviceRay& PressPos) override;

	virtual void OnBeginDrag(const FRay& Ray) override;
	virtual void OnUpdateDrag(const FRay& Ray) override;
	virtual void OnEndDrag(const FRay& Ray) override;

	virtual bool HitTest(const FRay& Ray, FHitResult& OutHit) override;
	
	virtual void OnPropertyModified(UObject* PropertySet, FProperty* Property) override;
	virtual void DrawHUD( FCanvas* Canvas, IToolsContextRenderAPI* RenderAPI ) override;
protected:
	UPROPERTY(Transient)
	TObjectPtr<UQuickSDFToolProperties> Properties;

	void ChangeTargetComponent(class UPrimitiveComponent* NewComponent);
	bool TryMakeStrokeSample(const FRay& Ray, FQuickSDFStrokeSample& OutSample);
	void StampSample(const FQuickSDFStrokeSample& Sample);
	void AppendStrokeSample(const FQuickSDFStrokeSample& Sample);
	void StampInterpolatedSegment(
		const FQuickSDFStrokeSample& P0,
		const FQuickSDFStrokeSample& P1,
		const FQuickSDFStrokeSample& P2,
		const FQuickSDFStrokeSample& P3);
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

	TArray<FQuickSDFStrokeSample> StrokeSamples;
	FQuickSDFStrokeSample LastStampedSample;
	bool bHasLastStampedSample = false;
	double DistanceSinceLastStamp = 0.0;
};
