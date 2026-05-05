#include "QuickSDFMaterialSlotHitTest.h"

#include "Components/MeshComponent.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "DynamicMesh/DynamicMeshAABBTree3.h"
#include "QuickSDFMeshComponentAdapter.h"
#include "Spatial/SpatialInterfaces.h"

namespace
{
constexpr double QuickSDFMaterialSlotHitTestRayLength = 100000.0;
}

bool QuickSDFMaterialSlotHitTest::HitTestMaterialSlot(UMeshComponent* Component, const FRay& WorldRay, FResult& OutResult)
{
	OutResult = FResult();
	if (!Component)
	{
		return false;
	}

	TUniquePtr<FQuickSDFMeshComponentAdapter> MeshAdapter = FQuickSDFMeshComponentAdapter::Make(Component);
	if (!MeshAdapter.IsValid())
	{
		return false;
	}

	UE::Geometry::FDynamicMesh3 Mesh;
	TMap<int32, int32> TriangleMaterialSlots;
	if (!MeshAdapter->BuildDynamicMesh(Mesh, TriangleMaterialSlots) || Mesh.TriangleCount() <= 0)
	{
		return false;
	}

	UE::Geometry::FDynamicMeshAABBTree3 Spatial;
	Spatial.SetMesh(&Mesh, true);

	const FTransform ComponentTransform = Component->GetComponentTransform();
	const FRay LocalRay(
		ComponentTransform.InverseTransformPosition(WorldRay.Origin),
		ComponentTransform.InverseTransformVector(WorldRay.Direction));
	UE::Geometry::IMeshSpatial::FQueryOptions QueryOptions(QuickSDFMaterialSlotHitTestRayLength);

	double HitDistance = QuickSDFMaterialSlotHitTestRayLength;
	int32 HitTriangleID = INDEX_NONE;
	FVector3d BaryCoords(0.0, 0.0, 0.0);
	if (!Spatial.FindNearestHitTriangle(LocalRay, HitDistance, HitTriangleID, BaryCoords, QueryOptions) ||
		HitTriangleID == INDEX_NONE)
	{
		return false;
	}

	const int32* MaterialSlot = TriangleMaterialSlots.Find(HitTriangleID);
	if (!MaterialSlot || *MaterialSlot < 0 || *MaterialSlot >= Component->GetNumMaterials())
	{
		return false;
	}

	OutResult.MaterialSlotIndex = *MaterialSlot;
	OutResult.TriangleID = HitTriangleID;
	OutResult.HitDistance = HitDistance;
	return true;
}
