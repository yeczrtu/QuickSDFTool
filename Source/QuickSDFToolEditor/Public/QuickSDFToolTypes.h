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

UENUM(BlueprintType)
enum class ESDFOutputFormat : uint8
{
	Monopolar UMETA(DisplayName = "Monopolar (Single Side)"),
	Bipolar   UMETA(DisplayName = "Bipolar (Both Sides)")
};

UENUM(BlueprintType)
enum class EQuickSDFClipDirection : uint8
{
	Auto UMETA(DisplayName = "Auto"),
	WhiteExpands UMETA(DisplayName = "White Expands"),
	WhiteShrinks UMETA(DisplayName = "White Shrinks")
};
