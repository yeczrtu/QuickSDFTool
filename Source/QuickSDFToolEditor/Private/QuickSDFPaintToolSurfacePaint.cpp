#include "QuickSDFPaintTool.h"

#include "QuickSDFProjectedPaintRendering.h"
#include "QuickSDFSurfacePaintRendering.h"
#include "CanvasTypes.h"
#include "DynamicMesh/DynamicMeshAttributeSet.h"
#include "Engine/Canvas.h"
#include "Engine/TextureRenderTarget2D.h"
#include "InteractiveToolManager.h"
#include "RenderingThread.h"
#include "TextureResource.h"

namespace
{
constexpr int32 SurfacePaintDirtyPadding = 4;
constexpr int32 ProjectedPaintResolvePadding = 6;

double GetConservativeLocalRadius(const FTransform& Transform, double WorldRadius)
{
	const FVector Scale = Transform.GetScale3D().GetAbs();
	const double MinScale = FMath::Max(static_cast<double>(FMath::Min3(Scale.X, Scale.Y, Scale.Z)), KINDA_SMALL_NUMBER);
	return WorldRadius / MinScale;
}

FIntRect UnionRects(const FIntRect& A, const FIntRect& B)
{
	return FIntRect(
		FMath::Min(A.Min.X, B.Min.X),
		FMath::Min(A.Min.Y, B.Min.Y),
		FMath::Max(A.Max.X, B.Max.X),
		FMath::Max(A.Max.Y, B.Max.Y));
}

void OffsetUVsIntoPrimaryTile(FVector2D (&UVs)[3])
{
	FVector2D UVMin(TNumericLimits<double>::Max(), TNumericLimits<double>::Max());
	FVector2D UVMax(-TNumericLimits<double>::Max(), -TNumericLimits<double>::Max());
	for (const FVector2D& UV : UVs)
	{
		UVMin.X = FMath::Min(UVMin.X, UV.X);
		UVMin.Y = FMath::Min(UVMin.Y, UV.Y);
		UVMax.X = FMath::Max(UVMax.X, UV.X);
		UVMax.Y = FMath::Max(UVMax.Y, UV.Y);
	}

	FVector2D UVOffset = FVector2D::ZeroVector;
	if (UVMax.X > 1.0)
	{
		UVOffset.X = -FMath::FloorToDouble(UVMin.X);
	}
	else if (UVMin.X < 0.0)
	{
		UVOffset.X = 1.0 + FMath::FloorToDouble(-UVMax.X);
	}

	if (UVMax.Y > 1.0)
	{
		UVOffset.Y = -FMath::FloorToDouble(UVMin.Y);
	}
	else if (UVMin.Y < 0.0)
	{
		UVOffset.Y = 1.0 + FMath::FloorToDouble(-UVMax.Y);
	}

	for (FVector2D& UV : UVs)
	{
		UV += UVOffset;
	}
}

bool ComputeUVBounds(const FVector2D (&UVs)[3], FVector2D& OutMin, FVector2D& OutMax)
{
	OutMin = FVector2D(TNumericLimits<double>::Max(), TNumericLimits<double>::Max());
	OutMax = FVector2D(-TNumericLimits<double>::Max(), -TNumericLimits<double>::Max());
	for (const FVector2D& UV : UVs)
	{
		OutMin.X = FMath::Min(OutMin.X, UV.X);
		OutMin.Y = FMath::Min(OutMin.Y, UV.Y);
		OutMax.X = FMath::Max(OutMax.X, UV.X);
		OutMax.Y = FMath::Max(OutMax.Y, UV.Y);
	}

	return OutMax.X >= 0.0 && OutMax.Y >= 0.0 && OutMin.X <= 1.0 && OutMin.Y <= 1.0;
}

FIntRect MakeDirtyRectFromUVBounds(const FVector2D& UVMin, const FVector2D& UVMax, UTextureRenderTarget2D* RenderTarget)
{
	const int32 MinX = FMath::FloorToInt(FMath::Clamp(UVMin.X, 0.0, 1.0) * RenderTarget->SizeX) - SurfacePaintDirtyPadding;
	const int32 MinY = FMath::FloorToInt(FMath::Clamp(UVMin.Y, 0.0, 1.0) * RenderTarget->SizeY) - SurfacePaintDirtyPadding;
	const int32 MaxX = FMath::CeilToInt(FMath::Clamp(UVMax.X, 0.0, 1.0) * RenderTarget->SizeX) + SurfacePaintDirtyPadding;
	const int32 MaxY = FMath::CeilToInt(FMath::Clamp(UVMax.Y, 0.0, 1.0) * RenderTarget->SizeY) + SurfacePaintDirtyPadding;
	return FIntRect(MinX, MinY, MaxX, MaxY);
}

FIntRect ClampDirtyRectToRenderTarget(const FIntRect& Rect, const UTextureRenderTarget2D* RenderTarget)
{
	if (!RenderTarget)
	{
		return FIntRect();
	}

	return FIntRect(
		FMath::Clamp(Rect.Min.X, 0, RenderTarget->SizeX),
		FMath::Clamp(Rect.Min.Y, 0, RenderTarget->SizeY),
		FMath::Clamp(Rect.Max.X, 0, RenderTarget->SizeX),
		FMath::Clamp(Rect.Max.Y, 0, RenderTarget->SizeY));
}

int32 GetProjectedPaintCoverageScale(const UTextureRenderTarget2D* RenderTarget)
{
	if (!RenderTarget)
	{
		return 1;
	}

	return (RenderTarget->SizeX * 2 <= 4096 && RenderTarget->SizeY * 2 <= 4096) ? 2 : 1;
}

bool AreProjectedStrokeSamplesContinuous(const FQuickSDFStrokeSample& A, const FQuickSDFStrokeSample& B)
{
	if (A.PaintChartID == INDEX_NONE || B.PaintChartID == INDEX_NONE)
	{
		return true;
	}
	return A.PaintChartID == B.PaintChartID;
}
}

bool UQuickSDFPaintTool::BuildSurfaceBrushParams(const FQuickSDFStrokeSample& Sample, UTextureRenderTarget2D* RenderTarget, FQuickSDFSurfaceBrushParams& OutParams) const
{
	if (!TargetMesh.IsValid() || !CurrentComponent.IsValid() || !RenderTarget || Sample.TriangleID == INDEX_NONE || !TargetMesh->IsTriangle(Sample.TriangleID))
	{
		return false;
	}

	const FTransform ComponentTransform = CurrentComponent->GetComponentTransform();
	const UE::Geometry::FIndex3i Tri = TargetMesh->GetTriangle(Sample.TriangleID);
	const FVector3d W0 = ComponentTransform.TransformPosition(TargetMesh->GetVertex(Tri.A));
	const FVector3d W1 = ComponentTransform.TransformPosition(TargetMesh->GetVertex(Tri.B));
	const FVector3d W2 = ComponentTransform.TransformPosition(TargetMesh->GetVertex(Tri.C));

	FVector3d BrushNormal = FVector3d::CrossProduct(W1 - W0, W2 - W0).GetSafeNormal();
	if (BrushNormal.IsNearlyZero())
	{
		BrushNormal = -Sample.RayDirection.GetSafeNormal();
	}
	if (FVector3d::DotProduct(BrushNormal, Sample.RayDirection) > 0.0)
	{
		BrushNormal *= -1.0;
	}

	FVector BrushXAxis;
	FVector BrushYAxis;
	const FVector BrushNormalF(BrushNormal);
	BrushNormalF.FindBestAxisVectors(BrushXAxis, BrushYAxis);

	const FVector BrushCenterF(Sample.WorldPos);
	const FMatrix BrushToWorldMatrix(BrushXAxis, BrushYAxis, BrushNormalF, BrushCenterF);

	const float BrushRadius = static_cast<float>(GetEffectiveBrushRadius());
	const float BrushDepth = BrushRadius;
	const FVector2D BrushPixelSize = GetBrushPixelSize(RenderTarget);
	const float BrushPixelRadius = FMath::Max(static_cast<float>(FMath::Min(BrushPixelSize.X, BrushPixelSize.Y) * 0.5), 1.0f);
	const float WorldUnitsPerPixel = BrushRadius / BrushPixelRadius;
	const float AntialiasWidth = Properties && Properties->bEnableBrushAntialiasing
		? FMath::Max(Properties->BrushAntialiasingWidth, 0.0f) * WorldUnitsPerPixel
		: 0.0f;

	OutParams.Center = Sample.WorldPos;
	OutParams.LineStart = Sample.WorldPos;
	OutParams.LineEnd = Sample.WorldPos;
	OutParams.Normal = BrushNormal;
	OutParams.WorldToBrushMatrix = BrushToWorldMatrix.InverseFast();
	OutParams.Radius = BrushRadius;
	OutParams.RadialFalloffRange = 0.0f;
	OutParams.Depth = BrushDepth;
	OutParams.DepthFalloffRange = 0.0f;
	OutParams.Strength = 1.0f;
	OutParams.AntialiasWidth = AntialiasWidth;
	OutParams.LineLength = 0.0f;
	OutParams.bIsLine = false;
	OutParams.Color = IsPaintingShadow() ? FLinearColor::Black : FLinearColor::White;
	OutParams.PaintChartID = Sample.PaintChartID;
	return true;
}

bool UQuickSDFPaintTool::BuildSurfaceLineBrushParams(const FQuickSDFStrokeSample& StartSample, const FQuickSDFStrokeSample& EndSample, UTextureRenderTarget2D* RenderTarget, FQuickSDFSurfaceBrushParams& OutParams) const
{
	FQuickSDFSurfaceBrushParams StartParams;
	if (!BuildSurfaceBrushParams(StartSample, RenderTarget, StartParams))
	{
		return false;
	}

	const FVector3d Segment = EndSample.WorldPos - StartSample.WorldPos;
	FVector3d BrushNormal = StartParams.Normal.GetSafeNormal();
	if (BrushNormal.IsNearlyZero())
	{
		BrushNormal = FVector3d(0.0, 0.0, 1.0);
	}

	FVector3d BrushXAxis = Segment - BrushNormal * FVector3d::DotProduct(Segment, BrushNormal);
	if (BrushXAxis.SizeSquared() <= KINDA_SMALL_NUMBER)
	{
		BrushXAxis = Segment;
	}

	const double LineLength = BrushXAxis.Size();
	if (LineLength <= KINDA_SMALL_NUMBER)
	{
		return false;
	}
	BrushXAxis /= LineLength;

	FVector3d BrushYAxis = FVector3d::CrossProduct(BrushNormal, BrushXAxis).GetSafeNormal();
	if (BrushYAxis.IsNearlyZero())
	{
		FVector FallbackX;
		FVector FallbackY;
		FVector(BrushNormal).FindBestAxisVectors(FallbackX, FallbackY);
		BrushXAxis = FVector3d(FallbackX);
		BrushYAxis = FVector3d(FallbackY);
	}
	else
	{
		BrushXAxis = FVector3d::CrossProduct(BrushYAxis, BrushNormal).GetSafeNormal();
	}

	const FMatrix BrushToWorldMatrix(FVector(BrushXAxis), FVector(BrushYAxis), FVector(BrushNormal), FVector(StartSample.WorldPos));

	OutParams = StartParams;
	OutParams.Center = (StartSample.WorldPos + EndSample.WorldPos) * 0.5;
	OutParams.LineStart = StartSample.WorldPos;
	OutParams.LineEnd = EndSample.WorldPos;
	OutParams.Normal = BrushNormal;
	OutParams.WorldToBrushMatrix = BrushToWorldMatrix.InverseFast();
	OutParams.LineLength = static_cast<float>(LineLength);
	OutParams.bIsLine = true;
	return true;
}

bool UQuickSDFPaintTool::GatherSurfacePaintTriangles(
	const FQuickSDFSurfaceBrushParams& BrushParams,
	UTextureRenderTarget2D* RenderTarget,
	TArray<FQuickSDFSurfacePaintTriangle>& OutTriangles,
	FIntRect& OutDirtyRect)
{
	OutTriangles.Reset();
	OutDirtyRect = FIntRect();

	if (!TargetMeshSpatial.IsValid() || !TargetMesh.IsValid() || !CurrentComponent.IsValid() || !Properties || !RenderTarget ||
		!TargetMesh->HasAttributes())
	{
		return false;
	}

	const UE::Geometry::FDynamicMeshUVOverlay* UVOverlay = TargetMesh->Attributes()->GetUVLayer(Properties->UVChannel);
	if (!UVOverlay)
	{
		return false;
	}

	const FTransform ComponentTransform = CurrentComponent->GetComponentTransform();
	const FVector3d LocalCenter = ComponentTransform.InverseTransformPosition(BrushParams.Center);
	const double QueryRadiusWorld = FMath::Max(static_cast<double>(BrushParams.Radius), static_cast<double>(BrushParams.Depth));
	const double LocalQueryRadius = GetConservativeLocalRadius(ComponentTransform, QueryRadiusWorld);
	const UE::Geometry::FAxisAlignedBox3d LocalQueryBounds(
		LocalCenter - FVector3d(LocalQueryRadius, LocalQueryRadius, LocalQueryRadius),
		LocalCenter + FVector3d(LocalQueryRadius, LocalQueryRadius, LocalQueryRadius));
	const double QueryRadiusSqr = FMath::Square(QueryRadiusWorld);

	bool bHasDirtyRect = false;
	UE::Geometry::FDynamicMeshAABBTree3::FTreeTraversal Traversal;
	Traversal.NextBoxF = [&LocalQueryBounds](const UE::Geometry::FAxisAlignedBox3d& Box, int)
	{
		return Box.Intersects(LocalQueryBounds);
	};
	Traversal.NextTriangleF = [this, &BrushParams, RenderTarget, UVOverlay, &ComponentTransform, QueryRadiusSqr, &OutTriangles, &OutDirtyRect, &bHasDirtyRect](int32 TriangleID)
	{
		if (!TargetMesh->IsTriangle(TriangleID) ||
			!IsTriangleInTargetMaterialSlot(TriangleID) ||
			!UVOverlay->IsSetTriangle(TriangleID))
		{
			return;
		}

		const UE::Geometry::FIndex3i Tri = TargetMesh->GetTriangle(TriangleID);
		const FVector3d WorldPositions[3] = {
			ComponentTransform.TransformPosition(TargetMesh->GetVertex(Tri.A)),
			ComponentTransform.TransformPosition(TargetMesh->GetVertex(Tri.B)),
			ComponentTransform.TransformPosition(TargetMesh->GetVertex(Tri.C))
		};

		const FVector ClosestPoint = FMath::ClosestPointOnTriangleToPoint(
			FVector(BrushParams.Center),
			FVector(WorldPositions[0]),
			FVector(WorldPositions[1]),
			FVector(WorldPositions[2]));
		if (FVector::DistSquared(ClosestPoint, FVector(BrushParams.Center)) > QueryRadiusSqr)
		{
			return;
		}

		const UE::Geometry::FIndex3i UVTri = UVOverlay->GetTriangle(TriangleID);
		FVector2D UVs[3] = {
			FVector2D(UVOverlay->GetElement(UVTri.A)),
			FVector2D(UVOverlay->GetElement(UVTri.B)),
			FVector2D(UVOverlay->GetElement(UVTri.C))
		};
		OffsetUVsIntoPrimaryTile(UVs);

		FVector2D UVMin;
		FVector2D UVMax;
		if (!ComputeUVBounds(UVs, UVMin, UVMax))
		{
			return;
		}

		const FIntRect TriangleDirtyRect = MakeDirtyRectFromUVBounds(UVMin, UVMax, RenderTarget);
		if (!bHasDirtyRect)
		{
			OutDirtyRect = TriangleDirtyRect;
			bHasDirtyRect = true;
		}
		else
		{
			OutDirtyRect = UnionRects(OutDirtyRect, TriangleDirtyRect);
		}

		FQuickSDFSurfacePaintTriangle& PaintTriangle = OutTriangles.AddDefaulted_GetRef();
		for (int32 VertexIndex = 0; VertexIndex < 3; ++VertexIndex)
		{
			PaintTriangle.UVs[VertexIndex] = UVs[VertexIndex];
			PaintTriangle.PixelPositions[VertexIndex] = FVector2D(
				UVs[VertexIndex].X * static_cast<double>(RenderTarget->SizeX),
				UVs[VertexIndex].Y * static_cast<double>(RenderTarget->SizeY));
			PaintTriangle.WorldPositions[VertexIndex] = WorldPositions[VertexIndex];
		}
	};

	TargetMeshSpatial->DoTraversal(Traversal);
	return OutTriangles.Num() > 0 && bHasDirtyRect;
}

bool UQuickSDFPaintTool::GatherSurfaceLinePaintTriangles(
	const FQuickSDFSurfaceBrushParams& BrushParams,
	UTextureRenderTarget2D* RenderTarget,
	TArray<FQuickSDFSurfacePaintTriangle>& OutTriangles,
	FIntRect& OutDirtyRect)
{
	OutTriangles.Reset();
	OutDirtyRect = FIntRect();

	if (!TargetMeshSpatial.IsValid() || !TargetMesh.IsValid() || !CurrentComponent.IsValid() || !Properties || !RenderTarget ||
		!TargetMesh->HasAttributes() || BrushParams.LineLength <= KINDA_SMALL_NUMBER)
	{
		return false;
	}

	const UE::Geometry::FDynamicMeshUVOverlay* UVOverlay = TargetMesh->Attributes()->GetUVLayer(Properties->UVChannel);
	if (!UVOverlay)
	{
		return false;
	}

	const FTransform ComponentTransform = CurrentComponent->GetComponentTransform();
	const FVector3d LocalStart = ComponentTransform.InverseTransformPosition(BrushParams.LineStart);
	const FVector3d LocalEnd = ComponentTransform.InverseTransformPosition(BrushParams.LineEnd);
	const double QueryRadiusWorld = FMath::Max(static_cast<double>(BrushParams.Radius), static_cast<double>(BrushParams.Depth));
	const double LocalQueryRadius = GetConservativeLocalRadius(ComponentTransform, QueryRadiusWorld);
	const FVector3d LocalMin(
		FMath::Min(LocalStart.X, LocalEnd.X) - LocalQueryRadius,
		FMath::Min(LocalStart.Y, LocalEnd.Y) - LocalQueryRadius,
		FMath::Min(LocalStart.Z, LocalEnd.Z) - LocalQueryRadius);
	const FVector3d LocalMax(
		FMath::Max(LocalStart.X, LocalEnd.X) + LocalQueryRadius,
		FMath::Max(LocalStart.Y, LocalEnd.Y) + LocalQueryRadius,
		FMath::Max(LocalStart.Z, LocalEnd.Z) + LocalQueryRadius);
	const UE::Geometry::FAxisAlignedBox3d LocalQueryBounds(LocalMin, LocalMax);
	const double RadiusWithAA = static_cast<double>(BrushParams.Radius + BrushParams.AntialiasWidth);
	const double Depth = static_cast<double>(BrushParams.Depth);
	const double LineLength = static_cast<double>(BrushParams.LineLength);

	bool bHasDirtyRect = false;
	UE::Geometry::FDynamicMeshAABBTree3::FTreeTraversal Traversal;
	Traversal.NextBoxF = [&LocalQueryBounds](const UE::Geometry::FAxisAlignedBox3d& Box, int)
	{
		return Box.Intersects(LocalQueryBounds);
	};
	Traversal.NextTriangleF = [this, &BrushParams, RenderTarget, UVOverlay, &ComponentTransform, RadiusWithAA, Depth, LineLength, &OutTriangles, &OutDirtyRect, &bHasDirtyRect](int32 TriangleID)
	{
		if (!TargetMesh->IsTriangle(TriangleID) ||
			!IsTriangleInTargetMaterialSlot(TriangleID) ||
			!UVOverlay->IsSetTriangle(TriangleID))
		{
			return;
		}

		const UE::Geometry::FIndex3i Tri = TargetMesh->GetTriangle(TriangleID);
		const FVector3d WorldPositions[3] = {
			ComponentTransform.TransformPosition(TargetMesh->GetVertex(Tri.A)),
			ComponentTransform.TransformPosition(TargetMesh->GetVertex(Tri.B)),
			ComponentTransform.TransformPosition(TargetMesh->GetVertex(Tri.C))
		};

		double MinX = TNumericLimits<double>::Max();
		double MinY = TNumericLimits<double>::Max();
		double MinZ = TNumericLimits<double>::Max();
		double MaxX = -TNumericLimits<double>::Max();
		double MaxY = -TNumericLimits<double>::Max();
		double MaxZ = -TNumericLimits<double>::Max();
		for (const FVector3d& WorldPosition : WorldPositions)
		{
			const FVector4 BrushPosition4 = BrushParams.WorldToBrushMatrix.TransformPosition(FVector(WorldPosition));
			MinX = FMath::Min(MinX, static_cast<double>(BrushPosition4.X));
			MinY = FMath::Min(MinY, static_cast<double>(BrushPosition4.Y));
			MinZ = FMath::Min(MinZ, static_cast<double>(BrushPosition4.Z));
			MaxX = FMath::Max(MaxX, static_cast<double>(BrushPosition4.X));
			MaxY = FMath::Max(MaxY, static_cast<double>(BrushPosition4.Y));
			MaxZ = FMath::Max(MaxZ, static_cast<double>(BrushPosition4.Z));
		}

		if (MaxX < -RadiusWithAA || MinX > LineLength + RadiusWithAA ||
			MaxY < -RadiusWithAA || MinY > RadiusWithAA ||
			MaxZ < -Depth || MinZ > Depth)
		{
			return;
		}

		const UE::Geometry::FIndex3i UVTri = UVOverlay->GetTriangle(TriangleID);
		FVector2D UVs[3] = {
			FVector2D(UVOverlay->GetElement(UVTri.A)),
			FVector2D(UVOverlay->GetElement(UVTri.B)),
			FVector2D(UVOverlay->GetElement(UVTri.C))
		};
		OffsetUVsIntoPrimaryTile(UVs);

		FVector2D UVMin;
		FVector2D UVMax;
		if (!ComputeUVBounds(UVs, UVMin, UVMax))
		{
			return;
		}

		const FIntRect TriangleDirtyRect = MakeDirtyRectFromUVBounds(UVMin, UVMax, RenderTarget);
		if (!bHasDirtyRect)
		{
			OutDirtyRect = TriangleDirtyRect;
			bHasDirtyRect = true;
		}
		else
		{
			OutDirtyRect = UnionRects(OutDirtyRect, TriangleDirtyRect);
		}

		FQuickSDFSurfacePaintTriangle& PaintTriangle = OutTriangles.AddDefaulted_GetRef();
		for (int32 VertexIndex = 0; VertexIndex < 3; ++VertexIndex)
		{
			PaintTriangle.UVs[VertexIndex] = UVs[VertexIndex];
			PaintTriangle.PixelPositions[VertexIndex] = FVector2D(
				UVs[VertexIndex].X * static_cast<double>(RenderTarget->SizeX),
				UVs[VertexIndex].Y * static_cast<double>(RenderTarget->SizeY));
			PaintTriangle.WorldPositions[VertexIndex] = WorldPositions[VertexIndex];
		}
	};

	TargetMeshSpatial->DoTraversal(Traversal);
	return OutTriangles.Num() > 0 && bHasDirtyRect;
}

bool UQuickSDFPaintTool::GatherSurfacePolylinePaintTriangles(
	const TArray<FQuickSDFStrokeSample>& Samples,
	const FQuickSDFSurfaceBrushParams& BrushParams,
	UTextureRenderTarget2D* RenderTarget,
	TArray<FQuickSDFSurfacePaintTriangle>& OutTriangles,
	FIntRect& OutDirtyRect)
{
	OutTriangles.Reset();
	OutDirtyRect = FIntRect();

	if (!TargetMeshSpatial.IsValid() || !TargetMesh.IsValid() || !CurrentComponent.IsValid() || !Properties || !RenderTarget ||
		!TargetMesh->HasAttributes() || Samples.Num() < 2)
	{
		return false;
	}

	const UE::Geometry::FDynamicMeshUVOverlay* UVOverlay = TargetMesh->Attributes()->GetUVLayer(Properties->UVChannel);
	if (!UVOverlay)
	{
		return false;
	}

	const FTransform ComponentTransform = CurrentComponent->GetComponentTransform();
	const double QueryRadiusWorld = FMath::Max(static_cast<double>(BrushParams.Radius), static_cast<double>(BrushParams.Depth));
	const double LocalQueryRadius = GetConservativeLocalRadius(ComponentTransform, QueryRadiusWorld);

	FVector3d LocalMin(TNumericLimits<double>::Max(), TNumericLimits<double>::Max(), TNumericLimits<double>::Max());
	FVector3d LocalMax(-TNumericLimits<double>::Max(), -TNumericLimits<double>::Max(), -TNumericLimits<double>::Max());
	for (const FQuickSDFStrokeSample& Sample : Samples)
	{
		const FVector3d LocalPoint = ComponentTransform.InverseTransformPosition(Sample.WorldPos);
		LocalMin.X = FMath::Min(LocalMin.X, LocalPoint.X);
		LocalMin.Y = FMath::Min(LocalMin.Y, LocalPoint.Y);
		LocalMin.Z = FMath::Min(LocalMin.Z, LocalPoint.Z);
		LocalMax.X = FMath::Max(LocalMax.X, LocalPoint.X);
		LocalMax.Y = FMath::Max(LocalMax.Y, LocalPoint.Y);
		LocalMax.Z = FMath::Max(LocalMax.Z, LocalPoint.Z);
	}

	const FVector3d LocalPadding(LocalQueryRadius, LocalQueryRadius, LocalQueryRadius);
	const UE::Geometry::FAxisAlignedBox3d LocalQueryBounds(LocalMin - LocalPadding, LocalMax + LocalPadding);

	bool bHasDirtyRect = false;
	UE::Geometry::FDynamicMeshAABBTree3::FTreeTraversal Traversal;
	Traversal.NextBoxF = [&LocalQueryBounds](const UE::Geometry::FAxisAlignedBox3d& Box, int)
	{
		return Box.Intersects(LocalQueryBounds);
	};
	Traversal.NextTriangleF = [this, RenderTarget, UVOverlay, &ComponentTransform, &OutTriangles, &OutDirtyRect, &bHasDirtyRect](int32 TriangleID)
	{
		if (!TargetMesh->IsTriangle(TriangleID) ||
			!IsTriangleInTargetMaterialSlot(TriangleID) ||
			!UVOverlay->IsSetTriangle(TriangleID))
		{
			return;
		}

		const UE::Geometry::FIndex3i Tri = TargetMesh->GetTriangle(TriangleID);
		const FVector3d WorldPositions[3] = {
			ComponentTransform.TransformPosition(TargetMesh->GetVertex(Tri.A)),
			ComponentTransform.TransformPosition(TargetMesh->GetVertex(Tri.B)),
			ComponentTransform.TransformPosition(TargetMesh->GetVertex(Tri.C))
		};

		const UE::Geometry::FIndex3i UVTri = UVOverlay->GetTriangle(TriangleID);
		FVector2D UVs[3] = {
			FVector2D(UVOverlay->GetElement(UVTri.A)),
			FVector2D(UVOverlay->GetElement(UVTri.B)),
			FVector2D(UVOverlay->GetElement(UVTri.C))
		};
		OffsetUVsIntoPrimaryTile(UVs);

		FVector2D UVMin;
		FVector2D UVMax;
		if (!ComputeUVBounds(UVs, UVMin, UVMax))
		{
			return;
		}

		const FIntRect TriangleDirtyRect = MakeDirtyRectFromUVBounds(UVMin, UVMax, RenderTarget);
		if (!bHasDirtyRect)
		{
			OutDirtyRect = TriangleDirtyRect;
			bHasDirtyRect = true;
		}
		else
		{
			OutDirtyRect = UnionRects(OutDirtyRect, TriangleDirtyRect);
		}

		FQuickSDFSurfacePaintTriangle& PaintTriangle = OutTriangles.AddDefaulted_GetRef();
		for (int32 VertexIndex = 0; VertexIndex < 3; ++VertexIndex)
		{
			PaintTriangle.UVs[VertexIndex] = UVs[VertexIndex];
			PaintTriangle.PixelPositions[VertexIndex] = FVector2D(
				UVs[VertexIndex].X * static_cast<double>(RenderTarget->SizeX),
				UVs[VertexIndex].Y * static_cast<double>(RenderTarget->SizeY));
			PaintTriangle.WorldPositions[VertexIndex] = WorldPositions[VertexIndex];
		}
	};

	TargetMeshSpatial->DoTraversal(Traversal);
	return OutTriangles.Num() > 0 && bHasDirtyRect;
}

bool UQuickSDFPaintTool::BuildProjectedPaintParams(
	const TArray<FQuickSDFStrokeSample>& Samples,
	UTextureRenderTarget2D* RenderTarget,
	FQuickSDFProjectedPaintParams& OutParams,
	TArray<FQuickSDFProjectedStrokePoint>& OutStrokePoints) const
{
	OutStrokePoints.Reset();
	if (!RenderTarget || Samples.Num() == 0)
	{
		return false;
	}

	const FQuickSDFStrokeSample* AnchorSample = nullptr;
	for (const FQuickSDFStrokeSample& Sample : Samples)
	{
		if (Sample.TriangleID != INDEX_NONE)
		{
			AnchorSample = &Sample;
			break;
		}
	}
	if (!AnchorSample)
	{
		return false;
	}

	FQuickSDFSurfaceBrushParams SurfaceBrushParams;
	if (!BuildSurfaceBrushParams(*AnchorSample, RenderTarget, SurfaceBrushParams))
	{
		return false;
	}

	FVector3d ProjectionNormal = -AnchorSample->RayDirection.GetSafeNormal();
	if (ProjectionNormal.IsNearlyZero())
	{
		ProjectionNormal = SurfaceBrushParams.Normal.GetSafeNormal();
	}
	if (ProjectionNormal.IsNearlyZero())
	{
		ProjectionNormal = FVector3d(0.0, 0.0, 1.0);
	}

	FVector ProjectionAxisXF;
	FVector ProjectionAxisYF;
	FVector(ProjectionNormal).FindBestAxisVectors(ProjectionAxisXF, ProjectionAxisYF);
	FVector3d ProjectionAxisX(ProjectionAxisXF);
	FVector3d ProjectionAxisY(ProjectionAxisYF);

	OutParams.ProjectionOrigin = AnchorSample->WorldPos;
	OutParams.ProjectionAxisX = ProjectionAxisX.GetSafeNormal();
	OutParams.ProjectionAxisY = ProjectionAxisY.GetSafeNormal();
	OutParams.ProjectionNormal = ProjectionNormal.GetSafeNormal();
	OutParams.Radius = SurfaceBrushParams.Radius;
	OutParams.RadialFalloffRange = SurfaceBrushParams.RadialFalloffRange;
	OutParams.Depth = FMath::Max(SurfaceBrushParams.Depth * 1.5f, SurfaceBrushParams.Radius + SurfaceBrushParams.AntialiasWidth);
	OutParams.DepthFalloffRange = SurfaceBrushParams.DepthFalloffRange;
	OutParams.Strength = SurfaceBrushParams.Strength;
	OutParams.AntialiasWidth = SurfaceBrushParams.AntialiasWidth;
	OutParams.Color = SurfaceBrushParams.Color;
	OutParams.PaintChartID = AnchorSample->PaintChartID;

	OutStrokePoints.Reserve(Samples.Num());
	for (const FQuickSDFStrokeSample& Sample : Samples)
	{
		if (Sample.TriangleID == INDEX_NONE)
		{
			continue;
		}

		if (OutStrokePoints.Num() > 0 &&
			FVector3d::DistSquared(OutStrokePoints.Last().WorldPosition, Sample.WorldPos) <= 1e-8)
		{
			continue;
		}

		const FVector3d Delta = Sample.WorldPos - OutParams.ProjectionOrigin;
		FQuickSDFProjectedStrokePoint& StrokePoint = OutStrokePoints.AddDefaulted_GetRef();
		StrokePoint.WorldPosition = Sample.WorldPos;
		StrokePoint.ProjectedPosition = FVector2f(
			static_cast<float>(FVector3d::DotProduct(Delta, OutParams.ProjectionAxisX)),
			static_cast<float>(FVector3d::DotProduct(Delta, OutParams.ProjectionAxisY)));
		StrokePoint.Normal = -Sample.RayDirection.GetSafeNormal();
		if (StrokePoint.Normal.IsNearlyZero())
		{
			StrokePoint.Normal = OutParams.ProjectionNormal;
		}
		StrokePoint.Pressure = 1.0f;
	}

	return OutStrokePoints.Num() > 0;
}

bool UQuickSDFPaintTool::GatherProjectedPaintTriangles(
	const TArray<FQuickSDFStrokeSample>& Samples,
	const FQuickSDFProjectedPaintParams& PaintParams,
	UTextureRenderTarget2D* RenderTarget,
	TArray<FQuickSDFProjectedPaintTriangle>& OutTriangles,
	FIntRect& OutDirtyRect)
{
	OutTriangles.Reset();
	OutDirtyRect = FIntRect();

	if (!TargetMeshSpatial.IsValid() || !TargetMesh.IsValid() || !CurrentComponent.IsValid() || !Properties || !RenderTarget ||
		!TargetMesh->HasAttributes() || Samples.Num() == 0)
	{
		return false;
	}

	const UE::Geometry::FDynamicMeshUVOverlay* UVOverlay = TargetMesh->Attributes()->GetUVLayer(Properties->UVChannel);
	if (!UVOverlay)
	{
		return false;
	}

	const FTransform ComponentTransform = CurrentComponent->GetComponentTransform();
	const double QueryRadiusWorld = FMath::Max(
		static_cast<double>(PaintParams.Radius + PaintParams.AntialiasWidth),
		static_cast<double>(PaintParams.Depth));
	const double LocalQueryRadius = GetConservativeLocalRadius(ComponentTransform, QueryRadiusWorld);

	FVector3d LocalMin(TNumericLimits<double>::Max(), TNumericLimits<double>::Max(), TNumericLimits<double>::Max());
	FVector3d LocalMax(-TNumericLimits<double>::Max(), -TNumericLimits<double>::Max(), -TNumericLimits<double>::Max());
	for (const FQuickSDFStrokeSample& Sample : Samples)
	{
		const FVector3d LocalPoint = ComponentTransform.InverseTransformPosition(Sample.WorldPos);
		LocalMin.X = FMath::Min(LocalMin.X, LocalPoint.X);
		LocalMin.Y = FMath::Min(LocalMin.Y, LocalPoint.Y);
		LocalMin.Z = FMath::Min(LocalMin.Z, LocalPoint.Z);
		LocalMax.X = FMath::Max(LocalMax.X, LocalPoint.X);
		LocalMax.Y = FMath::Max(LocalMax.Y, LocalPoint.Y);
		LocalMax.Z = FMath::Max(LocalMax.Z, LocalPoint.Z);
	}

	const FVector3d LocalPadding(LocalQueryRadius, LocalQueryRadius, LocalQueryRadius);
	const UE::Geometry::FAxisAlignedBox3d LocalQueryBounds(LocalMin - LocalPadding, LocalMax + LocalPadding);

	bool bHasDirtyRect = false;
	UE::Geometry::FDynamicMeshAABBTree3::FTreeTraversal Traversal;
	Traversal.NextBoxF = [&LocalQueryBounds](const UE::Geometry::FAxisAlignedBox3d& Box, int)
	{
		return Box.Intersects(LocalQueryBounds);
	};
	Traversal.NextTriangleF = [this, RenderTarget, UVOverlay, &ComponentTransform, &PaintParams, &OutTriangles, &OutDirtyRect, &bHasDirtyRect](int32 TriangleID)
	{
		if (!TargetMesh->IsTriangle(TriangleID) ||
			!IsTriangleInTargetMaterialSlot(TriangleID) ||
			!UVOverlay->IsSetTriangle(TriangleID))
		{
			return;
		}

		const int32 TrianglePaintChartID = GetPaintChartIDForTriangle(TriangleID);
		if (PaintParams.PaintChartID != INDEX_NONE && TrianglePaintChartID != PaintParams.PaintChartID)
		{
			return;
		}

		const UE::Geometry::FIndex3i Tri = TargetMesh->GetTriangle(TriangleID);
		const FVector3d WorldPositions[3] = {
			ComponentTransform.TransformPosition(TargetMesh->GetVertex(Tri.A)),
			ComponentTransform.TransformPosition(TargetMesh->GetVertex(Tri.B)),
			ComponentTransform.TransformPosition(TargetMesh->GetVertex(Tri.C))
		};

		const UE::Geometry::FIndex3i UVTri = UVOverlay->GetTriangle(TriangleID);
		FVector2D UVs[3] = {
			FVector2D(UVOverlay->GetElement(UVTri.A)),
			FVector2D(UVOverlay->GetElement(UVTri.B)),
			FVector2D(UVOverlay->GetElement(UVTri.C))
		};
		OffsetUVsIntoPrimaryTile(UVs);

		FVector2D UVMin;
		FVector2D UVMax;
		if (!ComputeUVBounds(UVs, UVMin, UVMax))
		{
			return;
		}

		FIntRect TriangleDirtyRect = MakeDirtyRectFromUVBounds(UVMin, UVMax, RenderTarget);
		TriangleDirtyRect.Min.X -= ProjectedPaintResolvePadding;
		TriangleDirtyRect.Min.Y -= ProjectedPaintResolvePadding;
		TriangleDirtyRect.Max.X += ProjectedPaintResolvePadding;
		TriangleDirtyRect.Max.Y += ProjectedPaintResolvePadding;
		if (!bHasDirtyRect)
		{
			OutDirtyRect = TriangleDirtyRect;
			bHasDirtyRect = true;
		}
		else
		{
			OutDirtyRect = UnionRects(OutDirtyRect, TriangleDirtyRect);
		}

		FQuickSDFProjectedPaintTriangle& PaintTriangle = OutTriangles.AddDefaulted_GetRef();
		PaintTriangle.PaintChartID = TrianglePaintChartID;
		for (int32 VertexIndex = 0; VertexIndex < 3; ++VertexIndex)
		{
			PaintTriangle.UVs[VertexIndex] = UVs[VertexIndex];
			PaintTriangle.PixelPositions[VertexIndex] = FVector2D(
				UVs[VertexIndex].X * static_cast<double>(RenderTarget->SizeX),
				UVs[VertexIndex].Y * static_cast<double>(RenderTarget->SizeY));
			PaintTriangle.WorldPositions[VertexIndex] = WorldPositions[VertexIndex];
		}
	};

	TargetMeshSpatial->DoTraversal(Traversal);
	return OutTriangles.Num() > 0 && bHasDirtyRect;
}

UTextureRenderTarget2D* UQuickSDFPaintTool::GetOrCreateProjectedPaintCoverageRenderTarget(int32 TargetWidth, int32 TargetHeight)
{
	if (TargetWidth <= 0 || TargetHeight <= 0)
	{
		return nullptr;
	}

	const int32 CoverageScale = (TargetWidth * 2 <= 4096 && TargetHeight * 2 <= 4096) ? 2 : 1;
	const int32 CoverageWidth = TargetWidth * CoverageScale;
	const int32 CoverageHeight = TargetHeight * CoverageScale;

	if (!ProjectedPaintCoverageRenderTarget ||
		ProjectedPaintCoverageRenderTarget->SizeX != CoverageWidth ||
		ProjectedPaintCoverageRenderTarget->SizeY != CoverageHeight ||
		ProjectedPaintCoverageRenderTarget->RenderTargetFormat != RTF_R16f)
	{
		ProjectedPaintCoverageRenderTarget = NewObject<UTextureRenderTarget2D>(this);
		ProjectedPaintCoverageRenderTarget->ClearColor = FLinearColor::Black;
		ProjectedPaintCoverageRenderTarget->RenderTargetFormat = RTF_R16f;
		ProjectedPaintCoverageRenderTarget->AddressX = TA_Clamp;
		ProjectedPaintCoverageRenderTarget->AddressY = TA_Clamp;
		ProjectedPaintCoverageRenderTarget->bAutoGenerateMips = false;
		ProjectedPaintCoverageRenderTarget->InitAutoFormat(CoverageWidth, CoverageHeight);
		ProjectedPaintCoverageRenderTarget->UpdateResourceImmediate(true);
	}

	return ProjectedPaintCoverageRenderTarget.Get();
}

bool UQuickSDFPaintTool::PaintProjectedSurfaceStrokeToRenderTarget(UTextureRenderTarget2D* RenderTarget, const TArray<FQuickSDFStrokeSample>& Samples, FIntRect* OutDirtyRect)
{
	if (OutDirtyRect)
	{
		*OutDirtyRect = FIntRect();
	}
	if (!RenderTarget || Samples.Num() == 0)
	{
		return false;
	}

	TArray<FQuickSDFStrokeSample> CleanSamples;
	CleanSamples.Reserve(Samples.Num());
	for (const FQuickSDFStrokeSample& Sample : Samples)
	{
		if (Sample.TriangleID == INDEX_NONE)
		{
			continue;
		}
		if (CleanSamples.Num() > 0 &&
			FVector3d::DistSquared(CleanSamples.Last().WorldPos, Sample.WorldPos) <= 1e-8)
		{
			continue;
		}
		CleanSamples.Add(Sample);
	}

	if (CleanSamples.Num() == 0)
	{
		return false;
	}

	const int32 MaxStrokePoints = QuickSDFProjectedPaintRendering::MaxProjectedPaintStrokePoints;
	const double MaxChunkLength = FMath::Max(static_cast<double>(GetEffectiveBrushRadius()) * 6.0, 0.001);
	FIntRect BatchDirtyRect;
	bool bHasBatchDirtyRect = false;
	bool bDrewAnyChunk = false;
	int32 StartIndex = 0;
	while (StartIndex < CleanSamples.Num())
	{
		int32 EndIndex = StartIndex;
		double ChunkLength = 0.0;
		while (EndIndex + 1 < CleanSamples.Num() &&
			EndIndex - StartIndex + 1 < MaxStrokePoints &&
			AreProjectedStrokeSamplesContinuous(CleanSamples[EndIndex], CleanSamples[EndIndex + 1]))
		{
			const double NextLength = FVector3d::Distance(CleanSamples[EndIndex].WorldPos, CleanSamples[EndIndex + 1].WorldPos);
			if (EndIndex > StartIndex && ChunkLength + NextLength > MaxChunkLength)
			{
				break;
			}
			ChunkLength += NextLength;
			++EndIndex;
		}

		TArray<FQuickSDFStrokeSample> ChunkSamples;
		ChunkSamples.Reserve(EndIndex - StartIndex + 1);
		for (int32 Index = StartIndex; Index <= EndIndex; ++Index)
		{
			ChunkSamples.Add(CleanSamples[Index]);
		}

		FIntRect ChunkDirtyRect;
		if (PaintProjectedSurfaceStrokeChunkToRenderTarget(RenderTarget, ChunkSamples, &ChunkDirtyRect) &&
			ChunkDirtyRect.Width() > 0 && ChunkDirtyRect.Height() > 0)
		{
			BatchDirtyRect = bHasBatchDirtyRect ? UnionRects(BatchDirtyRect, ChunkDirtyRect) : ChunkDirtyRect;
			bHasBatchDirtyRect = true;
			bDrewAnyChunk = true;
		}

		if (EndIndex + 1 < CleanSamples.Num() &&
			AreProjectedStrokeSamplesContinuous(CleanSamples[EndIndex], CleanSamples[EndIndex + 1]))
		{
			StartIndex = EndIndex;
		}
		else
		{
			StartIndex = EndIndex + 1;
		}
	}

	if (OutDirtyRect && bHasBatchDirtyRect)
	{
		*OutDirtyRect = BatchDirtyRect;
	}
	return bDrewAnyChunk;
}

bool UQuickSDFPaintTool::PaintProjectedSurfaceStrokeChunkToRenderTarget(UTextureRenderTarget2D* RenderTarget, const TArray<FQuickSDFStrokeSample>& Samples, FIntRect* OutDirtyRect)
{
	if (OutDirtyRect)
	{
		*OutDirtyRect = FIntRect();
	}
	if (!RenderTarget || Samples.Num() == 0)
	{
		return false;
	}

	FQuickSDFProjectedPaintParams PaintParams;
	TArray<FQuickSDFProjectedStrokePoint> StrokePoints;
	if (!BuildProjectedPaintParams(Samples, RenderTarget, PaintParams, StrokePoints))
	{
		return false;
	}

	TArray<FQuickSDFProjectedPaintTriangle> PaintTriangles;
	FIntRect DirtyRect;
	if (!GatherProjectedPaintTriangles(Samples, PaintParams, RenderTarget, PaintTriangles, DirtyRect))
	{
		return false;
	}

	DirtyRect = ClampDirtyRectToRenderTarget(DirtyRect, RenderTarget);
	if (DirtyRect.Width() <= 0 || DirtyRect.Height() <= 0)
	{
		return false;
	}

	UTextureRenderTarget2D* CoverageRenderTarget = GetOrCreateProjectedPaintCoverageRenderTarget(RenderTarget->SizeX, RenderTarget->SizeY);
	if (!CoverageRenderTarget)
	{
		return false;
	}

	FTextureRenderTargetResource* CoverageResource = CoverageRenderTarget->GameThread_GetRenderTargetResource();
	FTextureRenderTargetResource* TargetResource = RenderTarget->GameThread_GetRenderTargetResource();
	if (!CoverageResource || !TargetResource)
	{
		return false;
	}

	const int32 CoverageScale = FMath::Max(CoverageRenderTarget->SizeX / FMath::Max(RenderTarget->SizeX, 1), 1);
	TRefCountPtr<FQuickSDFProjectedPaintCoverageBatchedElementParameters> CoverageParameters(new FQuickSDFProjectedPaintCoverageBatchedElementParameters());
	CoverageParameters->ShaderParams.ProjectionOrigin = FVector4f(
		static_cast<float>(PaintParams.ProjectionOrigin.X),
		static_cast<float>(PaintParams.ProjectionOrigin.Y),
		static_cast<float>(PaintParams.ProjectionOrigin.Z),
		0.0f);
	CoverageParameters->ShaderParams.ProjectionAxisX = FVector4f(
		static_cast<float>(PaintParams.ProjectionAxisX.X),
		static_cast<float>(PaintParams.ProjectionAxisX.Y),
		static_cast<float>(PaintParams.ProjectionAxisX.Z),
		0.0f);
	CoverageParameters->ShaderParams.ProjectionAxisY = FVector4f(
		static_cast<float>(PaintParams.ProjectionAxisY.X),
		static_cast<float>(PaintParams.ProjectionAxisY.Y),
		static_cast<float>(PaintParams.ProjectionAxisY.Z),
		0.0f);
	CoverageParameters->ShaderParams.ProjectionNormal = FVector4f(
		static_cast<float>(PaintParams.ProjectionNormal.X),
		static_cast<float>(PaintParams.ProjectionNormal.Y),
		static_cast<float>(PaintParams.ProjectionNormal.Z),
		0.0f);
	CoverageParameters->ShaderParams.BrushRadius = PaintParams.Radius;
	CoverageParameters->ShaderParams.BrushRadialFalloffRange = PaintParams.RadialFalloffRange;
	CoverageParameters->ShaderParams.BrushDepth = PaintParams.Depth;
	CoverageParameters->ShaderParams.BrushDepthFalloffRange = PaintParams.DepthFalloffRange;
	CoverageParameters->ShaderParams.BrushAntialiasWidth = PaintParams.AntialiasWidth;
	CoverageParameters->ShaderParams.StrokePoints.Reserve(StrokePoints.Num());
	for (const FQuickSDFProjectedStrokePoint& StrokePoint : StrokePoints)
	{
		CoverageParameters->ShaderParams.StrokePoints.Add(FVector4f(
			StrokePoint.ProjectedPosition.X,
			StrokePoint.ProjectedPosition.Y,
			0.0f,
			StrokePoint.Pressure));
	}

	FCanvas CoverageCanvas(CoverageResource, nullptr, GetToolManager()->GetContextQueriesAPI()->GetCurrentEditingWorld(), GMaxRHIFeatureLevel);
	CoverageCanvas.Clear(FLinearColor::Black);
	FBatchedElements* CoverageElements = CoverageCanvas.GetBatchedElements(FCanvas::ET_Triangle, CoverageParameters, nullptr, SE_BLEND_Opaque);
	CoverageElements->AddReserveVertices(PaintTriangles.Num() * 3);
	CoverageElements->AddReserveTriangles(PaintTriangles.Num(), nullptr, SE_BLEND_Opaque);

	const FHitProxyId CoverageHitProxyId = CoverageCanvas.GetHitProxyId();
	for (const FQuickSDFProjectedPaintTriangle& PaintTriangle : PaintTriangles)
	{
		const int32 V0 = CoverageElements->AddVertex(
			FVector4(PaintTriangle.PixelPositions[0].X * CoverageScale, PaintTriangle.PixelPositions[0].Y * CoverageScale, 0.0, 1.0),
			PaintTriangle.UVs[0],
			FLinearColor(PaintTriangle.WorldPositions[0].X, PaintTriangle.WorldPositions[0].Y, PaintTriangle.WorldPositions[0].Z, 1.0f),
			CoverageHitProxyId);
		const int32 V1 = CoverageElements->AddVertex(
			FVector4(PaintTriangle.PixelPositions[1].X * CoverageScale, PaintTriangle.PixelPositions[1].Y * CoverageScale, 0.0, 1.0),
			PaintTriangle.UVs[1],
			FLinearColor(PaintTriangle.WorldPositions[1].X, PaintTriangle.WorldPositions[1].Y, PaintTriangle.WorldPositions[1].Z, 1.0f),
			CoverageHitProxyId);
		const int32 V2 = CoverageElements->AddVertex(
			FVector4(PaintTriangle.PixelPositions[2].X * CoverageScale, PaintTriangle.PixelPositions[2].Y * CoverageScale, 0.0, 1.0),
			PaintTriangle.UVs[2],
			FLinearColor(PaintTriangle.WorldPositions[2].X, PaintTriangle.WorldPositions[2].Y, PaintTriangle.WorldPositions[2].Z, 1.0f),
			CoverageHitProxyId);

		CoverageElements->AddTriangle(V0, V1, V2, CoverageParameters, SE_BLEND_Opaque);
	}
	CoverageCanvas.Flush_GameThread(false);

	ENQUEUE_RENDER_COMMAND(UpdateQuickSDFProjectedPaintCoverageRTCommand)(
		[CoverageResource](FRHICommandListImmediate& RHICmdList)
		{
			TransitionAndCopyTexture(RHICmdList, CoverageResource->GetRenderTargetTexture(), CoverageResource->TextureRHI, {});
		});

	TRefCountPtr<FQuickSDFProjectedPaintResolveBatchedElementParameters> ResolveParameters(new FQuickSDFProjectedPaintResolveBatchedElementParameters());
	ResolveParameters->ShaderParams.BrushColor = PaintParams.Color;
	ResolveParameters->ShaderParams.BrushStrength = PaintParams.Strength;
	ResolveParameters->ShaderParams.ResolveMetrics = FVector4f(
		1.0f / static_cast<float>(FMath::Max(CoverageRenderTarget->SizeX, 1)),
		1.0f / static_cast<float>(FMath::Max(CoverageRenderTarget->SizeY, 1)),
		static_cast<float>(CoverageScale),
		0.0f);

	FCanvas ResolveCanvas(TargetResource, nullptr, GetToolManager()->GetContextQueriesAPI()->GetCurrentEditingWorld(), GMaxRHIFeatureLevel);
	FBatchedElements* ResolveElements = ResolveCanvas.GetBatchedElements(
		FCanvas::ET_Triangle,
		ResolveParameters,
		CoverageRenderTarget->GetResource(),
		SE_BLEND_Translucent);
	ResolveElements->AddReserveVertices(4);
	ResolveElements->AddReserveTriangles(2, CoverageRenderTarget->GetResource(), SE_BLEND_Translucent);

	const FVector2D TargetSize(RenderTarget->SizeX, RenderTarget->SizeY);
	const FVector2D MinUV(
		static_cast<double>(DirtyRect.Min.X) / TargetSize.X,
		static_cast<double>(DirtyRect.Min.Y) / TargetSize.Y);
	const FVector2D MaxUV(
		static_cast<double>(DirtyRect.Max.X) / TargetSize.X,
		static_cast<double>(DirtyRect.Max.Y) / TargetSize.Y);
	const FHitProxyId ResolveHitProxyId = ResolveCanvas.GetHitProxyId();
	const int32 R0 = ResolveElements->AddVertex(
		FVector4(DirtyRect.Min.X, DirtyRect.Min.Y, 0.0, 1.0),
		MinUV,
		FLinearColor::White,
		ResolveHitProxyId);
	const int32 R1 = ResolveElements->AddVertex(
		FVector4(DirtyRect.Max.X, DirtyRect.Min.Y, 0.0, 1.0),
		FVector2D(MaxUV.X, MinUV.Y),
		FLinearColor::White,
		ResolveHitProxyId);
	const int32 R2 = ResolveElements->AddVertex(
		FVector4(DirtyRect.Max.X, DirtyRect.Max.Y, 0.0, 1.0),
		MaxUV,
		FLinearColor::White,
		ResolveHitProxyId);
	const int32 R3 = ResolveElements->AddVertex(
		FVector4(DirtyRect.Min.X, DirtyRect.Max.Y, 0.0, 1.0),
		FVector2D(MinUV.X, MaxUV.Y),
		FLinearColor::White,
		ResolveHitProxyId);

	ResolveElements->AddTriangleExtensive(R0, R1, R2, ResolveParameters, CoverageRenderTarget->GetResource(), SE_BLEND_Translucent);
	ResolveElements->AddTriangleExtensive(R0, R2, R3, ResolveParameters, CoverageRenderTarget->GetResource(), SE_BLEND_Translucent);
	ResolveCanvas.Flush_GameThread(false);

	ENQUEUE_RENDER_COMMAND(UpdateQuickSDFProjectedPaintTargetRTCommand)(
		[TargetResource](FRHICommandListImmediate& RHICmdList)
		{
			TransitionAndCopyTexture(RHICmdList, TargetResource->GetRenderTargetTexture(), TargetResource->TextureRHI, {});
		});

	if (OutDirtyRect)
	{
		*OutDirtyRect = DirtyRect;
	}
	return true;
}

bool UQuickSDFPaintTool::PaintSurfaceBrushToRenderTarget(UTextureRenderTarget2D* RenderTarget, const FQuickSDFStrokeSample& Sample, FIntRect* OutDirtyRect)
{
	if (OutDirtyRect)
	{
		*OutDirtyRect = FIntRect();
	}

	if (ShouldUseSurfaceSpacePaint())
	{
		return PaintProjectedSurfaceStrokeToRenderTarget(RenderTarget, TArray<FQuickSDFStrokeSample>{ Sample }, OutDirtyRect);
	}

	FQuickSDFSurfaceBrushParams BrushParams;
	if (!BuildSurfaceBrushParams(Sample, RenderTarget, BrushParams))
	{
		return false;
	}

	TArray<FQuickSDFSurfacePaintTriangle> PaintTriangles;
	FIntRect DirtyRect;
	if (!GatherSurfacePaintTriangles(BrushParams, RenderTarget, PaintTriangles, DirtyRect))
	{
		return false;
	}

	FTextureRenderTargetResource* RTResource = RenderTarget->GameThread_GetRenderTargetResource();
	if (!RTResource)
	{
		return false;
	}

	TRefCountPtr<FQuickSDFSurfacePaintBatchedElementParameters> BatchedElementParameters(new FQuickSDFSurfacePaintBatchedElementParameters());
	BatchedElementParameters->ShaderParams.WorldToBrushMatrix = BrushParams.WorldToBrushMatrix;
	BatchedElementParameters->ShaderParams.BrushRadius = BrushParams.Radius;
	BatchedElementParameters->ShaderParams.BrushRadialFalloffRange = BrushParams.RadialFalloffRange;
	BatchedElementParameters->ShaderParams.BrushDepth = BrushParams.Depth;
	BatchedElementParameters->ShaderParams.BrushDepthFalloffRange = BrushParams.DepthFalloffRange;
	BatchedElementParameters->ShaderParams.BrushStrength = BrushParams.Strength;
	BatchedElementParameters->ShaderParams.BrushAntialiasWidth = BrushParams.AntialiasWidth;
	BatchedElementParameters->ShaderParams.BrushLineLength = BrushParams.LineLength;
	BatchedElementParameters->ShaderParams.BrushIsLine = BrushParams.bIsLine ? 1.0f : 0.0f;
	BatchedElementParameters->ShaderParams.BrushColor = BrushParams.Color;

	FCanvas Canvas(RTResource, nullptr, GetToolManager()->GetContextQueriesAPI()->GetCurrentEditingWorld(), GMaxRHIFeatureLevel);
	FBatchedElements* BatchedElements = Canvas.GetBatchedElements(FCanvas::ET_Triangle, BatchedElementParameters, nullptr, SE_BLEND_Translucent);
	BatchedElements->AddReserveVertices(PaintTriangles.Num() * 3);
	BatchedElements->AddReserveTriangles(PaintTriangles.Num(), nullptr, SE_BLEND_Translucent);

	const FHitProxyId HitProxyId = Canvas.GetHitProxyId();
	for (const FQuickSDFSurfacePaintTriangle& PaintTriangle : PaintTriangles)
	{
		const int32 V0 = BatchedElements->AddVertex(
			FVector4(PaintTriangle.PixelPositions[0].X, PaintTriangle.PixelPositions[0].Y, 0.0, 1.0),
			PaintTriangle.UVs[0],
			FLinearColor(PaintTriangle.WorldPositions[0].X, PaintTriangle.WorldPositions[0].Y, PaintTriangle.WorldPositions[0].Z, 1.0f),
			HitProxyId);
		const int32 V1 = BatchedElements->AddVertex(
			FVector4(PaintTriangle.PixelPositions[1].X, PaintTriangle.PixelPositions[1].Y, 0.0, 1.0),
			PaintTriangle.UVs[1],
			FLinearColor(PaintTriangle.WorldPositions[1].X, PaintTriangle.WorldPositions[1].Y, PaintTriangle.WorldPositions[1].Z, 1.0f),
			HitProxyId);
		const int32 V2 = BatchedElements->AddVertex(
			FVector4(PaintTriangle.PixelPositions[2].X, PaintTriangle.PixelPositions[2].Y, 0.0, 1.0),
			PaintTriangle.UVs[2],
			FLinearColor(PaintTriangle.WorldPositions[2].X, PaintTriangle.WorldPositions[2].Y, PaintTriangle.WorldPositions[2].Z, 1.0f),
			HitProxyId);

		BatchedElements->AddTriangle(V0, V1, V2, BatchedElementParameters, SE_BLEND_Translucent);
	}

	Canvas.Flush_GameThread(false);

	ENQUEUE_RENDER_COMMAND(UpdateQuickSDFSurfacePaintRTCommand)(
		[RTResource](FRHICommandListImmediate& RHICmdList)
		{
			TransitionAndCopyTexture(RHICmdList, RTResource->GetRenderTargetTexture(), RTResource->TextureRHI, {});
		});

	if (OutDirtyRect)
	{
		*OutDirtyRect = DirtyRect;
	}
	return true;
}

bool UQuickSDFPaintTool::PaintSurfaceLineToRenderTarget(UTextureRenderTarget2D* RenderTarget, const FQuickSDFStrokeSample& StartSample, const FQuickSDFStrokeSample& EndSample, FIntRect* OutDirtyRect)
{
	if (OutDirtyRect)
	{
		*OutDirtyRect = FIntRect();
	}

	FQuickSDFSurfaceBrushParams BrushParams;
	if (!BuildSurfaceLineBrushParams(StartSample, EndSample, RenderTarget, BrushParams))
	{
		return false;
	}

	TArray<FQuickSDFSurfacePaintTriangle> PaintTriangles;
	FIntRect DirtyRect;
	if (!GatherSurfaceLinePaintTriangles(BrushParams, RenderTarget, PaintTriangles, DirtyRect))
	{
		return false;
	}

	FTextureRenderTargetResource* RTResource = RenderTarget->GameThread_GetRenderTargetResource();
	if (!RTResource)
	{
		return false;
	}

	TRefCountPtr<FQuickSDFSurfacePaintBatchedElementParameters> BatchedElementParameters(new FQuickSDFSurfacePaintBatchedElementParameters());
	BatchedElementParameters->ShaderParams.WorldToBrushMatrix = BrushParams.WorldToBrushMatrix;
	BatchedElementParameters->ShaderParams.BrushRadius = BrushParams.Radius;
	BatchedElementParameters->ShaderParams.BrushRadialFalloffRange = BrushParams.RadialFalloffRange;
	BatchedElementParameters->ShaderParams.BrushDepth = BrushParams.Depth;
	BatchedElementParameters->ShaderParams.BrushDepthFalloffRange = BrushParams.DepthFalloffRange;
	BatchedElementParameters->ShaderParams.BrushStrength = BrushParams.Strength;
	BatchedElementParameters->ShaderParams.BrushAntialiasWidth = BrushParams.AntialiasWidth;
	BatchedElementParameters->ShaderParams.BrushLineLength = BrushParams.LineLength;
	BatchedElementParameters->ShaderParams.BrushIsLine = BrushParams.bIsLine ? 1.0f : 0.0f;
	BatchedElementParameters->ShaderParams.BrushColor = BrushParams.Color;

	FCanvas Canvas(RTResource, nullptr, GetToolManager()->GetContextQueriesAPI()->GetCurrentEditingWorld(), GMaxRHIFeatureLevel);
	FBatchedElements* BatchedElements = Canvas.GetBatchedElements(FCanvas::ET_Triangle, BatchedElementParameters, nullptr, SE_BLEND_Translucent);
	BatchedElements->AddReserveVertices(PaintTriangles.Num() * 3);
	BatchedElements->AddReserveTriangles(PaintTriangles.Num(), nullptr, SE_BLEND_Translucent);

	const FHitProxyId HitProxyId = Canvas.GetHitProxyId();
	for (const FQuickSDFSurfacePaintTriangle& PaintTriangle : PaintTriangles)
	{
		const int32 V0 = BatchedElements->AddVertex(
			FVector4(PaintTriangle.PixelPositions[0].X, PaintTriangle.PixelPositions[0].Y, 0.0, 1.0),
			PaintTriangle.UVs[0],
			FLinearColor(PaintTriangle.WorldPositions[0].X, PaintTriangle.WorldPositions[0].Y, PaintTriangle.WorldPositions[0].Z, 1.0f),
			HitProxyId);
		const int32 V1 = BatchedElements->AddVertex(
			FVector4(PaintTriangle.PixelPositions[1].X, PaintTriangle.PixelPositions[1].Y, 0.0, 1.0),
			PaintTriangle.UVs[1],
			FLinearColor(PaintTriangle.WorldPositions[1].X, PaintTriangle.WorldPositions[1].Y, PaintTriangle.WorldPositions[1].Z, 1.0f),
			HitProxyId);
		const int32 V2 = BatchedElements->AddVertex(
			FVector4(PaintTriangle.PixelPositions[2].X, PaintTriangle.PixelPositions[2].Y, 0.0, 1.0),
			PaintTriangle.UVs[2],
			FLinearColor(PaintTriangle.WorldPositions[2].X, PaintTriangle.WorldPositions[2].Y, PaintTriangle.WorldPositions[2].Z, 1.0f),
			HitProxyId);

		BatchedElements->AddTriangle(V0, V1, V2, BatchedElementParameters, SE_BLEND_Translucent);
	}

	Canvas.Flush_GameThread(false);

	ENQUEUE_RENDER_COMMAND(UpdateQuickSDFSurfaceLinePaintRTCommand)(
		[RTResource](FRHICommandListImmediate& RHICmdList)
		{
			TransitionAndCopyTexture(RHICmdList, RTResource->GetRenderTargetTexture(), RTResource->TextureRHI, {});
		});

	if (OutDirtyRect)
	{
		*OutDirtyRect = DirtyRect;
	}
	return true;
}

bool UQuickSDFPaintTool::PaintSurfaceLineSegmentsToRenderTarget(UTextureRenderTarget2D* RenderTarget, const TArray<FQuickSDFStrokeSample>& Samples, FIntRect* OutDirtyRect)
{
	if (OutDirtyRect)
	{
		*OutDirtyRect = FIntRect();
	}
	if (!RenderTarget || Samples.Num() < 2)
	{
		return false;
	}

	FTextureRenderTargetResource* RTResource = RenderTarget->GameThread_GetRenderTargetResource();
	if (!RTResource)
	{
		return false;
	}

	FCanvas Canvas(RTResource, nullptr, GetToolManager()->GetContextQueriesAPI()->GetCurrentEditingWorld(), GMaxRHIFeatureLevel);
	TArray<TRefCountPtr<FQuickSDFSurfacePaintBatchedElementParameters>> SegmentParameters;
	FIntRect BatchDirtyRect;
	bool bHasBatchDirtyRect = false;
	bool bDrewAnySegment = false;

	const FHitProxyId HitProxyId = Canvas.GetHitProxyId();
	for (int32 SegmentIndex = 1; SegmentIndex < Samples.Num(); ++SegmentIndex)
	{
		const FQuickSDFStrokeSample& StartSample = Samples[SegmentIndex - 1];
		const FQuickSDFStrokeSample& EndSample = Samples[SegmentIndex];
		if (FVector3d::DistSquared(StartSample.WorldPos, EndSample.WorldPos) <= 1e-8)
		{
			continue;
		}

		FQuickSDFSurfaceBrushParams BrushParams;
		if (!BuildSurfaceLineBrushParams(StartSample, EndSample, RenderTarget, BrushParams))
		{
			continue;
		}

		TArray<FQuickSDFSurfacePaintTriangle> PaintTriangles;
		FIntRect SegmentDirtyRect;
		if (!GatherSurfaceLinePaintTriangles(BrushParams, RenderTarget, PaintTriangles, SegmentDirtyRect))
		{
			continue;
		}

		TRefCountPtr<FQuickSDFSurfacePaintBatchedElementParameters> BatchedElementParameters(new FQuickSDFSurfacePaintBatchedElementParameters());
		BatchedElementParameters->ShaderParams.WorldToBrushMatrix = BrushParams.WorldToBrushMatrix;
		BatchedElementParameters->ShaderParams.BrushRadius = BrushParams.Radius;
		BatchedElementParameters->ShaderParams.BrushRadialFalloffRange = BrushParams.RadialFalloffRange;
		BatchedElementParameters->ShaderParams.BrushDepth = BrushParams.Depth;
		BatchedElementParameters->ShaderParams.BrushDepthFalloffRange = BrushParams.DepthFalloffRange;
		BatchedElementParameters->ShaderParams.BrushStrength = BrushParams.Strength;
		BatchedElementParameters->ShaderParams.BrushAntialiasWidth = BrushParams.AntialiasWidth;
		BatchedElementParameters->ShaderParams.BrushLineLength = BrushParams.LineLength;
		BatchedElementParameters->ShaderParams.BrushIsLine = BrushParams.bIsLine ? 1.0f : 0.0f;
		BatchedElementParameters->ShaderParams.BrushColor = BrushParams.Color;
		SegmentParameters.Add(BatchedElementParameters);

		FBatchedElements* BatchedElements = Canvas.GetBatchedElements(FCanvas::ET_Triangle, BatchedElementParameters, nullptr, SE_BLEND_Translucent);
		BatchedElements->AddReserveVertices(PaintTriangles.Num() * 3);
		BatchedElements->AddReserveTriangles(PaintTriangles.Num(), nullptr, SE_BLEND_Translucent);

		for (const FQuickSDFSurfacePaintTriangle& PaintTriangle : PaintTriangles)
		{
			const int32 V0 = BatchedElements->AddVertex(
				FVector4(PaintTriangle.PixelPositions[0].X, PaintTriangle.PixelPositions[0].Y, 0.0, 1.0),
				PaintTriangle.UVs[0],
				FLinearColor(PaintTriangle.WorldPositions[0].X, PaintTriangle.WorldPositions[0].Y, PaintTriangle.WorldPositions[0].Z, 1.0f),
				HitProxyId);
			const int32 V1 = BatchedElements->AddVertex(
				FVector4(PaintTriangle.PixelPositions[1].X, PaintTriangle.PixelPositions[1].Y, 0.0, 1.0),
				PaintTriangle.UVs[1],
				FLinearColor(PaintTriangle.WorldPositions[1].X, PaintTriangle.WorldPositions[1].Y, PaintTriangle.WorldPositions[1].Z, 1.0f),
				HitProxyId);
			const int32 V2 = BatchedElements->AddVertex(
				FVector4(PaintTriangle.PixelPositions[2].X, PaintTriangle.PixelPositions[2].Y, 0.0, 1.0),
				PaintTriangle.UVs[2],
				FLinearColor(PaintTriangle.WorldPositions[2].X, PaintTriangle.WorldPositions[2].Y, PaintTriangle.WorldPositions[2].Z, 1.0f),
				HitProxyId);

			BatchedElements->AddTriangle(V0, V1, V2, BatchedElementParameters, SE_BLEND_Translucent);
		}

		if (!bHasBatchDirtyRect)
		{
			BatchDirtyRect = SegmentDirtyRect;
			bHasBatchDirtyRect = true;
		}
		else
		{
			BatchDirtyRect = UnionRects(BatchDirtyRect, SegmentDirtyRect);
		}
		bDrewAnySegment = true;
	}

	if (!bDrewAnySegment)
	{
		return false;
	}

	Canvas.Flush_GameThread(false);

	ENQUEUE_RENDER_COMMAND(UpdateQuickSDFSurfaceLineSegmentsPaintRTCommand)(
		[RTResource](FRHICommandListImmediate& RHICmdList)
		{
			TransitionAndCopyTexture(RHICmdList, RTResource->GetRenderTargetTexture(), RTResource->TextureRHI, {});
		});

	if (OutDirtyRect && bHasBatchDirtyRect)
	{
		*OutDirtyRect = BatchDirtyRect;
	}
	return true;
}

bool UQuickSDFPaintTool::PaintSurfaceBrushesToRenderTarget(UTextureRenderTarget2D* RenderTarget, const TArray<FQuickSDFStrokeSample>& Samples, FIntRect* OutDirtyRect)
{
	if (OutDirtyRect)
	{
		*OutDirtyRect = FIntRect();
	}
	if (!RenderTarget || Samples.Num() == 0)
	{
		return false;
	}

	FTextureRenderTargetResource* RTResource = RenderTarget->GameThread_GetRenderTargetResource();
	if (!RTResource)
	{
		return false;
	}

	FCanvas Canvas(RTResource, nullptr, GetToolManager()->GetContextQueriesAPI()->GetCurrentEditingWorld(), GMaxRHIFeatureLevel);
	TArray<TRefCountPtr<FQuickSDFSurfacePaintBatchedElementParameters>> BrushParameters;
	FIntRect BatchDirtyRect;
	bool bHasBatchDirtyRect = false;
	bool bDrewAnyBrush = false;

	const FHitProxyId HitProxyId = Canvas.GetHitProxyId();
	for (const FQuickSDFStrokeSample& Sample : Samples)
	{
		FQuickSDFSurfaceBrushParams BrushParams;
		if (!BuildSurfaceBrushParams(Sample, RenderTarget, BrushParams))
		{
			continue;
		}

		TArray<FQuickSDFSurfacePaintTriangle> PaintTriangles;
		FIntRect BrushDirtyRect;
		if (!GatherSurfacePaintTriangles(BrushParams, RenderTarget, PaintTriangles, BrushDirtyRect))
		{
			continue;
		}

		TRefCountPtr<FQuickSDFSurfacePaintBatchedElementParameters> BatchedElementParameters(new FQuickSDFSurfacePaintBatchedElementParameters());
		BatchedElementParameters->ShaderParams.WorldToBrushMatrix = BrushParams.WorldToBrushMatrix;
		BatchedElementParameters->ShaderParams.BrushRadius = BrushParams.Radius;
		BatchedElementParameters->ShaderParams.BrushRadialFalloffRange = BrushParams.RadialFalloffRange;
		BatchedElementParameters->ShaderParams.BrushDepth = BrushParams.Depth;
		BatchedElementParameters->ShaderParams.BrushDepthFalloffRange = BrushParams.DepthFalloffRange;
		BatchedElementParameters->ShaderParams.BrushStrength = BrushParams.Strength;
		BatchedElementParameters->ShaderParams.BrushAntialiasWidth = BrushParams.AntialiasWidth;
		BatchedElementParameters->ShaderParams.BrushLineLength = 0.0f;
		BatchedElementParameters->ShaderParams.BrushIsLine = 0.0f;
		BatchedElementParameters->ShaderParams.BrushColor = BrushParams.Color;
		BrushParameters.Add(BatchedElementParameters);

		FBatchedElements* BatchedElements = Canvas.GetBatchedElements(FCanvas::ET_Triangle, BatchedElementParameters, nullptr, SE_BLEND_Translucent);
		BatchedElements->AddReserveVertices(PaintTriangles.Num() * 3);
		BatchedElements->AddReserveTriangles(PaintTriangles.Num(), nullptr, SE_BLEND_Translucent);

		for (const FQuickSDFSurfacePaintTriangle& PaintTriangle : PaintTriangles)
		{
			const int32 V0 = BatchedElements->AddVertex(
				FVector4(PaintTriangle.PixelPositions[0].X, PaintTriangle.PixelPositions[0].Y, 0.0, 1.0),
				PaintTriangle.UVs[0],
				FLinearColor(PaintTriangle.WorldPositions[0].X, PaintTriangle.WorldPositions[0].Y, PaintTriangle.WorldPositions[0].Z, 1.0f),
				HitProxyId);
			const int32 V1 = BatchedElements->AddVertex(
				FVector4(PaintTriangle.PixelPositions[1].X, PaintTriangle.PixelPositions[1].Y, 0.0, 1.0),
				PaintTriangle.UVs[1],
				FLinearColor(PaintTriangle.WorldPositions[1].X, PaintTriangle.WorldPositions[1].Y, PaintTriangle.WorldPositions[1].Z, 1.0f),
				HitProxyId);
			const int32 V2 = BatchedElements->AddVertex(
				FVector4(PaintTriangle.PixelPositions[2].X, PaintTriangle.PixelPositions[2].Y, 0.0, 1.0),
				PaintTriangle.UVs[2],
				FLinearColor(PaintTriangle.WorldPositions[2].X, PaintTriangle.WorldPositions[2].Y, PaintTriangle.WorldPositions[2].Z, 1.0f),
				HitProxyId);

			BatchedElements->AddTriangle(V0, V1, V2, BatchedElementParameters, SE_BLEND_Translucent);
		}

		if (!bHasBatchDirtyRect)
		{
			BatchDirtyRect = BrushDirtyRect;
			bHasBatchDirtyRect = true;
		}
		else
		{
			BatchDirtyRect = UnionRects(BatchDirtyRect, BrushDirtyRect);
		}
		bDrewAnyBrush = true;
	}

	if (!bDrewAnyBrush)
	{
		return false;
	}

	Canvas.Flush_GameThread(false);

	ENQUEUE_RENDER_COMMAND(UpdateQuickSDFSurfaceBrushesPaintRTCommand)(
		[RTResource](FRHICommandListImmediate& RHICmdList)
		{
			TransitionAndCopyTexture(RHICmdList, RTResource->GetRenderTargetTexture(), RTResource->TextureRHI, {});
		});

	if (OutDirtyRect && bHasBatchDirtyRect)
	{
		*OutDirtyRect = BatchDirtyRect;
	}
	return true;
}

bool UQuickSDFPaintTool::PaintSurfacePolylineToRenderTarget(UTextureRenderTarget2D* RenderTarget, const TArray<FQuickSDFStrokeSample>& Samples, FIntRect* OutDirtyRect)
{
	if (OutDirtyRect)
	{
		*OutDirtyRect = FIntRect();
	}

	if (ShouldUseSurfaceSpacePaint())
	{
		return PaintProjectedSurfaceStrokeToRenderTarget(RenderTarget, Samples, OutDirtyRect);
	}

	if (!RenderTarget || Samples.Num() < 2)
	{
		return false;
	}

	TArray<FQuickSDFStrokeSample> PolylineSamples;
	const int32 MaxStrokePoints = QuickSDFSurfacePaintRendering::MaxSurfacePaintStrokePoints;
	if (Samples.Num() > MaxStrokePoints)
	{
		PolylineSamples.Reserve(MaxStrokePoints);
		for (int32 Index = 0; Index < MaxStrokePoints; ++Index)
		{
			const double Alpha = static_cast<double>(Index) / static_cast<double>(MaxStrokePoints - 1);
			const int32 SourceIndex = FMath::Clamp(FMath::RoundToInt(Alpha * static_cast<double>(Samples.Num() - 1)), 0, Samples.Num() - 1);
			if (PolylineSamples.Num() == 0 ||
				FVector3d::DistSquared(PolylineSamples.Last().WorldPos, Samples[SourceIndex].WorldPos) > 1e-8)
			{
				PolylineSamples.Add(Samples[SourceIndex]);
			}
		}
	}
	else
	{
		PolylineSamples.Reserve(Samples.Num());
		for (const FQuickSDFStrokeSample& Sample : Samples)
		{
			if (PolylineSamples.Num() == 0 ||
				FVector3d::DistSquared(PolylineSamples.Last().WorldPos, Sample.WorldPos) > 1e-8)
			{
				PolylineSamples.Add(Sample);
			}
		}
	}

	if (PolylineSamples.Num() < 2)
	{
		return false;
	}

	FQuickSDFSurfaceBrushParams BrushParams;
	if (!BuildSurfaceBrushParams(PolylineSamples[0], RenderTarget, BrushParams))
	{
		return false;
	}

	TArray<FQuickSDFSurfacePaintTriangle> PaintTriangles;
	FIntRect DirtyRect;
	if (!GatherSurfacePolylinePaintTriangles(PolylineSamples, BrushParams, RenderTarget, PaintTriangles, DirtyRect))
	{
		return false;
	}

	FTextureRenderTargetResource* RTResource = RenderTarget->GameThread_GetRenderTargetResource();
	if (!RTResource)
	{
		return false;
	}

	TRefCountPtr<FQuickSDFSurfacePaintBatchedElementParameters> BatchedElementParameters(new FQuickSDFSurfacePaintBatchedElementParameters());
	BatchedElementParameters->ShaderParams.WorldToBrushMatrix = BrushParams.WorldToBrushMatrix;
	BatchedElementParameters->ShaderParams.BrushRadius = BrushParams.Radius;
	BatchedElementParameters->ShaderParams.BrushRadialFalloffRange = BrushParams.RadialFalloffRange;
	BatchedElementParameters->ShaderParams.BrushDepth = BrushParams.Depth;
	BatchedElementParameters->ShaderParams.BrushDepthFalloffRange = BrushParams.DepthFalloffRange;
	BatchedElementParameters->ShaderParams.BrushStrength = BrushParams.Strength;
	BatchedElementParameters->ShaderParams.BrushAntialiasWidth = BrushParams.AntialiasWidth;
	BatchedElementParameters->ShaderParams.BrushLineLength = 0.0f;
	BatchedElementParameters->ShaderParams.BrushIsLine = 0.0f;
	BatchedElementParameters->ShaderParams.BrushIsPolyline = 1.0f;
	BatchedElementParameters->ShaderParams.BrushIsPointStroke = 1.0f;
	BatchedElementParameters->ShaderParams.BrushColor = BrushParams.Color;
	BatchedElementParameters->ShaderParams.StrokePoints.Reserve(PolylineSamples.Num());
	BatchedElementParameters->ShaderParams.StrokeNormals.Reserve(PolylineSamples.Num());
	for (const FQuickSDFStrokeSample& Sample : PolylineSamples)
	{
		BatchedElementParameters->ShaderParams.StrokePoints.Add(FVector4f(
			static_cast<float>(Sample.WorldPos.X),
			static_cast<float>(Sample.WorldPos.Y),
			static_cast<float>(Sample.WorldPos.Z),
			1.0f));

		FVector3d StrokeNormal = -Sample.RayDirection.GetSafeNormal();
		if (StrokeNormal.IsNearlyZero())
		{
			StrokeNormal = BrushParams.Normal.GetSafeNormal();
		}
		if (StrokeNormal.IsNearlyZero())
		{
			StrokeNormal = FVector3d(0.0, 0.0, 1.0);
		}
		BatchedElementParameters->ShaderParams.StrokeNormals.Add(FVector4f(
			static_cast<float>(StrokeNormal.X),
			static_cast<float>(StrokeNormal.Y),
			static_cast<float>(StrokeNormal.Z),
			0.0f));
	}

	FCanvas Canvas(RTResource, nullptr, GetToolManager()->GetContextQueriesAPI()->GetCurrentEditingWorld(), GMaxRHIFeatureLevel);
	FBatchedElements* BatchedElements = Canvas.GetBatchedElements(FCanvas::ET_Triangle, BatchedElementParameters, nullptr, SE_BLEND_Translucent);
	BatchedElements->AddReserveVertices(PaintTriangles.Num() * 3);
	BatchedElements->AddReserveTriangles(PaintTriangles.Num(), nullptr, SE_BLEND_Translucent);

	const FHitProxyId HitProxyId = Canvas.GetHitProxyId();
	for (const FQuickSDFSurfacePaintTriangle& PaintTriangle : PaintTriangles)
	{
		const int32 V0 = BatchedElements->AddVertex(
			FVector4(PaintTriangle.PixelPositions[0].X, PaintTriangle.PixelPositions[0].Y, 0.0, 1.0),
			PaintTriangle.UVs[0],
			FLinearColor(PaintTriangle.WorldPositions[0].X, PaintTriangle.WorldPositions[0].Y, PaintTriangle.WorldPositions[0].Z, 1.0f),
			HitProxyId);
		const int32 V1 = BatchedElements->AddVertex(
			FVector4(PaintTriangle.PixelPositions[1].X, PaintTriangle.PixelPositions[1].Y, 0.0, 1.0),
			PaintTriangle.UVs[1],
			FLinearColor(PaintTriangle.WorldPositions[1].X, PaintTriangle.WorldPositions[1].Y, PaintTriangle.WorldPositions[1].Z, 1.0f),
			HitProxyId);
		const int32 V2 = BatchedElements->AddVertex(
			FVector4(PaintTriangle.PixelPositions[2].X, PaintTriangle.PixelPositions[2].Y, 0.0, 1.0),
			PaintTriangle.UVs[2],
			FLinearColor(PaintTriangle.WorldPositions[2].X, PaintTriangle.WorldPositions[2].Y, PaintTriangle.WorldPositions[2].Z, 1.0f),
			HitProxyId);

		BatchedElements->AddTriangle(V0, V1, V2, BatchedElementParameters, SE_BLEND_Translucent);
	}

	Canvas.Flush_GameThread(false);

	ENQUEUE_RENDER_COMMAND(UpdateQuickSDFSurfacePolylinePaintRTCommand)(
		[RTResource](FRHICommandListImmediate& RHICmdList)
		{
			TransitionAndCopyTexture(RHICmdList, RTResource->GetRenderTargetTexture(), RTResource->TextureRHI, {});
		});

	if (OutDirtyRect)
	{
		*OutDirtyRect = DirtyRect;
	}
	return true;
}

bool UQuickSDFPaintTool::PaintUVBrushesToRenderTarget(
	UTextureRenderTarget2D* RenderTarget,
	const TArray<FQuickSDFStrokeSample>& Samples,
	const TArray<FVector2D>& PixelSizes,
	FIntRect* OutDirtyRect)
{
	if (OutDirtyRect)
	{
		*OutDirtyRect = FIntRect();
	}
	if (!RenderTarget || Samples.Num() == 0 || Samples.Num() != PixelSizes.Num())
	{
		return false;
	}

	FTextureRenderTargetResource* RTResource = RenderTarget->GameThread_GetRenderTargetResource();
	if (!RTResource)
	{
		return false;
	}

	const FVector2D RTSize(RenderTarget->SizeX, RenderTarget->SizeY);
	const FLinearColor PaintColor = IsPaintingShadow() ? FLinearColor::Black : FLinearColor::White;
	const float BrushAAWidthPixels = Properties && Properties->bEnableBrushAntialiasing
		? FMath::Max(Properties->BrushAntialiasingWidth * 2.0f, 1.0f)
		: 0.0f;

	FCanvas Canvas(RTResource, nullptr, GetToolManager()->GetContextQueriesAPI()->GetCurrentEditingWorld(), GMaxRHIFeatureLevel);
	TArray<TRefCountPtr<FQuickSDFSurfacePaintBatchedElementParameters>> BrushParameters;
	FIntRect BatchDirtyRect;
	bool bHasBatchDirtyRect = false;
	bool bDrewAnyBrush = false;
	const FHitProxyId HitProxyId = Canvas.GetHitProxyId();

	for (int32 Index = 0; Index < Samples.Num(); ++Index)
	{
		const FQuickSDFStrokeSample& Sample = Samples[Index];
		const FVector2D PixelSize = PixelSizes[Index];
		const double RadiusX = FMath::Max(PixelSize.X * 0.5, 0.5);
		const double RadiusY = FMath::Max(PixelSize.Y * 0.5, 0.5);
		const FVector2D PixelCenter(Sample.UV.X * RTSize.X, Sample.UV.Y * RTSize.Y);
		const FVector2D StampMin(PixelCenter.X - RadiusX, PixelCenter.Y - RadiusY);
		const FVector2D StampMax(PixelCenter.X + RadiusX, PixelCenter.Y + RadiusY);

		const int32 DirtyPadding = FMath::CeilToInt(BrushAAWidthPixels) + 2;
		const FIntRect BrushDirtyRect(
			FMath::FloorToInt(StampMin.X) - DirtyPadding,
			FMath::FloorToInt(StampMin.Y) - DirtyPadding,
			FMath::CeilToInt(StampMax.X) + DirtyPadding,
			FMath::CeilToInt(StampMax.Y) + DirtyPadding);
		if (!bHasBatchDirtyRect)
		{
			BatchDirtyRect = BrushDirtyRect;
			bHasBatchDirtyRect = true;
		}
		else
		{
			BatchDirtyRect = UnionRects(BatchDirtyRect, BrushDirtyRect);
		}

		TRefCountPtr<FQuickSDFSurfacePaintBatchedElementParameters> BatchedElementParameters(new FQuickSDFSurfacePaintBatchedElementParameters());
		BatchedElementParameters->ShaderParams.WorldToBrushMatrix = FMatrix(
			FPlane(1.0 / RadiusX, 0.0, 0.0, 0.0),
			FPlane(0.0, 1.0 / RadiusY, 0.0, 0.0),
			FPlane(0.0, 0.0, 1.0, 0.0),
			FPlane(-PixelCenter.X / RadiusX, -PixelCenter.Y / RadiusY, 0.0, 1.0));
		BatchedElementParameters->ShaderParams.BrushRadius = 1.0f;
		BatchedElementParameters->ShaderParams.BrushRadialFalloffRange = 0.0f;
		BatchedElementParameters->ShaderParams.BrushDepth = 1.0f;
		BatchedElementParameters->ShaderParams.BrushDepthFalloffRange = 0.0f;
		BatchedElementParameters->ShaderParams.BrushStrength = 1.0f;
		BatchedElementParameters->ShaderParams.BrushAntialiasWidth = BrushAAWidthPixels / static_cast<float>(FMath::Max(FMath::Min(RadiusX, RadiusY), 1.0));
		BatchedElementParameters->ShaderParams.BrushLineLength = 0.0f;
		BatchedElementParameters->ShaderParams.BrushIsLine = 0.0f;
		BatchedElementParameters->ShaderParams.BrushColor = PaintColor;
		BrushParameters.Add(BatchedElementParameters);

		FBatchedElements* BatchedElements = Canvas.GetBatchedElements(FCanvas::ET_Triangle, BatchedElementParameters, nullptr, SE_BLEND_Translucent);
		BatchedElements->AddReserveVertices(4);
		BatchedElements->AddReserveTriangles(2, nullptr, SE_BLEND_Translucent);

		const int32 V0 = BatchedElements->AddVertex(
			FVector4(StampMin.X, StampMin.Y, 0.0, 1.0),
			FVector2D(0.0, 0.0),
			FLinearColor(StampMin.X, StampMin.Y, 0.0f, 1.0f),
			HitProxyId);
		const int32 V1 = BatchedElements->AddVertex(
			FVector4(StampMax.X, StampMin.Y, 0.0, 1.0),
			FVector2D(1.0, 0.0),
			FLinearColor(StampMax.X, StampMin.Y, 0.0f, 1.0f),
			HitProxyId);
		const int32 V2 = BatchedElements->AddVertex(
			FVector4(StampMax.X, StampMax.Y, 0.0, 1.0),
			FVector2D(1.0, 1.0),
			FLinearColor(StampMax.X, StampMax.Y, 0.0f, 1.0f),
			HitProxyId);
		const int32 V3 = BatchedElements->AddVertex(
			FVector4(StampMin.X, StampMax.Y, 0.0, 1.0),
			FVector2D(0.0, 1.0),
			FLinearColor(StampMin.X, StampMax.Y, 0.0f, 1.0f),
			HitProxyId);

		BatchedElements->AddTriangle(V0, V1, V2, BatchedElementParameters, SE_BLEND_Translucent);
		BatchedElements->AddTriangle(V0, V2, V3, BatchedElementParameters, SE_BLEND_Translucent);
		bDrewAnyBrush = true;
	}

	if (!bDrewAnyBrush)
	{
		return false;
	}

	Canvas.Flush_GameThread(false);

	ENQUEUE_RENDER_COMMAND(UpdateQuickSDFUVBrushesPaintRTCommand)(
		[RTResource](FRHICommandListImmediate& RHICmdList)
		{
			TransitionAndCopyTexture(RHICmdList, RTResource->GetRenderTargetTexture(), RTResource->TextureRHI, {});
		});

	if (OutDirtyRect && bHasBatchDirtyRect)
	{
		*OutDirtyRect = BatchDirtyRect;
	}
	return true;
}
