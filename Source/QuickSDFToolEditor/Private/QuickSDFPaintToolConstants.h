#pragma once

#include "CoreMinimal.h"
#include "InteractiveToolActionSet.h"

namespace QuickSDFPaintToolPrivate
{
inline constexpr double QuickSDFStrokeSpacingFactor = 0.12;
inline constexpr int32 QuickSDFBrushMaskResolution = 256;
inline constexpr int32 QuickSDFPreviewActionIncreaseBrush = static_cast<int32>(EStandardToolActions::BaseClientDefinedActionID) + 1;
inline constexpr int32 QuickSDFActionOpenToggleMenu = static_cast<int32>(EStandardToolActions::BaseClientDefinedActionID) + 20;
inline constexpr int32 QuickSDFActionToggleBase = static_cast<int32>(EStandardToolActions::BaseClientDefinedActionID) + 30;
inline constexpr float QuickSDFMinResizeSensitivity = 0.01f;
inline constexpr int32 QuickSDFBipolarDetectionStride = 500;
inline constexpr int32 QuickSDFDefaultSymmetricAngleCount = 8;
inline constexpr int32 QuickSDFDefaultAsymmetricAngleCount = (QuickSDFDefaultSymmetricAngleCount - 1) * 2 + 1;
inline constexpr int32 QuickSDFDefaultAngleCount = QuickSDFDefaultSymmetricAngleCount;
inline constexpr int32 QuickSDFUVOverlaySupersample = 4;

inline int32 GetQuickSDFDefaultAngleCount(bool bSymmetryMode)
{
	return bSymmetryMode ? QuickSDFDefaultSymmetricAngleCount : QuickSDFDefaultAsymmetricAngleCount;
}

struct FQuickSDFUVEdgeKey
{
	FIntPoint A;
	FIntPoint B;

	bool operator==(const FQuickSDFUVEdgeKey& Other) const
	{
		return A == Other.A && B == Other.B;
	}
};

struct FQuickSDFUVOverlayEdge
{
	FVector2f A;
	FVector2f B;
};

inline uint32 GetTypeHash(const FQuickSDFUVEdgeKey& Key)
{
	return HashCombine(
		HashCombine(::GetTypeHash(Key.A.X), ::GetTypeHash(Key.A.Y)),
		HashCombine(::GetTypeHash(Key.B.X), ::GetTypeHash(Key.B.Y)));
}

inline FIntPoint QuantizeUVForOverlay(const FVector2f& UV)
{
	constexpr float QuantizeScale = 8192.0f;
	return FIntPoint(
		FMath::RoundToInt(UV.X * QuantizeScale),
		FMath::RoundToInt(UV.Y * QuantizeScale));
}

inline FQuickSDFUVEdgeKey MakeUVEdgeKey(const FVector2f& A, const FVector2f& B)
{
	FIntPoint QA = QuantizeUVForOverlay(A);
	FIntPoint QB = QuantizeUVForOverlay(B);
	if (QB.X < QA.X || (QB.X == QA.X && QB.Y < QA.Y))
	{
		Swap(QA, QB);
	}
	return { QA, QB };
}
}
