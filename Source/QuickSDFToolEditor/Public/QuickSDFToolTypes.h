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
	int32 TriangleID = INDEX_NONE;
	int32 PaintChartID = INDEX_NONE;
	FVector2D ScreenPosition = FVector2D::ZeroVector;
	FVector3d RayOrigin = FVector3d::Zero();
	FVector3d RayDirection = FVector3d(0.0, 0.0, 1.0);
};

UENUM(BlueprintType)
enum class ESDFOutputFormat : uint8
{
	Monopolar UMETA(DisplayName = "Monopolar (Single Side)"),
	Bipolar   UMETA(DisplayName = "Bipolar (Both Sides)")
};

UENUM(BlueprintType)
enum class EQuickSDFThresholdMapOutputMode : uint8
{
	Native UMETA(DisplayName = "RGBA"),
	Grayscale UMETA(DisplayName = "Grayscale"),
	LilToonCompatible UMETA(DisplayName = "liltoon")
};

UENUM(BlueprintType)
enum class EQuickSDFClipDirection : uint8
{
	Auto UMETA(DisplayName = "Auto"),
	WhiteExpands UMETA(DisplayName = "White Expands"),
	WhiteShrinks UMETA(DisplayName = "White Shrinks")
};
