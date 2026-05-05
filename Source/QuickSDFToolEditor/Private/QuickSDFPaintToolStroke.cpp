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
#include "LevelEditor.h"
#include "Misc/DefaultValueHelper.h"
#include "Containers/Ticker.h"
#include "SLevelViewport.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetToolsModule.h"
#include "IAssetTools.h"
#include "Misc/PackageName.h"
#include "ObjectTools.h"
#include "Widgets/SViewport.h"

#if WITH_EDITOR
#include "Editor.h"
#include "IMaterialBakingModule.h"
#include "MaterialBakingStructures.h"
#endif

#define LOCTEXT_NAMESPACE "QuickSDFPaintTool"

using namespace QuickSDFPaintToolPrivate;

namespace
{
bool IsCursorOverLevelViewport()
{
	if (!FModuleManager::Get().IsModuleLoaded(TEXT("LevelEditor")))
	{
		return false;
	}

	FLevelEditorModule& LevelEditorModule = FModuleManager::GetModuleChecked<FLevelEditorModule>(TEXT("LevelEditor"));
	const TSharedPtr<ILevelEditor> LevelEditor = LevelEditorModule.GetFirstLevelEditor();
	if (!LevelEditor.IsValid())
	{
		return false;
	}

	const FVector2D CursorPosition = FSlateApplication::Get().GetCursorPos();
	for (const TSharedPtr<SLevelViewport>& LevelViewport : LevelEditor->GetViewports())
	{
		if (!LevelViewport.IsValid())
		{
			continue;
		}

		if (const TSharedPtr<SViewport> ViewportWidget = LevelViewport->GetViewportWidget().Pin())
		{
			if (ViewportWidget->GetTickSpaceGeometry().IsUnderLocation(CursorPosition))
			{
				return true;
			}
		}
	}

	return false;
}
}

void UQuickSDFPaintTool::InvalidatePaintChartCache()
{
	TargetTrianglePaintChartIDs.Reset();
	bPaintChartCacheDirty = true;
	CachedPaintChartUVChannel = INDEX_NONE;
	CachedPaintChartMaterialSlot = INDEX_NONE;
	InvalidateAutoSymmetryCache();
}

void UQuickSDFPaintTool::EnsurePaintChartCache()
{
	if (!bPaintChartCacheDirty &&
		Properties &&
		CachedPaintChartUVChannel == Properties->UVChannel &&
		CachedPaintChartMaterialSlot == Properties->TargetMaterialSlot)
	{
		return;
	}

	TargetTrianglePaintChartIDs.Reset();
	bPaintChartCacheDirty = false;
	CachedPaintChartUVChannel = Properties ? Properties->UVChannel : INDEX_NONE;
	CachedPaintChartMaterialSlot = Properties ? Properties->TargetMaterialSlot : INDEX_NONE;

	if (!Properties || !TargetMesh.IsValid() || !TargetMesh->HasAttributes())
	{
		return;
	}

	const UE::Geometry::FDynamicMeshUVOverlay* UVOverlay = TargetMesh->Attributes()->GetUVLayer(Properties->UVChannel);
	if (!UVOverlay)
	{
		return;
	}

	TSet<int32> EligibleTriangles;
	TMap<FQuickSDFUVEdgeKey, TArray<int32>> EdgeToTriangles;
	for (int32 TriangleID : TargetMesh->TriangleIndicesItr())
	{
		if (!IsTriangleInTargetMaterialSlot(TriangleID) || !UVOverlay->IsSetTriangle(TriangleID))
		{
			continue;
		}

		const UE::Geometry::FIndex3i UVIndices = UVOverlay->GetTriangle(TriangleID);
		const FVector2f UV0 = UVOverlay->GetElement(UVIndices.A);
		const FVector2f UV1 = UVOverlay->GetElement(UVIndices.B);
		const FVector2f UV2 = UVOverlay->GetElement(UVIndices.C);

		EligibleTriangles.Add(TriangleID);
		EdgeToTriangles.FindOrAdd(MakeUVEdgeKey(UV0, UV1)).Add(TriangleID);
		EdgeToTriangles.FindOrAdd(MakeUVEdgeKey(UV1, UV2)).Add(TriangleID);
		EdgeToTriangles.FindOrAdd(MakeUVEdgeKey(UV2, UV0)).Add(TriangleID);
	}

	int32 NextChartID = 0;
	TArray<int32> Queue;
	for (int32 SeedTriangleID : EligibleTriangles)
	{
		if (TargetTrianglePaintChartIDs.Contains(SeedTriangleID))
		{
			continue;
		}

		Queue.Reset();
		Queue.Add(SeedTriangleID);
		TargetTrianglePaintChartIDs.Add(SeedTriangleID, NextChartID);

		for (int32 QueueIndex = 0; QueueIndex < Queue.Num(); ++QueueIndex)
		{
			const int32 TriangleID = Queue[QueueIndex];
			const UE::Geometry::FIndex3i UVIndices = UVOverlay->GetTriangle(TriangleID);
			const FVector2f UV0 = UVOverlay->GetElement(UVIndices.A);
			const FVector2f UV1 = UVOverlay->GetElement(UVIndices.B);
			const FVector2f UV2 = UVOverlay->GetElement(UVIndices.C);

			const FQuickSDFUVEdgeKey Edges[3] = {
				MakeUVEdgeKey(UV0, UV1),
				MakeUVEdgeKey(UV1, UV2),
				MakeUVEdgeKey(UV2, UV0)
			};

			for (const FQuickSDFUVEdgeKey& Edge : Edges)
			{
				const TArray<int32>* Neighbors = EdgeToTriangles.Find(Edge);
				if (!Neighbors)
				{
					continue;
				}

				for (int32 NeighborTriangleID : *Neighbors)
				{
					if (!TargetTrianglePaintChartIDs.Contains(NeighborTriangleID))
					{
						TargetTrianglePaintChartIDs.Add(NeighborTriangleID, NextChartID);
						Queue.Add(NeighborTriangleID);
					}
				}
			}
		}

		++NextChartID;
	}
}

int32 UQuickSDFPaintTool::GetPaintChartIDForTriangle(int32 TriangleID)
{
	EnsurePaintChartCache();
	if (const int32* PaintChartID = TargetTrianglePaintChartIDs.Find(TriangleID))
	{
		return *PaintChartID;
	}
	return INDEX_NONE;
}

EQuickSDFMeshPaintMode UQuickSDFPaintTool::GetMeshPaintMode() const
{
	if (!Properties)
	{
		return EQuickSDFMeshPaintMode::UVSpaceLegacy;
	}

	if (Properties->bUseSurfaceSpacePaint &&
		Properties->MeshPaintMode == EQuickSDFMeshPaintMode::UVSpaceLegacy)
	{
		return EQuickSDFMeshPaintMode::ProjectedSurface;
	}

	return Properties->MeshPaintMode;
}

bool UQuickSDFPaintTool::ShouldUseSurfaceSpacePaint() const
{
	return ShouldUseProjectedSurfacePaint();
}

bool UQuickSDFPaintTool::ShouldUseProjectedSurfacePaint() const
{
	return Properties &&
		GetMeshPaintMode() == EQuickSDFMeshPaintMode::ProjectedSurface &&
		ActiveStrokeInputMode == EQuickSDFStrokeInputMode::MeshSurface;
}

bool UQuickSDFPaintTool::ShouldUseScreenProjectionPaint() const
{
	return Properties &&
		GetMeshPaintMode() == EQuickSDFMeshPaintMode::ScreenProjection &&
		ActiveStrokeInputMode == EQuickSDFStrokeInputMode::MeshSurface;
}

bool UQuickSDFPaintTool::ShouldUseAnySurfaceProjectionPaint() const
{
	return ShouldUseProjectedSurfacePaint() || ShouldUseScreenProjectionPaint();
}

float UQuickSDFPaintTool::GetScreenProjectionBrushRadiusPixels() const
{
	return Properties ? FMath::Max(Properties->ScreenProjectionBrushRadiusPixels, 1.0f) : 32.0f;
}

bool UQuickSDFPaintTool::CanInterpolateStrokeSamples(const FQuickSDFStrokeSample& A, const FQuickSDFStrokeSample& B) const
{
	if (!Properties ||
		ActiveStrokeInputMode != EQuickSDFStrokeInputMode::MeshSurface ||
		ShouldUseAnySurfaceProjectionPaint())
	{
		return true;
	}

	return A.PaintChartID != INDEX_NONE &&
		B.PaintChartID != INDEX_NONE &&
		A.PaintChartID == B.PaintChartID;
}

FQuickSDFStrokeSample UQuickSDFPaintTool::SmoothStrokeSample(const FQuickSDFStrokeSample& RawSample)
{
	if (!Properties || !Properties->bEnableStrokeStabilizer)
	{
		FilteredStrokeSample = RawSample;
		bHasFilteredStrokeSample = true;
		return RawSample;
	}

	UTextureRenderTarget2D* RT = GetActiveRenderTarget();
	if (!RT)
	{
		FilteredStrokeSample = RawSample;
		bHasFilteredStrokeSample = true;
		return RawSample;
	}

	const double ResolutionScale = static_cast<double>(FMath::Min(RT->SizeX, RT->SizeY)) / 1024.0;
	const double LazyRadiusPixels = FMath::Max(static_cast<double>(Properties->StrokeStabilizerRadius) * ResolutionScale, 0.0);
	if (LazyRadiusPixels <= KINDA_SMALL_NUMBER)
	{
		FilteredStrokeSample = RawSample;
		bHasFilteredStrokeSample = true;
		return RawSample;
	}

	if (!bHasFilteredStrokeSample)
	{
		FilteredStrokeSample = RawSample;
		bHasFilteredStrokeSample = true;
		return RawSample;
	}

	if (!CanInterpolateStrokeSamples(FilteredStrokeSample, RawSample))
	{
		FilteredStrokeSample = RawSample;
		bHasFilteredStrokeSample = true;
		return RawSample;
	}

	double LazyRadius = LazyRadiusPixels;
	double SampleDistance = 0.0;
	if (ShouldUseProjectedSurfacePaint())
	{
		const FVector2D BrushPixelSize = GetBrushPixelSize(RT);
		const double BrushPixelRadius = FMath::Max(static_cast<double>(FMath::Min(BrushPixelSize.X, BrushPixelSize.Y) * 0.5f), 1.0);
		LazyRadius = (LazyRadiusPixels / BrushPixelRadius) * GetEffectiveBrushRadius();
		SampleDistance = FVector3d::Distance(FilteredStrokeSample.WorldPos, RawSample.WorldPos);
	}
	else if (ShouldUseScreenProjectionPaint())
	{
		const double BrushPixelRadius = FMath::Max(static_cast<double>(GetScreenProjectionBrushRadiusPixels()), 1.0);
		LazyRadius = (LazyRadiusPixels / BrushPixelRadius) * BrushPixelRadius;
		SampleDistance = FVector2D::Distance(FilteredStrokeSample.ScreenPosition, RawSample.ScreenPosition);
	}
	else
	{
		const FVector2D FilteredPixel = GetSamplePixelPosition(FilteredStrokeSample, RT);
		const FVector2D RawPixel = GetSamplePixelPosition(RawSample, RT);
		SampleDistance = FVector2D::Distance(FilteredPixel, RawPixel);
	}

	if (SampleDistance <= LazyRadius)
	{
		return FilteredStrokeSample;
	}

	const double Alpha = 1.0 - (LazyRadius / SampleDistance);
	FilteredStrokeSample = LerpStrokeSample(FilteredStrokeSample, RawSample, Alpha);
	return FilteredStrokeSample;
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
	const double BrushRadius = GetEffectiveBrushRadius();

	if (CurrentComponent.IsValid() && TargetMesh.IsValid())
	{
		const FTransform Transform = CurrentComponent->GetComponentTransform();
		const float MeshBoundsMax = static_cast<float>(TargetMesh->GetBounds().MaxDim());
		const float MaxScale = static_cast<float>(Transform.GetScale3D().GetMax());
		if (MeshBoundsMax > KINDA_SMALL_NUMBER && MaxScale > KINDA_SMALL_NUMBER)
		{
			const float UVBrushSize = static_cast<float>((BrushRadius / MaxScale / MeshBoundsMax) * 2.0);
			return FVector2D(UVBrushSize * RTSize.X, UVBrushSize * RTSize.Y);
		}
	}
	
	const float FallbackDiameter = static_cast<float>(FMath::Max(BrushRadius * 2.0, 1.0));
	return FVector2D(FallbackDiameter, FallbackDiameter);
}

double UQuickSDFPaintTool::GetCurrentStrokeSpacing(UTextureRenderTarget2D* RenderTarget) const
{
	const double SpacingRatio = Properties ? static_cast<double>(Properties->StrokeSpacingRatio) : QuickSDFStrokeSpacingFactor;
	if (ShouldUseProjectedSurfacePaint())
	{
		return FMath::Max(GetEffectiveBrushRadius() * FMath::Max(SpacingRatio, 0.02), 0.1);
	}
	if (ShouldUseScreenProjectionPaint())
	{
		return FMath::Max(static_cast<double>(GetScreenProjectionBrushRadiusPixels()) * FMath::Max(SpacingRatio, 0.02), 1.0);
	}

	const FVector2D PixelSize = GetBrushPixelSize(RenderTarget);
	const double PixelRadius = FMath::Max(static_cast<double>(FMath::Min(PixelSize.X, PixelSize.Y) * 0.5f), 1.0);
	return FMath::Max(PixelRadius * FMath::Max(SpacingRatio, 0.02), 1.0);
}

double UQuickSDFPaintTool::GetEffectiveBrushRadius() const
{
	if (!BrushProperties)
	{
		return 10.0;
	}
	return FMath::Max(GetCurrentBrushRadius(), 0.1);
}

FVector2D UQuickSDFPaintTool::GetSamplePixelPosition(const FQuickSDFStrokeSample& Sample, UTextureRenderTarget2D* RenderTarget) const
{
	if (ShouldUseScreenProjectionPaint())
	{
		return Sample.ScreenPosition;
	}
	if (!RenderTarget)
	{
		return FVector2D(Sample.WorldPos.X, Sample.WorldPos.Y);
	}
	return FVector2D(Sample.UV.X * RenderTarget->SizeX, Sample.UV.Y * RenderTarget->SizeY);
}

double UQuickSDFPaintTool::GetSamplePixelDistance(const FQuickSDFStrokeSample& A, const FQuickSDFStrokeSample& B, UTextureRenderTarget2D* RenderTarget) const
{
	if (ShouldUseProjectedSurfacePaint())
	{
		return FVector3d::Distance(A.WorldPos, B.WorldPos);
	}
	if (ShouldUseScreenProjectionPaint())
	{
		return FVector2D::Distance(A.ScreenPosition, B.ScreenPosition);
	}
	return FVector2D::Distance(GetSamplePixelPosition(A, RenderTarget), GetSamplePixelPosition(B, RenderTarget));
}

FQuickSDFStrokeSample UQuickSDFPaintTool::LerpStrokeSample(const FQuickSDFStrokeSample& A, const FQuickSDFStrokeSample& B, double Alpha) const
{
	const float AlphaFloat = static_cast<float>(FMath::Clamp(Alpha, 0.0, 1.0));
	FQuickSDFStrokeSample Result;
	Result.WorldPos = FMath::Lerp(A.WorldPos, B.WorldPos, static_cast<double>(AlphaFloat));
	Result.UV = FMath::Lerp(A.UV, B.UV, AlphaFloat);
	Result.LocalUVScale = FMath::Lerp(A.LocalUVScale, B.LocalUVScale, AlphaFloat);
	Result.TriangleID = B.TriangleID;
	Result.PaintChartID = B.PaintChartID;
	Result.ScreenPosition = FMath::Lerp(A.ScreenPosition, B.ScreenPosition, AlphaFloat);
	Result.RayOrigin = FMath::Lerp(A.RayOrigin, B.RayOrigin, static_cast<double>(AlphaFloat));
	Result.RayDirection = FMath::Lerp(A.RayDirection, B.RayDirection, static_cast<double>(AlphaFloat)).GetSafeNormal();
	return Result;
}

bool UQuickSDFPaintTool::IsPaintingShadow() const { return GetShiftToggle(); }

void UQuickSDFPaintTool::UpdateBrushStampIndicator()
{
	if (BrushStampIndicator && BrushProperties)
	{
		if (Properties &&
			GetMeshPaintMode() == EQuickSDFMeshPaintMode::ScreenProjection &&
			ActiveStrokeInputMode != EQuickSDFStrokeInputMode::TexturePreview &&
			PendingStrokeInputMode != EQuickSDFStrokeInputMode::TexturePreview)
		{
			BrushStampIndicator->bVisible = false;
			return;
		}
		BrushStampIndicator->Update(
			GetEffectiveBrushRadius(),
			LastBrushStamp.WorldPosition,
			LastBrushStamp.WorldNormal,
			BrushProperties->BrushFalloffAmount,
			BrushProperties->BrushStrength);
	}
}

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

void UQuickSDFPaintTool::RedrawQuickLinePreview(bool bForce)
{
	if (!bQuickLineActive || !bHasQuickLineStartSample || !bHasQuickLineEndSample)
	{
		return;
	}

	if (ActiveStrokeInputMode == EQuickSDFStrokeInputMode::TexturePreview && !bForce)
	{
		return;
	}

	if (RestoreStrokeStartPixels())
	{
		StampQuickLineSegment(QuickLineStartSample, QuickLineEndSample, bForce);
	}
}

void UQuickSDFPaintTool::StampQuickLineSegment(const FQuickSDFStrokeSample& StartSample, const FQuickSDFStrokeSample& EndSample, bool bForce)
{
	if (ActiveStrokeInputMode == EQuickSDFStrokeInputMode::MeshSurface)
	{
		StampQuickLineSurfaceSegment(StartSample, EndSample, bForce);
		return;
	}

	if (QuickLineSourceSamples.Num() >= 2)
	{
		TArray<FQuickSDFStrokeSample> CurveSamples;
		CurveSamples.Reserve(QuickLineSourceSamples.Num());
		const FQuickSDFStrokeSample& SourceStart = QuickLineSourceSamples[0];
		const FQuickSDFStrokeSample& SourceEnd = QuickLineSourceSamples.Last();
		for (const FQuickSDFStrokeSample& SourceSample : QuickLineSourceSamples)
		{
			CurveSamples.Add(TransformQuickLineSample(SourceSample, SourceStart, SourceEnd, StartSample, EndSample));
		}

		StampQuickLineResampledSamples(CurveSamples);
		return;
	}

	UTextureRenderTarget2D* RT = GetActiveRenderTarget();
	if (!RT) return;

	const double Spacing = FMath::Max(GetCurrentStrokeSpacing(RT), 1.0);
	const double SegmentLength = GetSamplePixelDistance(StartSample, EndSample, RT);
	const int32 StepCount = FMath::Max(FMath::CeilToInt(SegmentLength / Spacing), 1);

	TArray<FQuickSDFStrokeSample> LineSamples;
	LineSamples.Reserve(StepCount + 1);
	for (int32 Index = 0; Index <= StepCount; ++Index)
	{
		const float Alpha = static_cast<float>(Index) / static_cast<float>(StepCount);
		FQuickSDFStrokeSample Sample = LerpStrokeSample(StartSample, EndSample, Alpha);
		LineSamples.Add(Sample);
	}

	StampSamples(LineSamples);
}

void UQuickSDFPaintTool::StampQuickLineSurfaceSegment(const FQuickSDFStrokeSample& StartSample, const FQuickSDFStrokeSample& EndSample, bool bForce)
{
	if (ShouldUseSurfaceSpacePaint())
	{
		const TArray<int32> PaintTargetAngleIndices = GetPaintTargetAngleIndices();
		if (Properties && PaintTargetAngleIndices.Num() > 1 && !bStampingAllPaintTargets)
		{
			const int32 PreviousEditAngleIndex = Properties->EditAngleIndex;
			bStampingAllPaintTargets = true;
			for (int32 AngleIndex : PaintTargetAngleIndices)
			{
				Properties->EditAngleIndex = AngleIndex;
				StampQuickLineSurfaceSegment(StartSample, EndSample, bForce);
			}
			Properties->EditAngleIndex = PreviousEditAngleIndex;
			bStampingAllPaintTargets = false;
			RefreshPreviewMaterial();
			return;
		}

		UTextureRenderTarget2D* RT = GetActiveRenderTarget();
		if (!RT)
		{
			return;
		}

		FIntRect DirtyRect;
		TArray<FQuickSDFStrokeSample> CurveSamples;
		const double SurfaceSnapDistance = FMath::Max(GetEffectiveBrushRadius() * 6.0, 0.001);
		auto AppendUniqueSurfaceSample = [](TArray<FQuickSDFStrokeSample>& Samples, const FQuickSDFStrokeSample& Sample)
		{
			if (Samples.Num() == 0 ||
				FVector3d::DistSquared(Samples.Last().WorldPos, Sample.WorldPos) > 1e-8)
			{
				Samples.Add(Sample);
			}
			else
			{
				Samples.Last() = Sample;
			}
		};
		auto AppendProjectedSurfaceSample = [this, SurfaceSnapDistance, &AppendUniqueSurfaceSample](TArray<FQuickSDFStrokeSample>& Samples, const FQuickSDFStrokeSample& Candidate, bool bAllowRawFallback)
		{
			FQuickSDFStrokeSample ProjectedSample;
			if (ProjectSurfaceStrokeSample(Candidate, SurfaceSnapDistance, ProjectedSample))
			{
				AppendUniqueSurfaceSample(Samples, ProjectedSample);
			}
			else if (bAllowRawFallback)
			{
				AppendUniqueSurfaceSample(Samples, Candidate);
			}
		};

		if (QuickLineSourceSamples.Num() >= 2)
		{
			const FQuickSDFStrokeSample& SourceStart = QuickLineSourceSamples[0];
			const FQuickSDFStrokeSample& SourceEnd = QuickLineSourceSamples.Last();
			CurveSamples.Reserve(QuickLineSourceSamples.Num() + 2);
			for (const FQuickSDFStrokeSample& SourceSample : QuickLineSourceSamples)
			{
				const FQuickSDFStrokeSample Candidate = TransformQuickLineSample(SourceSample, SourceStart, SourceEnd, StartSample, EndSample);
				const bool bIsEndpoint = &SourceSample == &SourceStart || &SourceSample == &SourceEnd;
				AppendProjectedSurfaceSample(CurveSamples, Candidate, bIsEndpoint);
			}
		}

		if (CurveSamples.Num() == 0)
		{
			AppendProjectedSurfaceSample(CurveSamples, StartSample, true);
		}
		AppendProjectedSurfaceSample(CurveSamples, EndSample, true);

		if (CurveSamples.Num() < 2)
		{
			StampSample(EndSample);
			return;
		}

		if (!PaintSurfacePolylineToRenderTarget(RT, CurveSamples, &DirtyRect))
		{
			StampSample(EndSample);
			return;
		}

		if (bStrokeTransactionActive && DirtyRect.Width() > 0 && DirtyRect.Height() > 0)
		{
			AddStrokeDirtyRect(RT, DirtyRect);
		}

		if (!bStampingAllPaintTargets)
		{
			RefreshPreviewMaterial();
		}
		return;
	}

	if (ShouldUseScreenProjectionPaint())
	{
		TArray<FQuickSDFStrokeSample> CurveSamples;
		if (QuickLineSourceSamples.Num() >= 2)
		{
			const FQuickSDFStrokeSample& SourceStart = QuickLineSourceSamples[0];
			const FQuickSDFStrokeSample& SourceEnd = QuickLineSourceSamples.Last();
			CurveSamples.Reserve(QuickLineSourceSamples.Num());
			for (const FQuickSDFStrokeSample& SourceSample : QuickLineSourceSamples)
			{
				CurveSamples.Add(TransformQuickLineSample(SourceSample, SourceStart, SourceEnd, StartSample, EndSample));
			}
		}

		if (CurveSamples.Num() < 2)
		{
			CurveSamples.Reset();
			CurveSamples.Add(StartSample);
			CurveSamples.Add(EndSample);
		}

		StampQuickLineResampledSamples(CurveSamples);
		return;
	}

	if (!CanInterpolateStrokeSamples(StartSample, EndSample))
	{
		TArray<FQuickSDFStrokeSample> EndpointSamples;
		EndpointSamples.Reserve(2);
		EndpointSamples.Add(StartSample);
		if (FVector3d::DistSquared(StartSample.WorldPos, EndSample.WorldPos) > 1e-8)
		{
			EndpointSamples.Add(EndSample);
		}
		StampSamples(EndpointSamples);
		return;
	}

	TArray<FQuickSDFStrokeSample> SourceSegment;
	SourceSegment.Reserve(QuickLineSourceSamples.Num());
	const int32 TargetChartID = StartSample.PaintChartID;
	for (const FQuickSDFStrokeSample& SourceSample : QuickLineSourceSamples)
	{
		if (SourceSample.PaintChartID != TargetChartID)
		{
			if (SourceSegment.Num() >= 2)
			{
				break;
			}
			SourceSegment.Reset();
			continue;
		}

		if (SourceSegment.Num() > 0 && !CanInterpolateStrokeSamples(SourceSegment.Last(), SourceSample))
		{
			if (SourceSegment.Num() >= 2)
			{
				break;
			}
			SourceSegment.Reset();
		}

		SourceSegment.Add(SourceSample);
	}

	if (SourceSegment.Num() < 2)
	{
		SourceSegment.Reset();
		SourceSegment.Add(StartSample);
		SourceSegment.Add(EndSample);
	}

	const FQuickSDFStrokeSample& SourceStart = SourceSegment[0];
	const FQuickSDFStrokeSample& SourceEnd = SourceSegment.Last();
	TArray<FQuickSDFStrokeSample> CurveSamples;
	CurveSamples.Reserve(SourceSegment.Num());
	for (const FQuickSDFStrokeSample& SourceSample : SourceSegment)
	{
		CurveSamples.Add(TransformQuickLineSample(SourceSample, SourceStart, SourceEnd, StartSample, EndSample));
	}

	StampQuickLineResampledSamples(CurveSamples);
}

void UQuickSDFPaintTool::StampQuickLineResampledSamples(const TArray<FQuickSDFStrokeSample>& CurveSamples)
{
	if (CurveSamples.Num() == 0)
	{
		return;
	}

	UTextureRenderTarget2D* RT = GetActiveRenderTarget();
	if (!RT)
	{
		return;
	}

	const double QuickStrokeSpacingScale = ShouldUseAnySurfaceProjectionPaint() ? 1.0 : 0.35;
	const double SpacingPixels = FMath::Max(GetCurrentStrokeSpacing(RT) * QuickStrokeSpacingScale, 0.35);
	const double MinControlSpacingPixels = FMath::Max(SpacingPixels * 0.5, 1.0);
	const int32 MaxResampledSamples = ActiveStrokeInputMode == EQuickSDFStrokeInputMode::TexturePreview ? 512 : 2048;

	TArray<FQuickSDFStrokeSample> ControlSamples;
	ControlSamples.Reserve(CurveSamples.Num());
	for (int32 Index = 0; Index < CurveSamples.Num(); ++Index)
	{
		const FQuickSDFStrokeSample& Sample = CurveSamples[Index];
		const bool bIsLastSample = Index == CurveSamples.Num() - 1;
		bool bShouldAdd = ControlSamples.Num() == 0 || bIsLastSample;
		if (!bShouldAdd)
		{
			const FQuickSDFStrokeSample& LastSample = ControlSamples.Last();
			bShouldAdd = !CanInterpolateStrokeSamples(LastSample, Sample) ||
				GetSamplePixelDistance(LastSample, Sample, RT) >= MinControlSpacingPixels;
		}

		if (bShouldAdd)
		{
			if (ControlSamples.Num() > 0 &&
				CanInterpolateStrokeSamples(ControlSamples.Last(), Sample) &&
				GetSamplePixelDistance(ControlSamples.Last(), Sample, RT) < KINDA_SMALL_NUMBER)
			{
				ControlSamples.Last() = Sample;
			}
			else
			{
				ControlSamples.Add(Sample);
			}
		}
	}

	if (ControlSamples.Num() == 0)
	{
		return;
	}

	double TotalLengthPixels = 0.0;
	for (int32 Index = 1; Index < ControlSamples.Num(); ++Index)
	{
		if (CanInterpolateStrokeSamples(ControlSamples[Index - 1], ControlSamples[Index]))
		{
			TotalLengthPixels += GetSamplePixelDistance(ControlSamples[Index - 1], ControlSamples[Index], RT);
		}
	}
	const double EffectiveSpacingPixels = TotalLengthPixels > 0.0
		? FMath::Max(SpacingPixels, TotalLengthPixels / static_cast<double>(FMath::Max(MaxResampledSamples - 1, 1)))
		: SpacingPixels;

	TArray<FQuickSDFStrokeSample> ResampledSamples;
	ResampledSamples.Reserve(FMath::Min(MaxResampledSamples, FMath::Max(ControlSamples.Num() * 4, 1)));

	auto AddResampledSample = [&ResampledSamples, MaxResampledSamples](const FQuickSDFStrokeSample& Sample)
	{
		if (ResampledSamples.Num() < MaxResampledSamples)
		{
			ResampledSamples.Add(Sample);
		}
		else if (ResampledSamples.Num() > 0)
		{
			ResampledSamples.Last() = Sample;
		}
	};

	auto EvaluateSpline = [this](const FQuickSDFStrokeSample& P0,
		const FQuickSDFStrokeSample& P1,
		const FQuickSDFStrokeSample& P2,
		const FQuickSDFStrokeSample& P3,
		float Alpha) -> FQuickSDFStrokeSample
	{
		const FVector3d WorldTangent1 = (P2.WorldPos - P0.WorldPos) * 0.5;
		const FVector3d WorldTangent2 = (P3.WorldPos - P1.WorldPos) * 0.5;
		const FVector2f UVTangent1 = (P2.UV - P0.UV) * 0.5f;
		const FVector2f UVTangent2 = (P3.UV - P1.UV) * 0.5f;
		const FVector2D ScreenTangent1 = (P2.ScreenPosition - P0.ScreenPosition) * 0.5;
		const FVector2D ScreenTangent2 = (P3.ScreenPosition - P1.ScreenPosition) * 0.5;
		const FVector3d RayOriginTangent1 = (P2.RayOrigin - P0.RayOrigin) * 0.5;
		const FVector3d RayOriginTangent2 = (P3.RayOrigin - P1.RayOrigin) * 0.5;

		FQuickSDFStrokeSample OutSample;
		OutSample.WorldPos = FMath::CubicInterp(P1.WorldPos, WorldTangent1, P2.WorldPos, WorldTangent2, Alpha);
		OutSample.UV = FMath::CubicInterp(P1.UV, UVTangent1, P2.UV, UVTangent2, Alpha);
		OutSample.LocalUVScale = FMath::Max(FMath::CubicInterp(P1.LocalUVScale, (P2.LocalUVScale - P0.LocalUVScale) * 0.5f, P2.LocalUVScale, (P3.LocalUVScale - P1.LocalUVScale) * 0.5f, Alpha), KINDA_SMALL_NUMBER);
		OutSample.TriangleID = Alpha < 0.5f ? P1.TriangleID : P2.TriangleID;
		OutSample.PaintChartID = P1.PaintChartID == P2.PaintChartID ? P1.PaintChartID : INDEX_NONE;
		OutSample.ScreenPosition = FMath::CubicInterp(P1.ScreenPosition, ScreenTangent1, P2.ScreenPosition, ScreenTangent2, Alpha);
		OutSample.RayOrigin = FMath::CubicInterp(P1.RayOrigin, RayOriginTangent1, P2.RayOrigin, RayOriginTangent2, Alpha);
		OutSample.RayDirection = FMath::Lerp(P1.RayDirection, P2.RayDirection, static_cast<double>(Alpha)).GetSafeNormal();
		return OutSample;
	};

	int32 RunStart = 0;
	while (RunStart < ControlSamples.Num())
	{
		int32 RunEnd = RunStart;
		while (RunEnd + 1 < ControlSamples.Num() && CanInterpolateStrokeSamples(ControlSamples[RunEnd], ControlSamples[RunEnd + 1]))
		{
			++RunEnd;
		}

		AddResampledSample(ControlSamples[RunStart]);
		if (RunEnd == RunStart)
		{
			RunStart = RunEnd + 1;
			continue;
		}

		for (int32 SegmentIndex = RunStart; SegmentIndex < RunEnd; ++SegmentIndex)
		{
			const FQuickSDFStrokeSample& P0 = SegmentIndex > RunStart ? ControlSamples[SegmentIndex - 1] : ControlSamples[SegmentIndex];
			const FQuickSDFStrokeSample& P1 = ControlSamples[SegmentIndex];
			const FQuickSDFStrokeSample& P2 = ControlSamples[SegmentIndex + 1];
			const FQuickSDFStrokeSample& P3 = SegmentIndex + 2 <= RunEnd ? ControlSamples[SegmentIndex + 2] : ControlSamples[SegmentIndex + 1];
			const double SegmentPixels = GetSamplePixelDistance(P1, P2, RT);
			const int32 StepCount = FMath::Max(FMath::CeilToInt(SegmentPixels / EffectiveSpacingPixels), 1);
			for (int32 Step = 1; Step <= StepCount; ++Step)
			{
				const float Alpha = static_cast<float>(Step) / static_cast<float>(StepCount);
				AddResampledSample(EvaluateSpline(P0, P1, P2, P3, Alpha));
			}
		}

		RunStart = RunEnd + 1;
	}

	if (ResampledSamples.Num() > 0)
	{
		StampSamples(ResampledSamples);
	}
}

FQuickSDFStrokeSample UQuickSDFPaintTool::TransformQuickLineSample(
	const FQuickSDFStrokeSample& SourceSample,
	const FQuickSDFStrokeSample& SourceStart,
	const FQuickSDFStrokeSample& SourceEnd,
	const FQuickSDFStrokeSample& TargetStart,
	const FQuickSDFStrokeSample& TargetEnd) const
{
	if (ShouldUseSurfaceSpacePaint())
	{
		const FVector3d SourceAxis = SourceEnd.WorldPos - SourceStart.WorldPos;
		const FVector3d TargetAxis = TargetEnd.WorldPos - TargetStart.WorldPos;
		const double SourceLength = SourceAxis.Size();
		const double TargetLength = TargetAxis.Size();

		FQuickSDFStrokeSample OutSample = SourceSample;
		if (SourceLength <= KINDA_SMALL_NUMBER || TargetLength <= KINDA_SMALL_NUMBER)
		{
			OutSample.WorldPos = SourceSample.WorldPos + (TargetEnd.WorldPos - SourceEnd.WorldPos);
			OutSample.UV = TargetEnd.UV;
			OutSample.LocalUVScale = TargetEnd.LocalUVScale;
			OutSample.TriangleID = TargetEnd.TriangleID;
			OutSample.PaintChartID = TargetEnd.PaintChartID;
			OutSample.ScreenPosition = TargetEnd.ScreenPosition;
			OutSample.RayOrigin = TargetEnd.RayOrigin;
			OutSample.RayDirection = TargetEnd.RayDirection;
			return OutSample;
		}

		const FVector3d SourceUnit = SourceAxis / SourceLength;
		const FVector3d TargetUnit = TargetAxis / TargetLength;
		FVector3d SourceNormal = (-SourceStart.RayDirection - SourceEnd.RayDirection).GetSafeNormal();
		FVector3d TargetNormal = (-TargetStart.RayDirection - TargetEnd.RayDirection).GetSafeNormal();
		if (SourceNormal.IsNearlyZero())
		{
			SourceNormal = TargetNormal;
		}
		if (TargetNormal.IsNearlyZero())
		{
			TargetNormal = SourceNormal;
		}
		FVector3d SourcePerp = FVector3d::CrossProduct(SourceNormal, SourceUnit).GetSafeNormal();
		FVector3d TargetPerp = FVector3d::CrossProduct(TargetNormal, TargetUnit).GetSafeNormal();
		const FVector3d SourceOffset = SourceSample.WorldPos - SourceStart.WorldPos;
		const double Along = FVector3d::DotProduct(SourceOffset, SourceUnit);
		const float AlongAlpha = static_cast<float>(FMath::Clamp(Along / FMath::Max(SourceLength, KINDA_SMALL_NUMBER), 0.0, 1.0));
		if (SourcePerp.IsNearlyZero() || TargetPerp.IsNearlyZero())
		{
			OutSample.WorldPos = FMath::Lerp(TargetStart.WorldPos, TargetEnd.WorldPos, static_cast<double>(AlongAlpha));
		}
		else
		{
			const double AlongWorld = FVector3d::DotProduct(SourceOffset, SourceUnit);
			const double AcrossWorld = FVector3d::DotProduct(SourceOffset, SourcePerp);
			const double Scale = TargetLength / SourceLength;
			OutSample.WorldPos = TargetStart.WorldPos + TargetUnit * (AlongWorld * Scale) + TargetPerp * (AcrossWorld * Scale);
		}

		OutSample.UV = FMath::Lerp(TargetStart.UV, TargetEnd.UV, AlongAlpha);
		OutSample.LocalUVScale = FMath::Lerp(TargetStart.LocalUVScale, TargetEnd.LocalUVScale, AlongAlpha);
		OutSample.TriangleID = AlongAlpha < 0.5f ? TargetStart.TriangleID : TargetEnd.TriangleID;
		OutSample.PaintChartID = TargetStart.PaintChartID == TargetEnd.PaintChartID ? TargetStart.PaintChartID : INDEX_NONE;
		OutSample.ScreenPosition = FMath::Lerp(TargetStart.ScreenPosition, TargetEnd.ScreenPosition, AlongAlpha);
		OutSample.RayOrigin = FMath::Lerp(TargetStart.RayOrigin, TargetEnd.RayOrigin, static_cast<double>(AlongAlpha));
		OutSample.RayDirection = FMath::Lerp(TargetStart.RayDirection, TargetEnd.RayDirection, static_cast<double>(AlongAlpha)).GetSafeNormal();
		return OutSample;
	}

	if (ShouldUseScreenProjectionPaint())
	{
		const FVector2D SourceAxis = SourceEnd.ScreenPosition - SourceStart.ScreenPosition;
		const FVector2D TargetAxis = TargetEnd.ScreenPosition - TargetStart.ScreenPosition;
		const double SourceLength = SourceAxis.Size();
		const double TargetLength = TargetAxis.Size();

		FQuickSDFStrokeSample OutSample = SourceSample;
		if (SourceLength <= KINDA_SMALL_NUMBER || TargetLength <= KINDA_SMALL_NUMBER)
		{
			OutSample.ScreenPosition = SourceSample.ScreenPosition + (TargetEnd.ScreenPosition - SourceEnd.ScreenPosition);
			OutSample.WorldPos = TargetEnd.WorldPos;
			OutSample.UV = TargetEnd.UV;
			OutSample.LocalUVScale = TargetEnd.LocalUVScale;
			OutSample.TriangleID = TargetEnd.TriangleID;
			OutSample.PaintChartID = TargetEnd.PaintChartID;
			OutSample.RayOrigin = TargetEnd.RayOrigin;
			OutSample.RayDirection = TargetEnd.RayDirection;
			return OutSample;
		}

		const FVector2D SourceUnit = SourceAxis / SourceLength;
		const FVector2D SourcePerp(-SourceUnit.Y, SourceUnit.X);
		const FVector2D TargetUnit = TargetAxis / TargetLength;
		const FVector2D TargetPerp(-TargetUnit.Y, TargetUnit.X);
		const FVector2D SourceOffset = SourceSample.ScreenPosition - SourceStart.ScreenPosition;
		const double Along = FVector2D::DotProduct(SourceOffset, SourceUnit);
		const double Across = FVector2D::DotProduct(SourceOffset, SourcePerp);
		const double Scale = TargetLength / SourceLength;
		const float AlongAlpha = static_cast<float>(FMath::Clamp(Along / FMath::Max(SourceLength, KINDA_SMALL_NUMBER), 0.0, 1.0));

		OutSample.ScreenPosition = TargetStart.ScreenPosition + TargetUnit * (Along * Scale) + TargetPerp * (Across * Scale);
		OutSample.WorldPos = FMath::Lerp(TargetStart.WorldPos, TargetEnd.WorldPos, static_cast<double>(AlongAlpha));
		if (bHasActiveScreenProjectionPaintParams)
		{
			const FQuickSDFScreenProjectionPaintParams& Params = ActiveScreenProjectionPaintParams;
			const double StartDepth = FVector3d::DotProduct(TargetStart.WorldPos - Params.ViewOrigin, Params.ViewForward);
			const double EndDepth = FVector3d::DotProduct(TargetEnd.WorldPos - Params.ViewOrigin, Params.ViewForward);
			const double ViewDepth = FMath::Lerp(StartDepth, EndDepth, static_cast<double>(AlongAlpha));
			const double ScreenX = OutSample.ScreenPosition.X - static_cast<double>(Params.ScreenOffset.X);
			const double ScreenY = OutSample.ScreenPosition.Y - static_cast<double>(Params.ScreenOffset.Y);
			const double ViewX = Params.bOrthographic
				? ScreenX / FMath::Max(static_cast<double>(Params.ProjectionScale.X), KINDA_SMALL_NUMBER)
				: (ScreenX / FMath::Max(static_cast<double>(Params.ProjectionScale.X), KINDA_SMALL_NUMBER)) * ViewDepth;
			const double ViewY = Params.bOrthographic
				? -ScreenY / FMath::Max(static_cast<double>(Params.ProjectionScale.Y), KINDA_SMALL_NUMBER)
				: (-ScreenY / FMath::Max(static_cast<double>(Params.ProjectionScale.Y), KINDA_SMALL_NUMBER)) * ViewDepth;
			OutSample.WorldPos =
				Params.ViewOrigin +
				Params.ViewForward * ViewDepth +
				Params.ViewRight * ViewX +
				Params.ViewUp * ViewY;
		}
		OutSample.UV = FMath::Lerp(TargetStart.UV, TargetEnd.UV, AlongAlpha);
		OutSample.LocalUVScale = FMath::Lerp(TargetStart.LocalUVScale, TargetEnd.LocalUVScale, AlongAlpha);
		OutSample.TriangleID = AlongAlpha < 0.5f ? TargetStart.TriangleID : TargetEnd.TriangleID;
		OutSample.PaintChartID = INDEX_NONE;
		OutSample.RayOrigin = FMath::Lerp(TargetStart.RayOrigin, TargetEnd.RayOrigin, static_cast<double>(AlongAlpha));
		OutSample.RayDirection = FMath::Lerp(TargetStart.RayDirection, TargetEnd.RayDirection, static_cast<double>(AlongAlpha)).GetSafeNormal();
		return OutSample;
	}

	const FVector2f SourceAxis = SourceEnd.UV - SourceStart.UV;
	const FVector2f TargetAxis = TargetEnd.UV - TargetStart.UV;
	const float SourceLength = SourceAxis.Size();
	const float TargetLength = TargetAxis.Size();

	FQuickSDFStrokeSample OutSample = SourceSample;
	if (SourceLength <= KINDA_SMALL_NUMBER || TargetLength <= KINDA_SMALL_NUMBER)
	{
		const FVector2f UVOffset = TargetEnd.UV - SourceEnd.UV;
		OutSample.UV = SourceSample.UV + UVOffset;
		OutSample.WorldPos = SourceSample.WorldPos + (TargetEnd.WorldPos - SourceEnd.WorldPos);
		OutSample.LocalUVScale = TargetEnd.LocalUVScale;
		OutSample.TriangleID = TargetEnd.TriangleID;
		OutSample.PaintChartID = TargetEnd.PaintChartID;
		OutSample.ScreenPosition = TargetEnd.ScreenPosition;
		OutSample.RayOrigin = TargetEnd.RayOrigin;
		OutSample.RayDirection = TargetEnd.RayDirection;
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
	const float AlongAlpha = FMath::Clamp(Along / FMath::Max(SourceLength, KINDA_SMALL_NUMBER), 0.0f, 1.0f);

	OutSample.UV = TargetStart.UV + TargetUnit * (Along * Scale) + TargetPerp * (Across * Scale);
	OutSample.WorldPos = FMath::Lerp(TargetStart.WorldPos, TargetEnd.WorldPos, static_cast<double>(AlongAlpha));
	OutSample.LocalUVScale = FMath::Lerp(TargetStart.LocalUVScale, TargetEnd.LocalUVScale, AlongAlpha);
	OutSample.TriangleID = AlongAlpha < 0.5f ? TargetStart.TriangleID : TargetEnd.TriangleID;
	OutSample.PaintChartID = TargetStart.PaintChartID == TargetEnd.PaintChartID ? TargetStart.PaintChartID : INDEX_NONE;
	OutSample.ScreenPosition = FMath::Lerp(TargetStart.ScreenPosition, TargetEnd.ScreenPosition, AlongAlpha);
	OutSample.RayOrigin = FMath::Lerp(TargetStart.RayOrigin, TargetEnd.RayOrigin, static_cast<double>(AlongAlpha));
	OutSample.RayDirection = FMath::Lerp(TargetStart.RayDirection, TargetEnd.RayDirection, static_cast<double>(AlongAlpha)).GetSafeNormal();
	return OutSample;
}

void UQuickSDFPaintTool::BeginBrushResizeMode()
{
	if (!BrushProperties) return;
	if (bAdjustingBrushRadius) return;
	if (!IsCursorOverLevelViewport()) return;
	const FVector2D CurrentCursorPosition = FSlateApplication::Get().GetCursorPos();
	bAdjustingBrushRadius = true;
	LastInputScreenPosition = CurrentCursorPosition;
	BrushResizeStartScreenPosition = CurrentCursorPosition;
	BrushResizeStartAbsolutePosition = CurrentCursorPosition;
	BrushResizeStartStamp = LastBrushStamp;
	BrushResizeStartRadius = Properties && GetMeshPaintMode() == EQuickSDFMeshPaintMode::ScreenProjection
		? GetScreenProjectionBrushRadiusPixels()
		: BrushProperties->BrushRadius;
	bBrushResizeHadVisibleStamp = BrushStampIndicator && BrushStampIndicator->bVisible;
	if (BrushStampIndicator && bBrushResizeHadVisibleStamp)
	{
		BrushStampIndicator->bVisible = true;
	}
}

void UQuickSDFPaintTool::UpdateBrushResizeFromCursor()
{
	if (!bAdjustingBrushRadius || !BrushProperties) return;
	LastInputScreenPosition = FSlateApplication::Get().GetCursorPos();
	const FVector2D Delta = ConvertInputScreenToCanvasSpace(LastInputScreenPosition) - ConvertInputScreenToCanvasSpace(BrushResizeStartScreenPosition);
	const bool bResizeScreenProjectionBrush = Properties && GetMeshPaintMode() == EQuickSDFMeshPaintMode::ScreenProjection;
	const float NewRadius = FMath::Max(bResizeScreenProjectionBrush ? 1.0f : 0.1f, BrushResizeStartRadius + (Delta.X * FMath::Max(BrushResizeSensitivity, QuickSDFMinResizeSensitivity)));
	if (bResizeScreenProjectionBrush)
	{
		if (!Properties || FMath::IsNearlyEqual(Properties->ScreenProjectionBrushRadiusPixels, NewRadius, KINDA_SMALL_NUMBER))
		{
			return;
		}
		Properties->ScreenProjectionBrushRadiusPixels = NewRadius;
		LastBrushStamp.Radius = NewRadius;
		NotifyOfPropertyChangeByTool(Properties);
		return;
	}

	if (FMath::IsNearlyEqual(BrushProperties->BrushRadius, NewRadius, KINDA_SMALL_NUMBER))
	{
		return;
	}
	const float RangeMin = BrushRelativeSizeRange.Min;
	const float RangeSize = BrushRelativeSizeRange.Max - BrushRelativeSizeRange.Min;
	if (RangeSize > KINDA_SMALL_NUMBER)
	{
		BrushProperties->BrushSize = FMath::Clamp((NewRadius - RangeMin) / RangeSize, 0.0f, 1.0f);
	}
	BrushProperties->BrushRadius = NewRadius;
	RecalculateBrushRadius();
	BrushProperties->BrushSize = BrushProperties->BrushRadius;
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

		const bool bUseTextureSetFilter = Properties && Properties->TargetMaterialSlot >= 0;
		if (!bUseTextureSetFilter && bComponentHit)
		{
			OutHit = ComponentHit;
			return true;
		}

		if (TargetMeshSpatial.IsValid() && TargetMesh.IsValid())
		{
			const FTransform Transform = CurrentComponent->GetComponentTransform();
			const FRay LocalRay(Transform.InverseTransformPosition(Ray.Origin), Transform.InverseTransformVector(Ray.Direction));
			const bool bPaintThroughContext = Properties && Properties->bPaintThroughNonTargetGeometry && Properties->TargetMaterialSlot >= 0;
			UE::Geometry::IMeshSpatial::FQueryOptions QueryOptions(100000.0, [this, bPaintThroughContext](int32 TriangleID)
			{
				return bPaintThroughContext ? IsTriangleInTargetMaterialSlot(TriangleID) : true;
			});

			double HitDistance = 100000.0;
			int32 HitTID = INDEX_NONE;
			FVector3d BaryCoords(0.0, 0.0, 0.0);
			if (TargetMeshSpatial->FindNearestHitTriangle(LocalRay, HitDistance, HitTID, BaryCoords, QueryOptions) &&
				HitTID != INDEX_NONE)
			{
				if (!bPaintThroughContext && !IsTriangleInTargetMaterialSlot(HitTID))
				{
					return false;
				}

				const FVector LocalHitPosition = (FVector)LocalRay.PointAt(HitDistance);
				const FVector LocalNormal = (FVector)TargetMesh->GetTriNormal(HitTID);
				FVector WorldNormal = Transform.TransformVectorNoScale(LocalNormal).GetSafeNormal();
				if (FVector::DotProduct(WorldNormal, Ray.Direction) > 0.0)
				{
					WorldNormal *= -1.0;
				}

				OutHit.Component = CurrentComponent.Get();
				OutHit.Location = Transform.TransformPosition(LocalHitPosition);
				OutHit.ImpactPoint = OutHit.Location;
				OutHit.Normal = WorldNormal;
				OutHit.ImpactNormal = WorldNormal;
				OutHit.FaceIndex = HitTID;
				OutHit.Distance = FVector::Distance(Ray.Origin, OutHit.Location);
				OutHit.bBlockingHit = true;

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
		LastBrushStamp.Radius = GetEffectiveBrushRadius();
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
		LastBrushStamp.Radius = GetEffectiveBrushRadius();
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
			BrushStampIndicator->bVisible = !ShouldUseScreenProjectionPaint();
		}
		UpdateBrushStampIndicator();
		if (ShouldUseScreenProjectionPaint())
		{
			InitializeScreenProjectionPaintFrame(StartSample);
		}
		BeginStrokeTransaction(); 
		FilteredStrokeSample = StartSample;
		bHasFilteredStrokeSample = true;
		LastRawStrokeSample = StartSample;
		bHasLastRawStrokeSample = true;
		PointBuffer.Add(StartSample);
		QuickLineSourceSamples.Reset();
		QuickLineSourceSamples.Add(StartSample);
		QuickLineStartSample = StartSample;
		QuickLineEndSample = StartSample;
		bHasQuickLineStartSample = true;
		bHasQuickLineEndSample = true;
		QuickLineHoldScreenPosition = LastInputScreenPosition;
		QuickLineLastMoveTime = GetToolCurrentTime();
		StampSample(StartSample);
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
		LastRawStrokeSample = Sample;
		bHasLastRawStrokeSample = true;
		QuickLineEndSample = Sample;
		bHasQuickLineEndSample = true;
		UpdateQuickLineHoldState(LastInputScreenPosition);
		if (bQuickLineActive)
		{
			RedrawQuickLinePreview();
			return;
		}
		const FQuickSDFStrokeSample StabilizedSample = SmoothStrokeSample(Sample);
		QuickLineEndSample = StabilizedSample;
		if (QuickLineSourceSamples.Num() == 0 ||
			!CanInterpolateStrokeSamples(QuickLineSourceSamples.Last(), StabilizedSample))
		{
			QuickLineSourceSamples.Add(StabilizedSample);
		}
		else
		{
			UTextureRenderTarget2D* RT = GetActiveRenderTarget();
			const double MinQuickLineSourceSpacing = RT
				? (ShouldUseProjectedSurfacePaint()
					? FMath::Max(GetCurrentStrokeSpacing(RT) * 0.5, GetEffectiveBrushRadius() * 0.05)
					: FMath::Max(GetCurrentStrokeSpacing(RT) * 0.5, 1.0))
				: 0.0;
			if (!RT ||
				GetSamplePixelDistance(QuickLineSourceSamples.Last(), StabilizedSample, RT) >= MinQuickLineSourceSpacing)
			{
				QuickLineSourceSamples.Add(StabilizedSample);
			}
		}
		AppendStrokeSample(StabilizedSample);
	}
}

void UQuickSDFPaintTool::OnEndDrag(const FRay& Ray)
{
	if (bQuickLineActive)
	{
		RedrawQuickLinePreview(true);
		EndStrokeTransaction();
		PointBuffer.Empty();
		ResetStrokeState();
		return;
	}

	if (ShouldUseAnySurfaceProjectionPaint())
	{
		EndStrokeTransaction();
		PointBuffer.Empty();
		ResetStrokeState();
		return;
	}

	if (Properties && Properties->bEnableStrokeStabilizer && bHasLastRawStrokeSample)
	{
		AppendStrokeSample(LastRawStrokeSample);
	}

	FlushStrokeTail();

	EndStrokeTransaction();
	PointBuffer.Empty();
	ResetStrokeState();
}

bool UQuickSDFPaintTool::TryMakeStrokeSample(const FRay& Ray, FQuickSDFStrokeSample& OutSample)
{
	if (!TargetMeshSpatial.IsValid() || !TargetMesh.IsValid() || !Properties || !CurrentComponent.IsValid()) return false;
	
	const FTransform Transform = CurrentComponent->GetComponentTransform();
	const FRay LocalRay(Transform.InverseTransformPosition(Ray.Origin), Transform.InverseTransformVector(Ray.Direction));
	const bool bPaintThroughContext = Properties->bPaintThroughNonTargetGeometry && Properties->TargetMaterialSlot >= 0;
	UE::Geometry::IMeshSpatial::FQueryOptions QueryOptions(100000.0, [this, bPaintThroughContext](int32 TriangleID)
	{
		return bPaintThroughContext ? IsTriangleInTargetMaterialSlot(TriangleID) : true;
	});

	double HitDistance = 100000.0;
	int32 HitTID = -1;
	FVector3d BaryCoords(0.0, 0.0, 0.0);
	const bool bHit = TargetMeshSpatial->FindNearestHitTriangle(LocalRay, HitDistance, HitTID, BaryCoords, QueryOptions);

	if (!bHit || HitTID < 0) return false;
	if (!bPaintThroughContext && !IsTriangleInTargetMaterialSlot(HitTID)) return false;

	UE::Geometry::FIndex3i TriV = TargetMesh->GetTriangle(HitTID);

	// 2. 繧､繝ｳ繝・ャ繧ｯ繧ｹ縺九ｉ繝ｭ繝ｼ繧ｫ繝ｫ鬆らせ蠎ｧ讓吶ｒ蜿門ｾ励＠縲√Ρ繝ｼ繝ｫ繝牙ｺｧ讓吶↓螟画鋤
	FVector3d V0 = Transform.TransformPosition(TargetMesh->GetVertex(TriV.A));
	FVector3d V1 = Transform.TransformPosition(TargetMesh->GetVertex(TriV.B));
	FVector3d V2 = Transform.TransformPosition(TargetMesh->GetVertex(TriV.C));

	// UV蠎ｧ讓吶ｒ蜿門ｾ・
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
	OutSample.TriangleID = HitTID;
	OutSample.PaintChartID = GetPaintChartIDForTriangle(HitTID);
	OutSample.ScreenPosition = LastInputScreenPosition;
	OutSample.RayOrigin = Ray.Origin;
	OutSample.RayDirection = Ray.Direction.GetSafeNormal();
	return true;
}

bool UQuickSDFPaintTool::ProjectSurfaceStrokeSample(const FQuickSDFStrokeSample& Sample, double MaxWorldDistance, FQuickSDFStrokeSample& OutSample)
{
	if (!TargetMeshSpatial.IsValid() || !TargetMesh.IsValid() || !Properties || !CurrentComponent.IsValid() || !TargetMesh->HasAttributes())
	{
		return false;
	}

	const UE::Geometry::FDynamicMeshUVOverlay* UVOverlay = TargetMesh->Attributes()->GetUVLayer(Properties->UVChannel);
	if (!UVOverlay)
	{
		return false;
	}

	const FTransform Transform = CurrentComponent->GetComponentTransform();
	const FVector Scale = Transform.GetScale3D().GetAbs();
	const double MinScale = FMath::Max(static_cast<double>(FMath::Min3(Scale.X, Scale.Y, Scale.Z)), KINDA_SMALL_NUMBER);
	const double LocalMaxDistance = MaxWorldDistance > 0.0 ? MaxWorldDistance / MinScale : 100000.0;
	const FVector3d LocalPoint = Transform.InverseTransformPosition(Sample.WorldPos);

	UE::Geometry::IMeshSpatial::FQueryOptions QueryOptions(LocalMaxDistance, [this](int32 TriangleID)
	{
		return IsTriangleInTargetMaterialSlot(TriangleID);
	});

	double NearestDistSqr = FMath::Square(LocalMaxDistance);
	const int32 HitTID = TargetMeshSpatial->FindNearestTriangle(LocalPoint, NearestDistSqr, QueryOptions);
	if (HitTID == INDEX_NONE || !TargetMesh->IsTriangle(HitTID) || !UVOverlay->IsSetTriangle(HitTID))
	{
		return false;
	}

	const UE::Geometry::FIndex3i Tri = TargetMesh->GetTriangle(HitTID);
	const FVector3d L0 = TargetMesh->GetVertex(Tri.A);
	const FVector3d L1 = TargetMesh->GetVertex(Tri.B);
	const FVector3d L2 = TargetMesh->GetVertex(Tri.C);
	const FVector ClosestLocal = FMath::ClosestPointOnTriangleToPoint(FVector(LocalPoint), FVector(L0), FVector(L1), FVector(L2));

	FVector BaryCoords;
	if (!FMath::ComputeBarycentricTri(ClosestLocal, FVector(L0), FVector(L1), FVector(L2), BaryCoords))
	{
		return false;
	}

	const UE::Geometry::FIndex3i TriUVs = UVOverlay->GetTriangle(HitTID);
	const FVector2f UV0 = UVOverlay->GetElement(TriUVs.A);
	const FVector2f UV1 = UVOverlay->GetElement(TriUVs.B);
	const FVector2f UV2 = UVOverlay->GetElement(TriUVs.C);

	const FVector3d W0 = Transform.TransformPosition(L0);
	const FVector3d W1 = Transform.TransformPosition(L1);
	const FVector3d W2 = Transform.TransformPosition(L2);

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

	AccumulateUVScale(W0, W1, UV0, UV1);
	AccumulateUVScale(W1, W2, UV1, UV2);
	AccumulateUVScale(W2, W0, UV2, UV0);

	OutSample = Sample;
	OutSample.WorldPos = Transform.TransformPosition(FVector3d(ClosestLocal));
	OutSample.UV = UV0 * static_cast<float>(BaryCoords.X) +
		UV1 * static_cast<float>(BaryCoords.Y) +
		UV2 * static_cast<float>(BaryCoords.Z);
	OutSample.LocalUVScale = ScaleCount > 0
		? static_cast<float>(ScaleSum / static_cast<double>(ScaleCount))
		: 1.0f / FMath::Max(static_cast<float>(TargetMesh->GetBounds().MaxDim()), KINDA_SMALL_NUMBER);
	OutSample.TriangleID = HitTID;
	OutSample.PaintChartID = GetPaintChartIDForTriangle(HitTID);
	return true;
}

bool UQuickSDFPaintTool::TryMakePreviewStrokeSample(const FVector2D& ScreenPosition, FQuickSDFStrokeSample& OutSample) const
{
	UTextureRenderTarget2D* RT = GetActiveRenderTarget();
	if (!RT || !IsInPreviewBounds(ScreenPosition)) return false;

	OutSample.UV = ScreenToPreviewUV(ScreenPosition);
	OutSample.WorldPos = FVector3d(OutSample.UV.X * RT->SizeX, OutSample.UV.Y * RT->SizeY, 0.0);
	OutSample.ScreenPosition = ScreenPosition;
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
	if (!RT) return;

	if (ShouldUseAnySurfaceProjectionPaint())
	{
		FIntRect BatchDirtyRect;
		bool bHasBatchDirtyRect = false;
		if (ShouldUseScreenProjectionPaint())
		{
			if (PaintScreenProjectionStrokeToRenderTarget(RT, Samples, &BatchDirtyRect) &&
				BatchDirtyRect.Width() > 0 && BatchDirtyRect.Height() > 0)
			{
				bHasBatchDirtyRect = true;
			}
		}
		else if (Samples.Num() > 1)
		{
			if (PaintSurfacePolylineToRenderTarget(RT, Samples, &BatchDirtyRect) &&
				BatchDirtyRect.Width() > 0 && BatchDirtyRect.Height() > 0)
			{
				bHasBatchDirtyRect = true;
			}
		}
		else
		{
			for (const FQuickSDFStrokeSample& Sample : Samples)
			{
				FIntRect SampleDirtyRect;
				if (!PaintSurfaceBrushToRenderTarget(RT, Sample, &SampleDirtyRect) || SampleDirtyRect.Width() <= 0 || SampleDirtyRect.Height() <= 0)
				{
					continue;
				}

				if (!bHasBatchDirtyRect)
				{
					BatchDirtyRect = SampleDirtyRect;
					bHasBatchDirtyRect = true;
				}
				else
				{
					BatchDirtyRect.Min.X = FMath::Min(BatchDirtyRect.Min.X, SampleDirtyRect.Min.X);
					BatchDirtyRect.Min.Y = FMath::Min(BatchDirtyRect.Min.Y, SampleDirtyRect.Min.Y);
					BatchDirtyRect.Max.X = FMath::Max(BatchDirtyRect.Max.X, SampleDirtyRect.Max.X);
					BatchDirtyRect.Max.Y = FMath::Max(BatchDirtyRect.Max.Y, SampleDirtyRect.Max.Y);
				}
			}
		}

		if (bStrokeTransactionActive && bHasBatchDirtyRect)
		{
			AddStrokeDirtyRect(RT, BatchDirtyRect);
		}

		if (!bStampingAllPaintTargets)
		{
			RefreshPreviewMaterial();
		}
		return;
	}

	const FVector2D RTSize(RT->SizeX, RT->SizeY);
	
	TArray<FVector2D> PixelSizes;
	PixelSizes.Reserve(Samples.Num());
	for (const FQuickSDFStrokeSample& Sample : Samples)
	{
		FVector2D PixelSize;
		
		if (ActiveStrokeInputMode == EQuickSDFStrokeInputMode::TexturePreview)
		{
			// 繝励Ξ繝薙Η繝ｼ陦ｨ遉ｺ・・D・峨・蝣ｴ蜷医・莉･蜑阪・蝗ｺ螳夊ｨ育ｮ・
			PixelSize = GetBrushPixelSize(RT);
		}
		else
		{
			const float BrushRadiusWorld = static_cast<float>(GetEffectiveBrushRadius());
			const float UVRadius = BrushRadiusWorld * FMath::Max(Sample.LocalUVScale, KINDA_SMALL_NUMBER);
			PixelSize = FVector2D(UVRadius * RTSize.X * 2.0f, UVRadius * RTSize.Y * 2.0f);
		}
		PixelSizes.Add(PixelSize);
	}

	FIntRect BatchDirtyRect;
	if (!PaintUVBrushesToRenderTarget(RT, Samples, PixelSizes, &BatchDirtyRect))
	{
		return;
	}

	if (bStrokeTransactionActive && BatchDirtyRect.Width() > 0 && BatchDirtyRect.Height() > 0)
	{
		AddStrokeDirtyRect(RT, BatchDirtyRect);
	}

	if (!bStampingAllPaintTargets)
	{
		RefreshPreviewMaterial();
	}
}

void UQuickSDFPaintTool::AppendStrokeSample(const FQuickSDFStrokeSample& Sample)
{
	if (PointBuffer.Num() > 0)
	{
		if (!CanInterpolateStrokeSamples(PointBuffer.Last(), Sample))
		{
			PointBuffer.Reset();
			AccumulatedDistance = 0.0;
		}
	}

	if (PointBuffer.Num() > 0)
	{
		const bool bDuplicateSample = ShouldUseProjectedSurfacePaint()
			? FVector3d::DistSquared(PointBuffer.Last().WorldPos, Sample.WorldPos) < 1e-8
			: (ShouldUseScreenProjectionPaint()
				? FVector2D::DistSquared(PointBuffer.Last().ScreenPosition, Sample.ScreenPosition) < 1e-8
				: FVector2f::DistSquared(PointBuffer.Last().UV, Sample.UV) < 1e-12f);
		if (bDuplicateSample)
		{
			return;
		}
	}
	
	PointBuffer.Add(Sample);
	
	if (PointBuffer.Num() == 1)
	{
		StampSample(Sample);
	}
	else if (PointBuffer.Num() < 4)
	{
		const int32 L = PointBuffer.Num();
		StampLinearSegment(PointBuffer[L - 2], PointBuffer[L - 1]);
	}
	else {
		int32 L = PointBuffer.Num();
		StampInterpolatedSegment(PointBuffer[L-4], PointBuffer[L-3], PointBuffer[L-2], PointBuffer[L-1]);
		
		if (PointBuffer.Num() > 4) {
			PointBuffer.RemoveAt(0);
		}
	}
}

void UQuickSDFPaintTool::StampLinearSegment(const FQuickSDFStrokeSample& StartSample, const FQuickSDFStrokeSample& EndSample)
{
	if (!CanInterpolateStrokeSamples(StartSample, EndSample))
	{
		StampSample(EndSample);
		return;
	}

	UTextureRenderTarget2D* RT = GetActiveRenderTarget();
	if (!RT) return;

	if (ShouldUseSurfaceSpacePaint())
	{
		const double SurfaceSnapDistance = FMath::Max(GetEffectiveBrushRadius() * 2.0, 0.001);
		const double MaxProjectedStep = FMath::Max(GetEffectiveBrushRadius() * 2.0, 0.001);
		const double SegmentLengthWorld = FVector3d::Distance(StartSample.WorldPos, EndSample.WorldPos);
		const int32 StepCount = FMath::Clamp(FMath::CeilToInt(SegmentLengthWorld / MaxProjectedStep), 1, 128);
		TArray<FQuickSDFStrokeSample> SegmentSamples;
		SegmentSamples.Reserve(StepCount + 1);
		auto AddProjectedSegmentSample = [this, SurfaceSnapDistance, &SegmentSamples](const FQuickSDFStrokeSample& Candidate)
		{
			FQuickSDFStrokeSample ProjectedSample;
			if (!ProjectSurfaceStrokeSample(Candidate, SurfaceSnapDistance, ProjectedSample))
			{
				ProjectedSample = Candidate;
			}
			if (SegmentSamples.Num() == 0 ||
				FVector3d::DistSquared(SegmentSamples.Last().WorldPos, ProjectedSample.WorldPos) > 1e-8)
			{
				SegmentSamples.Add(ProjectedSample);
			}
			else
			{
				SegmentSamples.Last() = ProjectedSample;
			}
		};

		for (int32 Step = 0; Step <= StepCount; ++Step)
		{
			const double Alpha = static_cast<double>(Step) / static_cast<double>(StepCount);
			AddProjectedSegmentSample(LerpStrokeSample(StartSample, EndSample, Alpha));
		}
		StampSamples(SegmentSamples);
		return;
	}

	const double Spacing = GetCurrentStrokeSpacing(RT);
	if (Spacing <= KINDA_SMALL_NUMBER) return;

	const double SegmentLength = GetSamplePixelDistance(StartSample, EndSample, RT);
	if (SegmentLength <= KINDA_SMALL_NUMBER) return;

	const int32 StepCount = FMath::Clamp(FMath::CeilToInt(SegmentLength / FMath::Max(Spacing * 0.25, 0.25)), 1, 1000);
	const double SurfaceSnapDistance = ShouldUseSurfaceSpacePaint() ? FMath::Max(GetEffectiveBrushRadius() * 2.0, 0.001) : 0.0;
	TArray<FQuickSDFStrokeSample> Batch;
	FQuickSDFStrokeSample Prev = StartSample;

	for (int32 Step = 1; Step <= StepCount; ++Step)
	{
		const double Alpha = static_cast<double>(Step) / static_cast<double>(StepCount);
		FQuickSDFStrokeSample Curr = LerpStrokeSample(StartSample, EndSample, Alpha);
		const double Dist = GetSamplePixelDistance(Prev, Curr, RT);
		AccumulatedDistance += Dist;

		while (AccumulatedDistance >= Spacing)
		{
			const double Ratio = 1.0 - ((AccumulatedDistance - Spacing) / FMath::Max(Dist, 0.0001));
			FQuickSDFStrokeSample StampSample = LerpStrokeSample(Prev, Curr, Ratio);
			if (ShouldUseSurfaceSpacePaint())
			{
				FQuickSDFStrokeSample ProjectedSample;
				if (ProjectSurfaceStrokeSample(StampSample, SurfaceSnapDistance, ProjectedSample))
				{
					StampSample = ProjectedSample;
				}
				else
				{
					AccumulatedDistance -= Spacing;
					continue;
				}
			}
			Batch.Add(StampSample);
			AccumulatedDistance -= Spacing;
		}

		Prev = Curr;
	}

	StampSamples(Batch);
}

void UQuickSDFPaintTool::FlushStrokeTail()
{
	if (PointBuffer.Num() >= 2)
	{
		const int32 L = PointBuffer.Num();
		StampLinearSegment(PointBuffer[L - 2], PointBuffer[L - 1]);
	}
}

void UQuickSDFPaintTool::StampInterpolatedSegment(
    const FQuickSDFStrokeSample& P0,
    const FQuickSDFStrokeSample& P1,
    const FQuickSDFStrokeSample& P2,
    const FQuickSDFStrokeSample& P3)
{
	if (!CanInterpolateStrokeSamples(P0, P1) ||
		!CanInterpolateStrokeSamples(P1, P2) ||
		!CanInterpolateStrokeSamples(P2, P3))
	{
		StampLinearSegment(P2, P3);
		return;
	}

    UTextureRenderTarget2D* RT = GetActiveRenderTarget();
    if (!RT) return;

    const double Spacing = GetCurrentStrokeSpacing(RT);
    if (Spacing <= KINDA_SMALL_NUMBER) return;
    
    const double Alpha = 0.5; 
    auto GetT = [this, Alpha, RT](double t, const FQuickSDFStrokeSample& A, const FQuickSDFStrokeSample& B) {
        double d = FMath::Square(GetSamplePixelDistance(A, B, RT));
        return FMath::Pow(d, Alpha * 0.5) + t;
    };

    double t0 = 0.0;
    double t1 = GetT(t0, P0, P1);
    double t2 = GetT(t1, P1, P2);
    double t3 = GetT(t2, P2, P3);

    if (t1 - t0 < KINDA_SMALL_NUMBER || t2 - t1 < KINDA_SMALL_NUMBER || t3 - t2 < KINDA_SMALL_NUMBER) return;
    
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
		const float SegmentAlpha = static_cast<float>((t - t1) / FMath::Max(t2 - t1, KINDA_SMALL_NUMBER));
		Out.TriangleID = SegmentAlpha < 0.5f ? P1.TriangleID : P2.TriangleID;
		Out.PaintChartID = P1.PaintChartID == P2.PaintChartID ? P1.PaintChartID : INDEX_NONE;
		Out.ScreenPosition = FMath::Lerp(P1.ScreenPosition, P2.ScreenPosition, SegmentAlpha);
		Out.RayOrigin = FMath::Lerp(P1.RayOrigin, P2.RayOrigin, static_cast<double>(SegmentAlpha));
		Out.RayDirection = FMath::Lerp(P1.RayDirection, P2.RayDirection, static_cast<double>(SegmentAlpha)).GetSafeNormal();
    	
        return Out;
    };
	
    const double SegmentLength = GetSamplePixelDistance(P1, P2, RT);

	if (ShouldUseSurfaceSpacePaint())
	{
		const double SurfaceSnapDistance = FMath::Max(GetEffectiveBrushRadius() * 2.0, 0.001);
		const double SplineSpacing = FMath::Max(GetEffectiveBrushRadius() * 0.35, 0.001);
		const int32 StepCount = FMath::Clamp(FMath::CeilToInt(SegmentLength / SplineSpacing), 2, 96);
		TArray<FQuickSDFStrokeSample> SurfaceCurveSamples;
		SurfaceCurveSamples.Reserve(StepCount + 1);

		auto AddProjectedSplineSample = [this, SurfaceSnapDistance, &SurfaceCurveSamples](const FQuickSDFStrokeSample& Candidate)
		{
			FQuickSDFStrokeSample ProjectedSample;
			if (!ProjectSurfaceStrokeSample(Candidate, SurfaceSnapDistance, ProjectedSample))
			{
				ProjectedSample = Candidate;
			}
			if (SurfaceCurveSamples.Num() == 0 ||
				FVector3d::DistSquared(SurfaceCurveSamples.Last().WorldPos, ProjectedSample.WorldPos) > 1e-8)
			{
				SurfaceCurveSamples.Add(ProjectedSample);
			}
			else
			{
				SurfaceCurveSamples.Last() = ProjectedSample;
			}
		};

		for (int32 Step = 0; Step <= StepCount; ++Step)
		{
			const double AlphaT = static_cast<double>(Step) / static_cast<double>(StepCount);
			AddProjectedSplineSample(EvaluateSpline(FMath::Lerp(t1, t2, AlphaT)));
		}

		StampSamples(SurfaceCurveSamples);
		return;
	}
    
    const int32 SubSteps = FMath::Clamp(FMath::CeilToInt(SegmentLength / (Spacing * 0.1)), 20, 1000);
    const double dt = (t2 - t1) / (double)SubSteps;
	const double SurfaceSnapDistance = ShouldUseSurfaceSpacePaint() ? FMath::Max(GetEffectiveBrushRadius() * 2.0, 0.001) : 0.0;

    TArray<FQuickSDFStrokeSample> Batch;
    FQuickSDFStrokeSample Prev = EvaluateSpline(t1);

    for (int32 i = 1; i <= SubSteps; ++i) {
        double currT = t1 + i * dt;
        FQuickSDFStrokeSample Curr = EvaluateSpline(currT);
        
        double Dist = GetSamplePixelDistance(Prev, Curr, RT);
        AccumulatedDistance += Dist;

        while (AccumulatedDistance >= Spacing) {
            double Ratio = 1.0 - ((AccumulatedDistance - Spacing) / FMath::Max(Dist, 0.0001));
            FQuickSDFStrokeSample StampSample = LerpStrokeSample(Prev, Curr, Ratio);
			if (ShouldUseSurfaceSpacePaint())
			{
				FQuickSDFStrokeSample ProjectedSample;
				if (ProjectSurfaceStrokeSample(StampSample, SurfaceSnapDistance, ProjectedSample))
				{
					StampSample = ProjectedSample;
				}
				else
				{
					AccumulatedDistance -= Spacing;
					continue;
				}
			}
            Batch.Add(StampSample);
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
	bHasActiveScreenProjectionPaintParams = false;
	bHasFilteredStrokeSample = false;
	DistanceSinceLastStamp = 0.0;
	ActiveStrokeInputMode = EQuickSDFStrokeInputMode::None;
	PendingStrokeInputMode = EQuickSDFStrokeInputMode::None;
	bQuickLineActive = false;
	bHasQuickLineStartSample = false;
	bHasQuickLineEndSample = false;
	QuickLineStartSample = FQuickSDFStrokeSample();
	QuickLineEndSample = FQuickSDFStrokeSample();
	LastRawStrokeSample = FQuickSDFStrokeSample();
	bHasLastRawStrokeSample = false;
	QuickLineSourceSamples.Reset();
	QuickLineHoldScreenPosition = FVector2D::ZeroVector;
	QuickLineLastMoveTime = 0.0;
	PointBuffer.Reset();
	AccumulatedDistance = 0.0;
}
#undef LOCTEXT_NAMESPACE
