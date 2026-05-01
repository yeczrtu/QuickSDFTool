#include "QuickSDFMonotonicGuard.h"

namespace QuickSDFMonotonicGuard
{
bool IsWhite(const FColor& Color)
{
	return Color.R >= 127;
}

bool GetProjectedStrokeState(const FColor& BeforeColor, const FColor& AfterColor)
{
	if (AfterColor.R > BeforeColor.R)
	{
		return true;
	}

	if (AfterColor.R < BeforeColor.R)
	{
		return false;
	}

	return IsWhite(AfterColor);
}

bool IsTransitionAllowed(bool bFromWhite, bool bToWhite, float FromAngle, float ToAngle, EQuickSDFClipDirection ClipDirection)
{
	if (bFromWhite == bToWhite || FMath::IsNearlyEqual(FromAngle, ToAngle))
	{
		return true;
	}

	EQuickSDFClipDirection EffectiveDirection = ClipDirection;
	if (EffectiveDirection == EQuickSDFClipDirection::Auto)
	{
		const float MidAngle = (FromAngle + ToAngle) * 0.5f;
		EffectiveDirection = MidAngle <= 90.0f
			? EQuickSDFClipDirection::WhiteExpands
			: EQuickSDFClipDirection::WhiteShrinks;
	}

	const int32 AngleSign = ToAngle > FromAngle ? 1 : -1;
	const int32 StateDelta = static_cast<int32>(bToWhite) - static_cast<int32>(bFromWhite);
	const int32 DirectionalDelta = StateDelta * AngleSign;

	if (EffectiveDirection == EQuickSDFClipDirection::WhiteExpands)
	{
		return DirectionalDelta >= 0;
	}

	return DirectionalDelta <= 0;
}

int32 CountViolations(TConstArrayView<bool> States, TConstArrayView<float> Angles, EQuickSDFClipDirection ClipDirection)
{
	const int32 Count = FMath::Min(States.Num(), Angles.Num());
	int32 Violations = 0;
	for (int32 Index = 0; Index + 1 < Count; ++Index)
	{
		if (!IsTransitionAllowed(States[Index], States[Index + 1], Angles[Index], Angles[Index + 1], ClipDirection))
		{
			++Violations;
		}
	}
	return Violations;
}
}

#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FQuickSDFMonotonicGuardWhiteExpandsTest,
	"QuickSDFTool.MonotonicGuard.WhiteExpands",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FQuickSDFMonotonicGuardWhiteExpandsTest::RunTest(const FString& Parameters)
{
	const TArray<float> Angles = { 0.0f, 45.0f, 90.0f };
	const TArray<bool> InvalidStates = { false, true, false };
	const TArray<bool> ValidStates = { false, false, true };

	TestEqual(
		TEXT("Black -> White -> Black violates white-expands monotonicity"),
		QuickSDFMonotonicGuard::CountViolations(InvalidStates, Angles, EQuickSDFClipDirection::WhiteExpands),
		1);

	TestEqual(
		TEXT("Black -> Black -> White is valid for white-expands"),
		QuickSDFMonotonicGuard::CountViolations(ValidStates, Angles, EQuickSDFClipDirection::WhiteExpands),
		0);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FQuickSDFMonotonicGuardWhiteShrinksTest,
	"QuickSDFTool.MonotonicGuard.WhiteShrinks",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FQuickSDFMonotonicGuardWhiteShrinksTest::RunTest(const FString& Parameters)
{
	const TArray<float> Angles = { 90.0f, 135.0f, 180.0f };
	const TArray<bool> InvalidStates = { true, false, true };
	const TArray<bool> ValidStates = { true, false, false };

	TestEqual(
		TEXT("White -> Black -> White violates white-shrinks monotonicity"),
		QuickSDFMonotonicGuard::CountViolations(InvalidStates, Angles, EQuickSDFClipDirection::WhiteShrinks),
		1);

	TestEqual(
		TEXT("White -> Black -> Black is valid for white-shrinks"),
		QuickSDFMonotonicGuard::CountViolations(ValidStates, Angles, EQuickSDFClipDirection::WhiteShrinks),
		0);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FQuickSDFMonotonicGuardDirectionOrderTest,
	"QuickSDFTool.MonotonicGuard.DirectionOrder",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FQuickSDFMonotonicGuardDirectionOrderTest::RunTest(const FString& Parameters)
{
	const TArray<float> IncreasingAngles = { 0.0f, 45.0f, 90.0f };
	const TArray<float> DecreasingAngles = { 90.0f, 45.0f, 0.0f };
	const TArray<bool> ExpandsIncreasingStates = { false, false, true };
	const TArray<bool> ExpandsDecreasingStates = { true, false, false };
	const TArray<bool> ShrinksIncreasingStates = { true, false, false };
	const TArray<bool> ShrinksDecreasingStates = { false, false, true };

	TestEqual(
		TEXT("Manual white-expands supports increasing angle order"),
		QuickSDFMonotonicGuard::CountViolations(ExpandsIncreasingStates, IncreasingAngles, EQuickSDFClipDirection::WhiteExpands),
		0);

	TestEqual(
		TEXT("Manual white-expands supports decreasing angle order"),
		QuickSDFMonotonicGuard::CountViolations(ExpandsDecreasingStates, DecreasingAngles, EQuickSDFClipDirection::WhiteExpands),
		0);

	TestEqual(
		TEXT("Manual white-shrinks supports increasing angle order"),
		QuickSDFMonotonicGuard::CountViolations(ShrinksIncreasingStates, IncreasingAngles, EQuickSDFClipDirection::WhiteShrinks),
		0);

	TestEqual(
		TEXT("Manual white-shrinks supports decreasing angle order"),
		QuickSDFMonotonicGuard::CountViolations(ShrinksDecreasingStates, DecreasingAngles, EQuickSDFClipDirection::WhiteShrinks),
		0);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FQuickSDFMonotonicGuardRangeTest,
	"QuickSDFTool.MonotonicGuard.SelectedRange",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FQuickSDFMonotonicGuardRangeTest::RunTest(const FString& Parameters)
{
	const TArray<float> FullAngles = { 0.0f, 45.0f, 90.0f, 135.0f };
	const TArray<bool> FullStates = { false, true, false, false };

	TestEqual(
		TEXT("Full range sees the middle reversal"),
		QuickSDFMonotonicGuard::CountViolations(FullStates, FullAngles, EQuickSDFClipDirection::WhiteExpands),
		1);

	const TArray<float> AfterAngles = { 90.0f, 135.0f };
	const TArray<bool> AfterStates = { false, false };
	TestEqual(
		TEXT("After range only evaluates its own selected masks"),
		QuickSDFMonotonicGuard::CountViolations(AfterStates, AfterAngles, EQuickSDFClipDirection::WhiteExpands),
		0);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FQuickSDFMonotonicGuardSoftStrokeProjectionTest,
	"QuickSDFTool.MonotonicGuard.SoftStrokeProjection",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FQuickSDFMonotonicGuardSoftStrokeProjectionTest::RunTest(const FString& Parameters)
{
	const TArray<float> Angles = { 0.0f, 45.0f, 90.0f };
	const TArray<bool> ActualSoftWhiteStates = { false, false, false };
	const TArray<bool> ProjectedSoftWhiteStates = {
		false,
		QuickSDFMonotonicGuard::GetProjectedStrokeState(FColor(0, 0, 0, 255), FColor(96, 96, 96, 255)),
		false
	};

	TestEqual(
		TEXT("A soft white stroke can stay below the binary threshold"),
		QuickSDFMonotonicGuard::CountViolations(ActualSoftWhiteStates, Angles, EQuickSDFClipDirection::WhiteExpands),
		0);

	TestEqual(
		TEXT("Projected soft white stroke is clipped when full white would break monotonicity"),
		QuickSDFMonotonicGuard::CountViolations(ProjectedSoftWhiteStates, Angles, EQuickSDFClipDirection::WhiteExpands),
		1);

	const bool bProjectedSoftBlack = QuickSDFMonotonicGuard::GetProjectedStrokeState(FColor(255, 255, 255, 255), FColor(160, 160, 160, 255));
	TestFalse(TEXT("A soft black stroke above the binary threshold still projects to black"), bProjectedSoftBlack);

	return true;
}
#endif
