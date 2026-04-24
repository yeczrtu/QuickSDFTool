#pragma once
#include "CoreMinimal.h"

enum class EQuickSDFStrokeInputMode : uint8
{
	None,
	MeshSurface,
	TexturePreview
};

struct FQuickSDFStrokeSample
{
	FVector3d WorldPos = FVector3d::Zero();
	FVector2f UV = FVector2f::ZeroVector;
	float LocalUVScale = 1.0f;
};