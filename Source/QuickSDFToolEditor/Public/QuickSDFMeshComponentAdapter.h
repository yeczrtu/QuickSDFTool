#pragma once

#include "CoreMinimal.h"
#include "DynamicMesh/DynamicMesh3.h"

class UMeshComponent;

class FQuickSDFMeshComponentAdapter
{
public:
	virtual ~FQuickSDFMeshComponentAdapter() = default;

	virtual bool BuildDynamicMesh(UE::Geometry::FDynamicMesh3& OutMesh, TMap<int32, int32>& OutTriangleMaterialSlots) const = 0;
	virtual void GetMaterialSlots(TArray<int32>& OutMaterialSlots, int32 TargetMaterialSlot) const = 0;

	static TUniquePtr<FQuickSDFMeshComponentAdapter> Make(UMeshComponent* MeshComponent);
};
