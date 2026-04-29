#include "QuickSDFMeshComponentAdapter.h"

#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "MeshDescription.h"
#include "MeshDescriptionToDynamicMesh.h"

namespace
{
void BuildTriangleMaterialSlotMap(const FMeshDescription& MeshDescription, const UE::Geometry::FDynamicMesh3& DynamicMesh, TMap<int32, int32>& OutTriangleMaterialSlots)
{
	OutTriangleMaterialSlots.Reset();

	TArray<int32> DynamicTriangleIDs;
	DynamicTriangleIDs.Reserve(DynamicMesh.TriangleCount());
	for (int32 TriangleID : DynamicMesh.TriangleIndicesItr())
	{
		DynamicTriangleIDs.Add(TriangleID);
	}

	int32 TriangleIndex = 0;
	for (const FTriangleID MeshDescriptionTriangleID : MeshDescription.Triangles().GetElementIDs())
	{
		if (!DynamicTriangleIDs.IsValidIndex(TriangleIndex))
		{
			break;
		}

		const FPolygonID PolygonID = MeshDescription.GetTrianglePolygon(MeshDescriptionTriangleID);
		if (PolygonID != INDEX_NONE)
		{
			const FPolygonGroupID PolygonGroupID = MeshDescription.GetPolygonPolygonGroup(PolygonID);
			OutTriangleMaterialSlots.Add(DynamicTriangleIDs[TriangleIndex], PolygonGroupID.GetValue());
		}

		++TriangleIndex;
	}
}

void AddMaterialSlotsFromComponent(const UMeshComponent* MeshComponent, TArray<int32>& OutMaterialSlots, int32 TargetMaterialSlot)
{
	OutMaterialSlots.Reset();
	if (!MeshComponent)
	{
		return;
	}

	if (TargetMaterialSlot >= 0)
	{
		if (TargetMaterialSlot < MeshComponent->GetNumMaterials())
		{
			OutMaterialSlots.Add(TargetMaterialSlot);
		}
		return;
	}

	const int32 NumMaterials = MeshComponent->GetNumMaterials();
	for (int32 MaterialSlot = 0; MaterialSlot < NumMaterials; ++MaterialSlot)
	{
		OutMaterialSlots.Add(MaterialSlot);
	}

	if (OutMaterialSlots.Num() == 0)
	{
		OutMaterialSlots.Add(0);
	}
}

class FQuickSDFStaticMeshComponentAdapter : public FQuickSDFMeshComponentAdapter
{
public:
	explicit FQuickSDFStaticMeshComponentAdapter(UStaticMeshComponent* InComponent)
		: Component(InComponent)
	{
	}

	virtual bool BuildDynamicMesh(UE::Geometry::FDynamicMesh3& OutMesh, TMap<int32, int32>& OutTriangleMaterialSlots) const override
	{
		UStaticMeshComponent* StaticMeshComponent = Component.Get();
		UStaticMesh* StaticMesh = StaticMeshComponent ? StaticMeshComponent->GetStaticMesh() : nullptr;
		const FMeshDescription* MeshDescription = StaticMesh ? StaticMesh->GetMeshDescription(0) : nullptr;
		if (!MeshDescription)
		{
			return false;
		}

		FMeshDescriptionToDynamicMesh Converter;
		Converter.Convert(MeshDescription, OutMesh);
		BuildTriangleMaterialSlotMap(*MeshDescription, OutMesh, OutTriangleMaterialSlots);
		return OutMesh.TriangleCount() > 0;
	}

	virtual void GetMaterialSlots(TArray<int32>& OutMaterialSlots, int32 TargetMaterialSlot) const override
	{
		AddMaterialSlotsFromComponent(Component.Get(), OutMaterialSlots, TargetMaterialSlot);
	}

private:
	TWeakObjectPtr<UStaticMeshComponent> Component;
};

class FQuickSDFSkeletalMeshComponentAdapter : public FQuickSDFMeshComponentAdapter
{
public:
	explicit FQuickSDFSkeletalMeshComponentAdapter(USkeletalMeshComponent* InComponent)
		: Component(InComponent)
	{
	}

	virtual bool BuildDynamicMesh(UE::Geometry::FDynamicMesh3& OutMesh, TMap<int32, int32>& OutTriangleMaterialSlots) const override
	{
		USkeletalMeshComponent* SkeletalMeshComponent = Component.Get();
		USkeletalMesh* SkeletalMesh = SkeletalMeshComponent ? SkeletalMeshComponent->GetSkeletalMeshAsset() : nullptr;
		const FMeshDescription* MeshDescription = (SkeletalMesh && SkeletalMesh->HasMeshDescription(0)) ? SkeletalMesh->GetMeshDescription(0) : nullptr;
		if (!MeshDescription)
		{
			return false;
		}

		FMeshDescriptionToDynamicMesh Converter;
		Converter.Convert(MeshDescription, OutMesh);
		BuildTriangleMaterialSlotMap(*MeshDescription, OutMesh, OutTriangleMaterialSlots);
		return OutMesh.TriangleCount() > 0;
	}

	virtual void GetMaterialSlots(TArray<int32>& OutMaterialSlots, int32 TargetMaterialSlot) const override
	{
		AddMaterialSlotsFromComponent(Component.Get(), OutMaterialSlots, TargetMaterialSlot);
	}

private:
	TWeakObjectPtr<USkeletalMeshComponent> Component;
};
}

TUniquePtr<FQuickSDFMeshComponentAdapter> FQuickSDFMeshComponentAdapter::Make(UMeshComponent* MeshComponent)
{
	if (UStaticMeshComponent* StaticMeshComponent = Cast<UStaticMeshComponent>(MeshComponent))
	{
		return MakeUnique<FQuickSDFStaticMeshComponentAdapter>(StaticMeshComponent);
	}

	if (USkeletalMeshComponent* SkeletalMeshComponent = Cast<USkeletalMeshComponent>(MeshComponent))
	{
		return MakeUnique<FQuickSDFSkeletalMeshComponentAdapter>(SkeletalMeshComponent);
	}

	return nullptr;
}
