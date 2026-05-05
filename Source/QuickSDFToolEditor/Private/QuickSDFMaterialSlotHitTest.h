#pragma once

#include "CoreMinimal.h"

class UMeshComponent;

namespace QuickSDFMaterialSlotHitTest
{
struct FResult
{
	int32 MaterialSlotIndex = INDEX_NONE;
	int32 TriangleID = INDEX_NONE;
	double HitDistance = 0.0;
};

bool HitTestMaterialSlot(UMeshComponent* Component, const FRay& WorldRay, FResult& OutResult);
}
