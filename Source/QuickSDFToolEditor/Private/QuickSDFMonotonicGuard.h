#pragma once

#include "CoreMinimal.h"
#include "QuickSDFToolTypes.h"

namespace QuickSDFMonotonicGuard
{
struct FValidationResult
{
	int32 CheckedPixels = 0;
	int32 ViolationPixels = 0;
	int32 ViolationTransitions = 0;

	bool HasViolations() const
	{
		return ViolationPixels > 0 || ViolationTransitions > 0;
	}
};

bool IsWhite(const FColor& Color);
bool GetProjectedStrokeState(const FColor& BeforeColor, const FColor& AfterColor);
bool IsTransitionAllowed(bool bFromWhite, bool bToWhite, float FromAngle, float ToAngle, EQuickSDFClipDirection ClipDirection);
int32 CountViolations(TConstArrayView<bool> States, TConstArrayView<float> Angles, EQuickSDFClipDirection ClipDirection);
}
