#include "QuickSDFPaintTool.h"
#include "QuickSDFPaintToolPrivate.h"
#include "QuickSDFMeshComponentAdapter.h"
#include "QuickSDFToolSubsystem.h"
#include "QuickSDFAsset.h"
#include "SDFProcessor.h"
#include "BaseGizmos/BrushStampIndicator.h"
#include "InteractiveToolManager.h"
#include "ToolBuilderUtil.h"
#include "EngineUtils.h"
#include "Engine/DirectionalLight.h"
#include "CollisionQueryParams.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/Texture2D.h"
#include "BaseBehaviors/ClickDragBehavior.h"
#include "TargetInterfaces/PrimitiveComponentBackedTarget.h"
#include "TargetInterfaces/MeshDescriptionProvider.h"
#include "DynamicMesh/MeshTransforms.h"
#include "DynamicMesh/DynamicMeshAttributeSet.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Components/PrimitiveComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/SkinnedMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Kismet/KismetRenderingLibrary.h"
#include "Intersection/IntrRay3Triangle3.h"
#include "Spatial/SpatialInterfaces.h"
#include "IndexTypes.h"
#include "Engine/Canvas.h"
#include "CanvasItem.h"
#include "CanvasTypes.h"
#include "TextureResource.h"
#include "RenderResource.h"
#include "Math/UnrealMathUtility.h"
#include "InputCoreTypes.h"
#include "HAL/PlatformApplicationMisc.h"
#include "HAL/PlatformTime.h"
#include "InteractiveToolChange.h"
#include "Misc/ScopedSlowTask.h"
#include "Misc/MessageDialog.h"
#include "DesktopPlatformModule.h"
#include "Engine/Selection.h"
#include "Framework/Application/SlateApplication.h"
#include "IDesktopPlatform.h"
#include "Misc/DefaultValueHelper.h"
#include "Containers/Ticker.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetToolsModule.h"
#include "IAssetTools.h"
#include "Misc/PackageName.h"
#include "ObjectTools.h"

#if WITH_EDITOR
#include "Editor.h"
#include "IMaterialBakingModule.h"
#include "MaterialBakingStructures.h"
#endif

#define LOCTEXT_NAMESPACE "QuickSDFPaintTool"

using namespace QuickSDFPaintToolPrivate;

FQuickSDFStrokeSample UQuickSDFPaintTool::SmoothStrokeSample(const FQuickSDFStrokeSample& RawSample)
{
#if 0//todo impliment one euro filter
	double MinCutoff = 1.0;//FMath::Lerp(5.0, 0.1, FMath::Clamp(StabilizerAmount, 0.0f, 1.0f));
	WorldPosFilter.MinCutoff = MinCutoff;
	UVFilter.MinCutoff = MinCutoff;

	double DeltaTime = GetToolManager()->GetContextQueriesAPI()->GetCurrentEditingWorld()->GetDeltaSeconds();
	if (DeltaTime <= 0.0) DeltaTime = 1.0/60.0;

	FQuickSDFStrokeSample FilteredSample = RawSample;

	FilteredSample.WorldPos = WorldPosFilter.Update(RawSample.WorldPos, DeltaTime);

	FVector3d UVAsVector(RawSample.UV.X, RawSample.UV.Y, 0.0);
	FVector3d FilteredUV = UVFilter.Update(UVAsVector, DeltaTime);
	FilteredSample.UV = FVector2f(FilteredUV.X, FilteredUV.Y);

	return FilteredSample;
#else
	return RawSample;
#endif
}

FVector2D UQuickSDFPaintTool::GetPreviewOrigin() const { return PreviewCanvasOrigin; }
FVector2D UQuickSDFPaintTool::GetPreviewSize() const { return PreviewCanvasSize; }

FVector2D UQuickSDFPaintTool::ConvertInputScreenToCanvasSpace(const FVector2D& ScreenPosition) const
{
	const float DPIScale = FMath::Max(FPlatformApplicationMisc::GetDPIScaleFactorAtPoint(ScreenPosition.X, ScreenPosition.Y), 1.0f);
	return ScreenPosition / DPIScale;
}

bool UQuickSDFPaintTool::IsInPreviewBounds(const FVector2D& ScreenPosition) const
{
	const FVector2D CanvasPos = ConvertInputScreenToCanvasSpace(ScreenPosition);
	const FVector2D Origin = GetPreviewOrigin();
	const FVector2D Size = GetPreviewSize();
	return CanvasPos.X >= Origin.X && CanvasPos.Y >= Origin.Y &&
		CanvasPos.X <= Origin.X + Size.X && CanvasPos.Y <= Origin.Y + Size.Y;
}

FVector2f UQuickSDFPaintTool::ScreenToPreviewUV(const FVector2D& ScreenPosition) const
{
	const FVector2D CanvasPos = ConvertInputScreenToCanvasSpace(ScreenPosition);
	const FVector2D Origin = GetPreviewOrigin();
	const FVector2D Size = GetPreviewSize();
	const float U = static_cast<float>(FMath::Clamp((CanvasPos.X - Origin.X) / Size.X, 0.0, 1.0));
	const float V = static_cast<float>(FMath::Clamp((CanvasPos.Y - Origin.Y) / Size.Y, 0.0, 1.0));
	return FVector2f(U, V);
}

FVector2D UQuickSDFPaintTool::GetBrushPixelSize(UTextureRenderTarget2D* RenderTarget) const
{
	if (!RenderTarget) return FVector2D(16.0, 16.0);
	const FVector2D RTSize(RenderTarget->SizeX, RenderTarget->SizeY);

	if (CurrentComponent.IsValid() && TargetMesh.IsValid())
	{
		const FTransform Transform = CurrentComponent->GetComponentTransform();
		const float MeshBoundsMax = static_cast<float>(TargetMesh->GetBounds().MaxDim());
		const float MaxScale = static_cast<float>(Transform.GetScale3D().GetMax());
		if (MeshBoundsMax > KINDA_SMALL_NUMBER && MaxScale > KINDA_SMALL_NUMBER && BrushProperties)
		{
			const float UVBrushSize = (BrushProperties->BrushRadius / MaxScale / MeshBoundsMax) * 2.0f;
			return FVector2D(UVBrushSize * RTSize.X, UVBrushSize * RTSize.Y);
		}
	}
	
	const float FallbackDiameter = BrushProperties ? FMath::Max(BrushProperties->BrushRadius * 2.0f, 1.0f) : 16.0f;
	return FVector2D(FallbackDiameter, FallbackDiameter);
}

double UQuickSDFPaintTool::GetCurrentStrokeSpacing(UTextureRenderTarget2D* RenderTarget) const
{
	if (ActiveStrokeInputMode == EQuickSDFStrokeInputMode::TexturePreview)
	{
		const FVector2D PixelSize = GetBrushPixelSize(RenderTarget);
		const double PixelRadius = FMath::Max(static_cast<double>(FMath::Min(PixelSize.X, PixelSize.Y) * 0.5f), 1.0);
		return FMath::Max(PixelRadius * QuickSDFStrokeSpacingFactor, 1.0);
	}
	if (!BrushProperties) return 1.0;
	return FMath::Max(static_cast<double>(BrushProperties->BrushRadius) * QuickSDFStrokeSpacingFactor, 0.1);
}

bool UQuickSDFPaintTool::IsPaintingShadow() const { return GetShiftToggle(); }

double UQuickSDFPaintTool::GetToolCurrentTime() const
{
	if (const UWorld* World = GetToolManager()->GetContextQueriesAPI()->GetCurrentEditingWorld())
	{
		return World->GetTimeSeconds();
	}
	return FPlatformTime::Seconds();
}

void UQuickSDFPaintTool::UpdateQuickLineHoldState(const FVector2D& ScreenPosition)
{
	if (!Properties || !Properties->bEnableQuickLine || bQuickLineActive)
	{
		return;
	}

	const double MoveTolerance = FMath::Max(static_cast<double>(Properties->QuickLineMoveTolerance), 1.0);
	if (FVector2D::Distance(ScreenPosition, QuickLineHoldScreenPosition) > MoveTolerance)
	{
		QuickLineHoldScreenPosition = ScreenPosition;
		QuickLineLastMoveTime = GetToolCurrentTime();
	}
}

void UQuickSDFPaintTool::TryActivateQuickLine()
{
	if (!Properties || !Properties->bEnableQuickLine || bQuickLineActive || !bStrokeTransactionActive ||
		!bHasQuickLineStartSample || !bHasQuickLineEndSample ||
		ActiveStrokeInputMode == EQuickSDFStrokeInputMode::None ||
		QuickLineSourceSamples.Num() < 2)
	{
		return;
	}

	if (GetToolCurrentTime() - QuickLineLastMoveTime < static_cast<double>(Properties->QuickLineHoldTime))
	{
		return;
	}

	bQuickLineActive = true;
	PointBuffer.Reset();
	AccumulatedDistance = 0.0;
	RedrawQuickLinePreview();
}

void UQuickSDFPaintTool::RedrawQuickLinePreview()
{
	if (!bQuickLineActive || !bHasQuickLineStartSample || !bHasQuickLineEndSample)
	{
		return;
	}

	if (RestoreStrokeStartPixels())
	{
		StampQuickLineSegment(QuickLineStartSample, QuickLineEndSample);
	}
}

void UQuickSDFPaintTool::StampQuickLineSegment(const FQuickSDFStrokeSample& StartSample, const FQuickSDFStrokeSample& EndSample)
{
	if (QuickLineSourceSamples.Num() >= 2)
	{
		TArray<FQuickSDFStrokeSample> CurveSamples;
		CurveSamples.Reserve(QuickLineSourceSamples.Num());
		for (const FQuickSDFStrokeSample& SourceSample : QuickLineSourceSamples)
		{
			CurveSamples.Add(TransformQuickLineSample(SourceSample));
		}

		UTextureRenderTarget2D* RT = GetActiveRenderTarget();
		if (!RT)
		{
			return;
		}

		const FVector2D RTSize(RT->SizeX, RT->SizeY);
		const FVector2D BrushPixelSize = GetBrushPixelSize(RT);
		const double SpacingPixels = FMath::Max(static_cast<double>(FMath::Min(BrushPixelSize.X, BrushPixelSize.Y) * QuickSDFStrokeSpacingFactor), 1.0);
		TArray<FQuickSDFStrokeSample> ResampledSamples;
		ResampledSamples.Reserve(CurveSamples.Num() * 4);
		ResampledSamples.Add(CurveSamples[0]);

		for (int32 Index = 1; Index < CurveSamples.Num(); ++Index)
		{
			const FQuickSDFStrokeSample& Prev = CurveSamples[Index - 1];
			const FQuickSDFStrokeSample& Curr = CurveSamples[Index];
			const FVector2D PrevPixel(Prev.UV.X * RTSize.X, Prev.UV.Y * RTSize.Y);
			const FVector2D CurrPixel(Curr.UV.X * RTSize.X, Curr.UV.Y * RTSize.Y);
			const double SegmentPixels = FVector2D::Distance(PrevPixel, CurrPixel);
			const int32 StepCount = FMath::Max(FMath::CeilToInt(SegmentPixels / SpacingPixels), 1);
			for (int32 Step = 1; Step <= StepCount; ++Step)
			{
				const float Alpha = static_cast<float>(Step) / static_cast<float>(StepCount);
				FQuickSDFStrokeSample Sample;
				Sample.WorldPos = FMath::Lerp(Prev.WorldPos, Curr.WorldPos, static_cast<double>(Alpha));
				Sample.UV = FMath::Lerp(Prev.UV, Curr.UV, Alpha);
				Sample.LocalUVScale = FMath::Lerp(Prev.LocalUVScale, Curr.LocalUVScale, Alpha);
				ResampledSamples.Add(Sample);
			}
		}

		StampSamples(ResampledSamples);
		return;
	}

	UTextureRenderTarget2D* RT = GetActiveRenderTarget();
	if (!RT) return;

	const double Spacing = FMath::Max(GetCurrentStrokeSpacing(RT), 1.0);
	const double SegmentLength = FVector3d::Distance(StartSample.WorldPos, EndSample.WorldPos);
	const int32 StepCount = FMath::Max(FMath::CeilToInt(SegmentLength / Spacing), 1);

	TArray<FQuickSDFStrokeSample> LineSamples;
	LineSamples.Reserve(StepCount + 1);
	for (int32 Index = 0; Index <= StepCount; ++Index)
	{
		const float Alpha = static_cast<float>(Index) / static_cast<float>(StepCount);
		FQuickSDFStrokeSample Sample;
		Sample.WorldPos = FMath::Lerp(StartSample.WorldPos, EndSample.WorldPos, static_cast<double>(Alpha));
		Sample.UV = FMath::Lerp(StartSample.UV, EndSample.UV, Alpha);
		Sample.LocalUVScale = FMath::Lerp(StartSample.LocalUVScale, EndSample.LocalUVScale, Alpha);
		LineSamples.Add(Sample);
	}

	StampSamples(LineSamples);
}

FQuickSDFStrokeSample UQuickSDFPaintTool::TransformQuickLineSample(const FQuickSDFStrokeSample& SourceSample) const
{
	if (QuickLineSourceSamples.Num() < 2)
	{
		return SourceSample;
	}

	const FQuickSDFStrokeSample& SourceStart = QuickLineSourceSamples[0];
	const FQuickSDFStrokeSample& SourceEnd = QuickLineSourceSamples.Last();
	const FVector2f SourceAxis = SourceEnd.UV - SourceStart.UV;
	const FVector2f TargetAxis = QuickLineEndSample.UV - QuickLineStartSample.UV;
	const float SourceLength = SourceAxis.Size();
	const float TargetLength = TargetAxis.Size();

	FQuickSDFStrokeSample OutSample = SourceSample;
	if (SourceLength <= KINDA_SMALL_NUMBER || TargetLength <= KINDA_SMALL_NUMBER)
	{
		const FVector2f UVOffset = QuickLineEndSample.UV - SourceEnd.UV;
		OutSample.UV = SourceSample.UV + UVOffset;
		OutSample.WorldPos = SourceSample.WorldPos + (QuickLineEndSample.WorldPos - SourceEnd.WorldPos);
		return OutSample;
	}

	const FVector2f SourceUnit = SourceAxis / SourceLength;
	const FVector2f SourcePerp(-SourceUnit.Y, SourceUnit.X);
	const FVector2f TargetUnit = TargetAxis / TargetLength;
	const FVector2f TargetPerp(-TargetUnit.Y, TargetUnit.X);
	const FVector2f SourceOffset = SourceSample.UV - SourceStart.UV;
	const float Along = FVector2f::DotProduct(SourceOffset, SourceUnit);
	const float Across = FVector2f::DotProduct(SourceOffset, SourcePerp);
	const float Scale = TargetLength / SourceLength;

	OutSample.UV = QuickLineStartSample.UV + TargetUnit * (Along * Scale) + TargetPerp * (Across * Scale);
	OutSample.WorldPos = FVector3d(OutSample.UV.X, OutSample.UV.Y, 0.0);
	return OutSample;
}

void UQuickSDFPaintTool::BeginBrushResizeMode()
{
	if (!BrushProperties) return;
	if (bAdjustingBrushRadius) return;
	if (!bBrushResizeTransactionOpen)
	{
		GetToolManager()->BeginUndoTransaction(LOCTEXT("QuickSDFBrushResizeTransaction", "Quick SDF Change Brush Radius"));
		BrushProperties->SetFlags(RF_Transactional);
		BrushProperties->Modify();
		bBrushResizeTransactionOpen = true;
	}
	bAdjustingBrushRadius = true;
	BrushResizeStartScreenPosition = LastInputScreenPosition;
	BrushResizeStartAbsolutePosition = FSlateApplication::Get().GetCursorPos();
	BrushResizeStartStamp = LastBrushStamp;
	BrushResizeStartRadius = BrushProperties->BrushRadius;
	bBrushResizeHadVisibleStamp = BrushStampIndicator && BrushStampIndicator->bVisible;
	if (BrushStampIndicator && bBrushResizeHadVisibleStamp)
	{
		BrushStampIndicator->bVisible = true;
	}
}

void UQuickSDFPaintTool::UpdateBrushResizeFromCursor()
{
	if (!bAdjustingBrushRadius || !BrushProperties) return;
	const FVector2D Delta = ConvertInputScreenToCanvasSpace(LastInputScreenPosition) - ConvertInputScreenToCanvasSpace(BrushResizeStartScreenPosition);
	const float NewRadius = FMath::Max(0.1f, BrushResizeStartRadius + (Delta.X * FMath::Max(BrushResizeSensitivity, QuickSDFMinResizeSensitivity)));
	const float RangeMin = BrushRelativeSizeRange.Min;
	const float RangeSize = BrushRelativeSizeRange.Max - BrushRelativeSizeRange.Min;
	if (RangeSize > KINDA_SMALL_NUMBER)
	{
		BrushProperties->BrushSize = FMath::Clamp((NewRadius - RangeMin) / RangeSize, 0.0f, 1.0f);
	}
	BrushProperties->BrushRadius = NewRadius;
	if (bBrushResizeHadVisibleStamp)
	{
		LastBrushStamp = BrushResizeStartStamp;
		LastBrushStamp.Radius = NewRadius;
		if (BrushStampIndicator)
		{
			BrushStampIndicator->bVisible = true;
		}
	}
	else
	{
		LastBrushStamp.Radius = NewRadius;
	}
	NotifyOfPropertyChangeByTool(BrushProperties);
}

void UQuickSDFPaintTool::EndBrushResizeMode()
{
	if (!bAdjustingBrushRadius) return;
	UpdateBrushResizeFromCursor();
	FSlateApplication::Get().SetCursorPos(BrushResizeStartAbsolutePosition);
	LastInputScreenPosition = BrushResizeStartScreenPosition;
	bAdjustingBrushRadius = false;
	bBrushResizeHadVisibleStamp = false;
	if (bBrushResizeTransactionOpen)
	{
		GetToolManager()->EndUndoTransaction();
		bBrushResizeTransactionOpen = false;
	}
}

FInputRayHit UQuickSDFPaintTool::CanBeginClickDragSequence(const FInputDeviceRay& PressPos)
{
	LastInputScreenPosition = PressPos.ScreenPosition;
	if (bAdjustingBrushRadius)
	{
		PendingStrokeInputMode = EQuickSDFStrokeInputMode::None;
		return FInputRayHit(0.0f);
	}
	if (!Properties)
	{
		PendingStrokeInputMode = EQuickSDFStrokeInputMode::None;
		return FInputRayHit();
	}
	if (Properties->bShowPreview && GetActiveRenderTarget() && IsInPreviewBounds(PressPos.ScreenPosition))
	{
		PendingStrokeInputMode = EQuickSDFStrokeInputMode::TexturePreview;
		return FInputRayHit(0.0f);
	}
	FHitResult OutHit;
	if (HitTest(PressPos.WorldRay, OutHit))
	{
		PendingStrokeInputMode = EQuickSDFStrokeInputMode::MeshSurface;
		return FInputRayHit(OutHit.Distance);
	}
	PendingStrokeInputMode = EQuickSDFStrokeInputMode::None;
	return Super::CanBeginClickDragSequence(PressPos);
}

void UQuickSDFPaintTool::OnClickPress(const FInputDeviceRay& PressPos)
{
	LastInputScreenPosition = PressPos.ScreenPosition;
	if (bAdjustingBrushRadius)
	{
		EndBrushResizeMode();
		return;
	}
	Super::OnClickPress(PressPos);
}

void UQuickSDFPaintTool::OnClickDrag(const FInputDeviceRay& DragPos)
{
	LastInputScreenPosition = DragPos.ScreenPosition;
	if (bAdjustingBrushRadius)
	{
		UpdateBrushResizeFromCursor();
		return;
	}
	Super::OnClickDrag(DragPos);
}

void UQuickSDFPaintTool::OnClickRelease(const FInputDeviceRay& ReleasePos)
{
	LastInputScreenPosition = ReleasePos.ScreenPosition;
	if (bAdjustingBrushRadius) return;
	Super::OnClickRelease(ReleasePos);
}

bool UQuickSDFPaintTool::HitTest(const FRay& Ray, FHitResult& OutHit)
{
	if (CurrentComponent.IsValid())
	{
		FCollisionQueryParams Params(SCENE_QUERY_STAT(QuickSDF), true);
		Params.bReturnFaceIndex = true;
		Params.bTraceComplex = true;

		FHitResult ComponentHit;
		const bool bComponentHit = CurrentComponent->LineTraceComponent(
			ComponentHit,
			Ray.Origin,
			Ray.Origin + Ray.Direction * 100000.0f,
			Params);

		if ((!Properties || Properties->TargetMaterialSlot < 0) && bComponentHit)
		{
			OutHit = ComponentHit;
			return true;
		}

		if (TargetMeshSpatial.IsValid() && TargetMesh.IsValid())
		{
			const FTransform Transform = CurrentComponent->GetComponentTransform();
			const FRay LocalRay(Transform.InverseTransformPosition(Ray.Origin), Transform.InverseTransformVector(Ray.Direction));

			double HitDistance = 100000.0;
			int32 HitTID = INDEX_NONE;
			FVector3d BaryCoords(0.0, 0.0, 0.0);
			if (TargetMeshSpatial->FindNearestHitTriangle(LocalRay, HitDistance, HitTID, BaryCoords) &&
				HitTID != INDEX_NONE &&
				IsTriangleInTargetMaterialSlot(HitTID))
			{
				const FVector LocalHitPosition = (FVector)LocalRay.PointAt(HitDistance);
				const FVector LocalNormal = (FVector)TargetMesh->GetTriNormal(HitTID);
				FVector WorldNormal = Transform.TransformVectorNoScale(LocalNormal).GetSafeNormal();
				if (FVector::DotProduct(WorldNormal, Ray.Direction) > 0.0)
				{
					WorldNormal *= -1.0;
				}

				if (bComponentHit)
				{
					OutHit = ComponentHit;
					OutHit.FaceIndex = HitTID;
					OutHit.Distance = FVector::Distance(Ray.Origin, OutHit.ImpactPoint);
				}
				else
				{
					OutHit.Component = CurrentComponent.Get();
					OutHit.Location = Transform.TransformPosition(LocalHitPosition);
					OutHit.ImpactPoint = OutHit.Location;
					OutHit.Normal = WorldNormal;
					OutHit.ImpactNormal = WorldNormal;
					OutHit.FaceIndex = HitTID;
					OutHit.Distance = FVector::Distance(Ray.Origin, OutHit.Location);
					OutHit.bBlockingHit = true;
				}

				LastBrushStamp.HitResult = OutHit;
				LastBrushStamp.WorldPosition = OutHit.ImpactPoint;
				LastBrushStamp.WorldNormal = OutHit.Normal;
				UpdateBrushStampIndicator();
				return true;
			}
		}
	}

	return false;
}

FInputRayHit UQuickSDFPaintTool::BeginHoverSequenceHitTest(const FInputDeviceRay& PressPos)
{
	LastInputScreenPosition = PressPos.ScreenPosition;
	if (bAdjustingBrushRadius)
	{
		UpdateBrushResizeFromCursor();
		return FInputRayHit(0.0f);
	}

	FHitResult OutHit;
	if (HitTest(PressPos.WorldRay, OutHit))
	{
		LastBrushStamp.Radius = BrushProperties ? BrushProperties->BrushRadius : LastBrushStamp.Radius;
		LastBrushStamp.WorldPosition = OutHit.ImpactPoint;
		LastBrushStamp.WorldNormal = OutHit.Normal;
		LastBrushStamp.HitResult = OutHit;
		LastBrushStamp.Falloff = BrushProperties ? BrushProperties->BrushFalloffAmount : LastBrushStamp.Falloff;
		if (BrushStampIndicator)
		{
			BrushStampIndicator->bVisible = true;
		}
		UpdateBrushStampIndicator();
		return FInputRayHit(OutHit.Distance);
	}

	if (BrushStampIndicator)
	{
		BrushStampIndicator->bVisible = false;
	}
	return FInputRayHit();
}

bool UQuickSDFPaintTool::OnUpdateHover(const FInputDeviceRay& DevicePos)
{
	LastInputScreenPosition = DevicePos.ScreenPosition;
	if (bAdjustingBrushRadius)
	{
		UpdateBrushResizeFromCursor();
		return true;
	}
	FHitResult OutHit;
	if (HitTest(DevicePos.WorldRay, OutHit))
	{
		LastBrushStamp.Radius = BrushProperties ? BrushProperties->BrushRadius : LastBrushStamp.Radius;
		LastBrushStamp.WorldPosition = OutHit.ImpactPoint;
		LastBrushStamp.WorldNormal = OutHit.Normal;
		LastBrushStamp.HitResult = OutHit;
		LastBrushStamp.Falloff = BrushProperties ? BrushProperties->BrushFalloffAmount : LastBrushStamp.Falloff;
		if (BrushStampIndicator)
		{
			BrushStampIndicator->bVisible = true;
		}
		UpdateBrushStampIndicator();
		return true;
	}

	if (BrushStampIndicator)
	{
		BrushStampIndicator->bVisible = false;
	}
	return true;
}

void UQuickSDFPaintTool::OnEndHover()
{
	if (bAdjustingBrushRadius)
	{
		return;
	}
	if (BrushStampIndicator)
	{
		BrushStampIndicator->bVisible = false;
	}
}

void UQuickSDFPaintTool::OnBeginDrag(const FRay& Ray)
{
	Super::OnBeginDrag(Ray);
	const EQuickSDFStrokeInputMode StrokeMode = PendingStrokeInputMode;
	ResetStrokeState();
	ActiveStrokeInputMode = StrokeMode;

	PointBuffer.Empty();
	AccumulatedDistance = 0.0;

	FQuickSDFStrokeSample StartSample;
	bool bHasSample = false;
	if (ActiveStrokeInputMode == EQuickSDFStrokeInputMode::TexturePreview) {
		bHasSample = TryMakePreviewStrokeSample(LastInputScreenPosition, StartSample);
	} else if (ActiveStrokeInputMode == EQuickSDFStrokeInputMode::MeshSurface) {
		bHasSample = TryMakeStrokeSample(Ray, StartSample);
	}

	if (bHasSample) {
		if (BrushStampIndicator)
		{
			BrushStampIndicator->bVisible = true;
		}
		UpdateBrushStampIndicator();
		BeginStrokeTransaction(); 
		PointBuffer.Add(StartSample);
		QuickLineSourceSamples.Reset();
		QuickLineSourceSamples.Add(StartSample);
		QuickLineStartSample = StartSample;
		QuickLineEndSample = StartSample;
		bHasQuickLineStartSample = true;
		bHasQuickLineEndSample = true;
		QuickLineHoldScreenPosition = LastInputScreenPosition;
		QuickLineLastMoveTime = GetToolCurrentTime();
		//StampSample(StartSample); 
	}
}

void UQuickSDFPaintTool::OnUpdateDrag(const FRay& Ray)
{
	Super::OnUpdateDrag(Ray);
	if (bAdjustingBrushRadius) return;

	FQuickSDFStrokeSample Sample;
	bool bHasSample = false;
	
	if (ActiveStrokeInputMode == EQuickSDFStrokeInputMode::TexturePreview)
	{
		bHasSample = TryMakePreviewStrokeSample(LastInputScreenPosition, Sample);
	}
	else if (ActiveStrokeInputMode == EQuickSDFStrokeInputMode::MeshSurface)
	{
		bHasSample = TryMakeStrokeSample(Ray, Sample);
	}

	if (bHasSample)
	{
		if (BrushStampIndicator)
		{
			BrushStampIndicator->bVisible = true;
		}
		UpdateBrushStampIndicator();
		Sample = SmoothStrokeSample(Sample);
		QuickLineEndSample = Sample;
		bHasQuickLineEndSample = true;
		UpdateQuickLineHoldState(LastInputScreenPosition);
		if (bQuickLineActive)
		{
			RedrawQuickLinePreview();
			return;
		}
		if (QuickLineSourceSamples.Num() == 0 ||
			FVector3d::DistSquared(QuickLineSourceSamples.Last().WorldPos, Sample.WorldPos) > 1e-8)
		{
			QuickLineSourceSamples.Add(Sample);
		}
		AppendStrokeSample(Sample);
	}
}

void UQuickSDFPaintTool::OnEndDrag(const FRay& Ray)
{
	if (bQuickLineActive)
	{
		RedrawQuickLinePreview();
		EndStrokeTransaction();
		PointBuffer.Empty();
		ResetStrokeState();
		return;
	}

	if (PointBuffer.Num() >= 2) {
		FQuickSDFStrokeSample Last = PointBuffer.Last();
		
		AppendStrokeSample(Last); 
		
		int32 L = PointBuffer.Num();
		if (L >= 4) {
			StampInterpolatedSegment(PointBuffer[L-3], PointBuffer[L-2], PointBuffer[L-1], PointBuffer[L-1]);
		}
	}

	EndStrokeTransaction();
	PointBuffer.Empty();
	ResetStrokeState();
}

bool UQuickSDFPaintTool::TryMakeStrokeSample(const FRay& Ray, FQuickSDFStrokeSample& OutSample)
{
	if (!TargetMeshSpatial.IsValid() || !TargetMesh.IsValid() || !Properties || !CurrentComponent.IsValid()) return false;
	
	const FTransform Transform = CurrentComponent->GetComponentTransform();
	const FRay LocalRay(Transform.InverseTransformPosition(Ray.Origin), Transform.InverseTransformVector(Ray.Direction));

	double HitDistance = 100000.0;
	int32 HitTID = -1;
	FVector3d BaryCoords(0.0, 0.0, 0.0);
	const bool bHit = TargetMeshSpatial->FindNearestHitTriangle(LocalRay, HitDistance, HitTID, BaryCoords);

	if (!bHit || HitTID < 0) return false;
	if (!IsTriangleInTargetMaterialSlot(HitTID)) return false;

	UE::Geometry::FIndex3i TriV = TargetMesh->GetTriangle(HitTID);

	// 2. インデックスからローカル頂点座標を取得し、ワールド座標に変換
	FVector3d V0 = Transform.TransformPosition(TargetMesh->GetVertex(TriV.A));
	FVector3d V1 = Transform.TransformPosition(TargetMesh->GetVertex(TriV.B));
	FVector3d V2 = Transform.TransformPosition(TargetMesh->GetVertex(TriV.C));

	// UV座標を取得
	const UE::Geometry::FDynamicMeshUVOverlay* UVOverlay = TargetMesh->Attributes()->GetUVLayer(Properties->UVChannel);
	if (!UVOverlay || !UVOverlay->IsSetTriangle(HitTID)) return false;

	const UE::Geometry::FIndex3i TriUVs = UVOverlay->GetTriangle(HitTID);
	const FVector2f UV0 = UVOverlay->GetElement(TriUVs.A);
	const FVector2f UV1 = UVOverlay->GetElement(TriUVs.B);
	const FVector2f UV2 = UVOverlay->GetElement(TriUVs.C);

	double ScaleSum = 0.0;
	int32 ScaleCount = 0;
	auto AccumulateUVScale = [&ScaleSum, &ScaleCount](const FVector3d& WorldA, const FVector3d& WorldB, const FVector2f& UVA, const FVector2f& UVB)
	{
		const double WorldLength = FVector3d::Distance(WorldA, WorldB);
		const double UVLength = FVector2f::Distance(UVA, UVB);
		if (WorldLength > KINDA_SMALL_NUMBER && UVLength > KINDA_SMALL_NUMBER)
		{
			ScaleSum += UVLength / WorldLength;
			++ScaleCount;
		}
	};

	AccumulateUVScale(V0, V1, UV0, UV1);
	AccumulateUVScale(V1, V2, UV1, UV2);
	AccumulateUVScale(V2, V0, UV2, UV0);

	if (ScaleCount > 0)
	{
		OutSample.LocalUVScale = static_cast<float>(ScaleSum / static_cast<double>(ScaleCount));
	}
	else
	{
		OutSample.LocalUVScale = 1.0f / FMath::Max(static_cast<float>(TargetMesh->GetBounds().MaxDim()), KINDA_SMALL_NUMBER);
	}
	OutSample.UV = UV0 * static_cast<float>(BaryCoords.X) +
		UV1 * static_cast<float>(BaryCoords.Y) +
		UV2 * static_cast<float>(BaryCoords.Z);
	OutSample.WorldPos = Transform.TransformPosition(LocalRay.PointAt(HitDistance));
	return true;
}

bool UQuickSDFPaintTool::TryMakePreviewStrokeSample(const FVector2D& ScreenPosition, FQuickSDFStrokeSample& OutSample) const
{
	UTextureRenderTarget2D* RT = GetActiveRenderTarget();
	if (!RT || !IsInPreviewBounds(ScreenPosition)) return false;

	OutSample.UV = ScreenToPreviewUV(ScreenPosition);
	OutSample.WorldPos = FVector3d(OutSample.UV.X * RT->SizeX, OutSample.UV.Y * RT->SizeY, 0.0);
	return true;
}

void UQuickSDFPaintTool::StampSample(const FQuickSDFStrokeSample& Sample)
{
	StampSamples({ Sample });
}

void UQuickSDFPaintTool::StampSamples(const TArray<FQuickSDFStrokeSample>& Samples)
{
	if (Samples.Num() == 0) return;

	const TArray<int32> PaintTargetAngleIndices = GetPaintTargetAngleIndices();
	if (Properties && PaintTargetAngleIndices.Num() > 1 && !bStampingAllPaintTargets)
	{
		const int32 PreviousEditAngleIndex = Properties->EditAngleIndex;
		bStampingAllPaintTargets = true;
		for (int32 AngleIndex : PaintTargetAngleIndices)
		{
			Properties->EditAngleIndex = AngleIndex;
			StampSamples(Samples);
		}
		Properties->EditAngleIndex = PreviousEditAngleIndex;
		bStampingAllPaintTargets = false;
		RefreshPreviewMaterial();
		return;
	}

	UTextureRenderTarget2D* RT = GetActiveRenderTarget();
	if (!RT || !BrushMaskTexture) return;

	FTextureRenderTargetResource* RTResource = RT->GameThread_GetRenderTargetResource();
	if (!RTResource) return;

	const FVector2D RTSize(RT->SizeX, RT->SizeY);
	const FLinearColor PaintColor = IsPaintingShadow() ? FLinearColor::Black : FLinearColor::White;
	
	FCanvas Canvas(RTResource, nullptr, GetToolManager()->GetContextQueriesAPI()->GetCurrentEditingWorld(), GMaxRHIFeatureLevel);

	for (const FQuickSDFStrokeSample& Sample : Samples)
	{
		FVector2D PixelSize;
		
		if (ActiveStrokeInputMode == EQuickSDFStrokeInputMode::TexturePreview)
		{
			// プレビュー表示（2D）の場合は以前の固定計算
			PixelSize = GetBrushPixelSize(RT);
		}
		else
		{
			const float BrushRadiusWorld = BrushProperties ? BrushProperties->BrushRadius : 10.0f;
			const float UVRadius = BrushRadiusWorld * FMath::Max(Sample.LocalUVScale, KINDA_SMALL_NUMBER);
			PixelSize = FVector2D(UVRadius * RTSize.X * 2.0f, UVRadius * RTSize.Y * 2.0f);
		}
		const FVector2D PixelPos(Sample.UV.X * RTSize.X, Sample.UV.Y * RTSize.Y);
		const FVector2D StampPos = PixelPos - (PixelSize * 0.5f);
		FCanvasTileItem BrushItem(StampPos, BrushMaskTexture->GetResource(), PixelSize, PaintColor);
		BrushItem.BlendMode = SE_BLEND_Translucent;
		Canvas.DrawItem(BrushItem);
	}
	
	Canvas.Flush_GameThread(false);

	ENQUEUE_RENDER_COMMAND(UpdateSDFPaintRTCommand)(
		[RTResource](FRHICommandListImmediate& RHICmdList)
		{
			TransitionAndCopyTexture(RHICmdList, RTResource->GetRenderTargetTexture(), RTResource->TextureRHI, {});
		});

	RefreshPreviewMaterial();
}

void UQuickSDFPaintTool::AppendStrokeSample(const FQuickSDFStrokeSample& Sample)
{
	if (PointBuffer.Num() > 0)
	{
		if (FVector3d::DistSquared(PointBuffer.Last().WorldPos, Sample.WorldPos) < 1e-8)
		{
			return;
		}
	}
	
	PointBuffer.Add(Sample);
	
	if (PointBuffer.Num() >= 4) {
		int32 L = PointBuffer.Num();
		StampInterpolatedSegment(PointBuffer[L-4], PointBuffer[L-3], PointBuffer[L-2], PointBuffer[L-1]);
		
		if (PointBuffer.Num() > 4) {
			PointBuffer.RemoveAt(0);
		}
	}
}

void UQuickSDFPaintTool::StampInterpolatedSegment(
    const FQuickSDFStrokeSample& P0,
    const FQuickSDFStrokeSample& P1,
    const FQuickSDFStrokeSample& P2,
    const FQuickSDFStrokeSample& P3)
{
    UTextureRenderTarget2D* RT = GetActiveRenderTarget();
    if (!RT) return;

    const double Spacing = GetCurrentStrokeSpacing(RT);
    if (Spacing <= KINDA_SMALL_NUMBER) return;
    
    const double Alpha = 0.5; 
    auto GetT = [Alpha](double t, const FVector3d& p1, const FVector3d& p2) {
        double d = FVector3d::DistSquared(p1, p2);
        return FMath::Pow(d, Alpha * 0.5) + t;
    };

    double t0 = 0.0;
    double t1 = GetT(t0, P0.WorldPos, P1.WorldPos);
    double t2 = GetT(t1, P1.WorldPos, P2.WorldPos);
    double t3 = GetT(t2, P2.WorldPos, P3.WorldPos);

    if (t2 - t1 < KINDA_SMALL_NUMBER) return;
    
    auto EvaluateSpline = [&](double t) {
        FQuickSDFStrokeSample Out;
        
        double a1 = (t1 - t) / (t1 - t0); double b1 = (t - t0) / (t1 - t0);
        double a2 = (t2 - t) / (t2 - t1); double b2 = (t - t1) / (t2 - t1);
        double a3 = (t3 - t) / (t3 - t2); double b3 = (t - t2) / (t3 - t2);
        double a12 = (t2 - t) / (t2 - t0); double b12 = (t - t0) / (t2 - t0);
        double a23 = (t3 - t) / (t3 - t1); double b23 = (t - t1) / (t3 - t1);

        FVector3d A1 = a1 * P0.WorldPos + b1 * P1.WorldPos;
        FVector3d A2 = a2 * P1.WorldPos + b2 * P2.WorldPos;
        FVector3d A3 = a3 * P2.WorldPos + b3 * P3.WorldPos;
        FVector3d B1 = a12 * A1 + b12 * A2;
        FVector3d B2 = a23 * A2 + b23 * A3;
        Out.WorldPos = a2 * B1 + b2 * B2;
    	
        FVector2f U1 = a1 * P0.UV + b1 * P1.UV;
        FVector2f U2 = a2 * P1.UV + b2 * P2.UV;
        FVector2f U3 = a3 * P2.UV + b3 * P3.UV;
        FVector2f V1 = a12 * U1 + b12 * U2;
        FVector2f V2 = a23 * U2 + b23 * U3;
        Out.UV = (float)a2 * V1 + (float)b2 * V2;
        
    	double s1 = a1 * P0.LocalUVScale + b1 * P1.LocalUVScale;
    	double s2 = a2 * P1.LocalUVScale + b2 * P2.LocalUVScale;
    	double s3 = a3 * P2.LocalUVScale + b3 * P3.LocalUVScale;
    	double sv1 = a12 * s1 + b12 * s2;
    	double sv2 = a23 * s2 + b23 * s3;
    	Out.LocalUVScale = (float)(a2 * sv1 + b2 * sv2);
    	
        return Out;
    };
	
    const double SegmentLength = FVector3d::Distance(P1.WorldPos, P2.WorldPos);
    
    const int32 SubSteps = FMath::Clamp(FMath::CeilToInt(SegmentLength / (Spacing * 0.1)), 20, 1000);
    const double dt = (t2 - t1) / (double)SubSteps;

    TArray<FQuickSDFStrokeSample> Batch;
    FQuickSDFStrokeSample Prev = EvaluateSpline(t1);

    for (int32 i = 1; i <= SubSteps; ++i) {
        double currT = t1 + i * dt;
        FQuickSDFStrokeSample Curr = EvaluateSpline(currT);
        
        double Dist = FVector3d::Distance(Prev.WorldPos, Curr.WorldPos);
        AccumulatedDistance += Dist;

        while (AccumulatedDistance >= Spacing) {
            double Ratio = 1.0 - ((AccumulatedDistance - Spacing) / FMath::Max(Dist, 0.0001));
            FQuickSDFStrokeSample Stamp;
            Stamp.WorldPos = FMath::Lerp(Prev.WorldPos, Curr.WorldPos, Ratio);
            Stamp.UV = FMath::Lerp(Prev.UV, Curr.UV, (float)Ratio);
        	Stamp.LocalUVScale = FMath::Lerp(Prev.LocalUVScale, Curr.LocalUVScale, (float)Ratio);
            Batch.Add(Stamp);
            AccumulatedDistance -= Spacing;
        }
        Prev = Curr;
    }
    StampSamples(Batch);
}

void UQuickSDFPaintTool::ResetStrokeState()
{
	StrokeSamples.Reset();
	WorldPosFilter.Reset();
	UVFilter.Reset();
	bHasLastStampedSample = false;
	bHasFilteredStrokeSample = false;
	DistanceSinceLastStamp = 0.0;
	ActiveStrokeInputMode = EQuickSDFStrokeInputMode::None;
	PendingStrokeInputMode = EQuickSDFStrokeInputMode::None;
	bQuickLineActive = false;
	bHasQuickLineStartSample = false;
	bHasQuickLineEndSample = false;
	QuickLineStartSample = FQuickSDFStrokeSample();
	QuickLineEndSample = FQuickSDFStrokeSample();
	QuickLineSourceSamples.Reset();
	QuickLineHoldScreenPosition = FVector2D::ZeroVector;
	QuickLineLastMoveTime = 0.0;
}
#undef LOCTEXT_NAMESPACE
