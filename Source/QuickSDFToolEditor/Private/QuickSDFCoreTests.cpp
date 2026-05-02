#include "QuickSDFAsset.h"
#include "QuickSDFMaskImportModel.h"
#include "QuickSDFPaintToolPrivate.h"
#include "QuickSDFTimelineStatus.h"
#include "SDFProcessor.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FQuickSDFDefaultAngleCountTest,
	"QuickSDFTool.Core.DefaultAngleCount",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FQuickSDFDefaultAngleCountTest::RunTest(const FString& Parameters)
{
	TestEqual(TEXT("Symmetry mode keeps the default 0-90 sequence at eight masks"), QuickSDFPaintToolPrivate::GetQuickSDFDefaultAngleCount(true), 8);
	TestEqual(TEXT("Asymmetric mode mirrors the sequence around 90 degrees"), QuickSDFPaintToolPrivate::GetQuickSDFDefaultAngleCount(false), 15);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FQuickSDFAngleNameParseTest,
	"QuickSDFTool.Core.AngleNameParse",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FQuickSDFAngleNameParseTest::RunTest(const FString& Parameters)
{
	float ParsedAngle = 0.0f;
	TestTrue(TEXT("Uses the last valid angle-like token"), QuickSDFPaintToolPrivate::TryExtractAngleFromName(TEXT("T_FaceMask_take02_67.5"), ParsedAngle));
	TestEqual(TEXT("Parsed final token"), ParsedAngle, 67.5f);

	TestFalse(TEXT("Rejects names without a 0-180 token"), QuickSDFPaintToolPrivate::TryExtractAngleFromName(TEXT("T_FaceMask_Final"), ParsedAngle));
	TestFalse(TEXT("Rejects out-of-range values"), QuickSDFPaintToolPrivate::TryExtractAngleFromName(TEXT("T_FaceMask_270"), ParsedAngle));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FQuickSDFSDFEdgeCasesTest,
	"QuickSDFTool.Core.SDFEdgeCases",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FQuickSDFSDFEdgeCasesTest::RunTest(const FString& Parameters)
{
	const TArray<uint8> AllWhite = { 255, 255, 255, 255 };
	const TArray<double> WhiteSDF = FSDFProcessor::GenerateSDF(AllWhite, 2, 2);
	TestEqual(TEXT("All-white SDF has the requested pixel count"), WhiteSDF.Num(), 4);
	for (double Value : WhiteSDF)
	{
		TestTrue(TEXT("All-white pixels remain inside the white region"), Value < 0.0);
	}

	const TArray<uint8> AllBlack = { 0, 0, 0, 0 };
	const TArray<double> BlackSDF = FSDFProcessor::GenerateSDF(AllBlack, 2, 2);
	TestEqual(TEXT("All-black SDF has the requested pixel count"), BlackSDF.Num(), 4);
	for (double Value : BlackSDF)
	{
		TestTrue(TEXT("All-black pixels remain outside the white region"), Value > 0.0);
	}

	const TArray<uint8> Split = { 0, 255, 0, 255 };
	const TArray<double> SplitSDF = FSDFProcessor::GenerateSDF(Split, 2, 2);
	TestEqual(TEXT("Split SDF has the requested pixel count"), SplitSDF.Num(), 4);
	TestTrue(TEXT("Black side is positive"), SplitSDF[0] > 0.0);
	TestTrue(TEXT("White side is negative"), SplitSDF[1] < 0.0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FQuickSDFAssetMigrationTest,
	"QuickSDFTool.Core.AssetMigration",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FQuickSDFAssetMigrationTest::RunTest(const FString& Parameters)
{
	UQuickSDFAsset* Asset = NewObject<UQuickSDFAsset>();
	Asset->Resolution = FIntPoint(512, 256);
	Asset->UVChannel = 2;
	Asset->AngleDataList.SetNum(2);
	Asset->AngleDataList[0].Angle = 0.0f;
	Asset->AngleDataList[0].MaskGuid = FGuid::NewGuid();
	Asset->AngleDataList[1].Angle = 90.0f;
	Asset->AngleDataList[1].MaskGuid = FGuid::NewGuid();

	Asset->MigrateLegacyDataToTextureSetsIfNeeded();

	TestEqual(TEXT("Legacy data migrates into one texture set"), Asset->TextureSets.Num(), 1);
	TestEqual(TEXT("Active texture set owns the resolution"), Asset->GetActiveResolution(), FIntPoint(512, 256));
	TestEqual(TEXT("Active texture set owns the UV channel"), Asset->GetActiveUVChannel(), 2);
	TestEqual(TEXT("Active texture set owns the angle data"), Asset->GetActiveAngleDataList().Num(), 2);
	TestEqual(TEXT("Legacy mirror remains available after migration"), Asset->AngleDataList.Num(), 2);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FQuickSDFMaskImportModelTest,
	"QuickSDFTool.Core.MaskImportModel",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FQuickSDFMaskImportModelTest::RunTest(const FString& Parameters)
{
	FQuickSDFMaskImportSource A;
	FQuickSDFMaskImportSource B;
	const FGuid SharedGuid = FGuid::NewGuid();
	A.ImportGuid = SharedGuid;
	B.ImportGuid = SharedGuid;
	A.DisplayName = TEXT("Mask_A");

	TestTrue(TEXT("Matching import GUIDs identify the same import source"), QuickSDFMaskImportModel::DoSourcesReferToSameContent(A, B));
	TestEqual(TEXT("Display name wins over missing texture name"), QuickSDFMaskImportModel::GetSourceName(A), FString(TEXT("Mask_A")));
	TestTrue(TEXT("Engine root is protected"), QuickSDFMaskImportModel::IsEngineContentPath(TEXT("/Engine")));
	TestTrue(TEXT("Engine child path is protected"), QuickSDFMaskImportModel::IsEngineContentPath(TEXT("/Engine/EditorMaterials")));
	TestFalse(TEXT("Project content path is writable"), QuickSDFMaskImportModel::IsEngineContentPath(TEXT("/Game/QuickSDF")));

	const FText CombinedWarning = QuickSDFMaskImportModel::AppendWarningText(FText::FromString(TEXT("A")), FText::FromString(TEXT("B")));
	TestEqual(TEXT("Warnings are combined predictably"), CombinedWarning.ToString(), FString(TEXT("A / B")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FQuickSDFTimelineRangeStatusTest,
	"QuickSDFTool.Core.TimelineRangeStatus",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FQuickSDFTimelineRangeStatusTest::RunTest(const FString& Parameters)
{
	const TArray<float> Angles = { 0.0f, 45.0f, 90.0f, 135.0f };

	const FQuickSDFTimelineRangeStatus Current = QuickSDFTimelineStatus::BuildRangeStatus(
		Angles,
		2,
		EQuickSDFPaintTargetMode::CurrentOnly,
		false,
		false);
	TestTrue(TEXT("Current target includes only the active key"), Current.IsKeyInTargetRange(2));
	TestFalse(TEXT("Current target excludes earlier keys"), Current.IsKeyInTargetRange(1));
	TestFalse(TEXT("Current target excludes later keys"), Current.IsKeyInTargetRange(3));
	TestEqual(TEXT("Current range starts at the previous midpoint"), Current.TargetRangeLeftAngle, 67.5f);
	TestEqual(TEXT("Current range ends at the next midpoint"), Current.TargetRangeRightAngle, 112.5f);

	const FQuickSDFTimelineRangeStatus All = QuickSDFTimelineStatus::BuildRangeStatus(
		Angles,
		2,
		EQuickSDFPaintTargetMode::CurrentOnly,
		true,
		false);
	TestTrue(TEXT("Legacy paint-all flag resolves to All"), All.IsKeyInTargetRange(0) && All.IsKeyInTargetRange(3));
	TestEqual(TEXT("All range starts at timeline start"), All.TargetRangeLeftAngle, 0.0f);
	TestEqual(TEXT("All range ends at final segment midpoint to max angle"), All.TargetRangeRightAngle, 180.0f);

	const FQuickSDFTimelineRangeStatus Before = QuickSDFTimelineStatus::BuildRangeStatus(
		Angles,
		2,
		EQuickSDFPaintTargetMode::BeforeCurrent,
		false,
		false);
	TestTrue(TEXT("Before includes first key"), Before.IsKeyInTargetRange(0));
	TestTrue(TEXT("Before includes active key"), Before.IsKeyInTargetRange(2));
	TestFalse(TEXT("Before excludes later key"), Before.IsKeyInTargetRange(3));

	const FQuickSDFTimelineRangeStatus After = QuickSDFTimelineStatus::BuildRangeStatus(
		Angles,
		2,
		EQuickSDFPaintTargetMode::AfterCurrent,
		false,
		false);
	TestFalse(TEXT("After excludes earlier key"), After.IsKeyInTargetRange(1));
	TestTrue(TEXT("After includes active key"), After.IsKeyInTargetRange(2));
	TestTrue(TEXT("After includes final key"), After.IsKeyInTargetRange(3));

	const TArray<float> MirroredAngles = { 0.0f, 45.0f, 90.0f, 135.0f, 180.0f };
	const FQuickSDFTimelineRangeStatus Symmetry = QuickSDFTimelineStatus::BuildRangeStatus(
		MirroredAngles,
		1,
		EQuickSDFPaintTargetMode::All,
		false,
		true);
	TestEqual(TEXT("Symmetry view keeps only 0-90 degree keys"), Symmetry.VisibleKeyIndices.Num(), 3);
	TestTrue(TEXT("Symmetry range includes visible key"), Symmetry.IsKeyInTargetRange(2));
	TestFalse(TEXT("Symmetry range excludes hidden mirrored key"), Symmetry.IsKeyInTargetRange(3));
	TestEqual(TEXT("Symmetry max range ends at 90 degrees"), Symmetry.TargetRangeRightAngle, 90.0f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FQuickSDFTimelineKeyStatusTest,
	"QuickSDFTool.Core.TimelineKeyStatus",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FQuickSDFTimelineKeyStatusTest::RunTest(const FString& Parameters)
{
	FQuickSDFTimelineKeyStatusInput Input;
	Input.KeyIndex = 1;
	Input.Angle = 45.0f;
	Input.bIsActive = true;
	Input.bInPaintTargetRange = true;
	Input.bHasPaintRenderTarget = true;
	Input.bGuardEnabled = true;
	Input.bHasWarning = true;
	Input.WarningMessage = FText::FromString(TEXT("Needs review"));

	const FQuickSDFTimelineKeyStatus Status = QuickSDFTimelineStatus::BuildKeyStatus(Input);
	TestTrue(TEXT("Paint render target counts as mask presence"), Status.bHasMask);
	TestTrue(TEXT("Guard state is preserved"), Status.bGuardEnabled);
	TestTrue(TEXT("Warning state is preserved"), Status.bHasWarning);

	const FString Tooltip = QuickSDFTimelineStatus::BuildKeyTooltip(Status).ToString();
	TestTrue(TEXT("Tooltip contains angle"), Tooltip.Contains(TEXT("Angle: 45 deg")));
	TestTrue(TEXT("Tooltip reports included paint target range"), Tooltip.Contains(TEXT("Paint Target: Included")));
	TestTrue(TEXT("Tooltip includes warning details"), Tooltip.Contains(TEXT("Warning: Needs review")));

	Input.bHasPaintRenderTarget = false;
	Input.bHasTextureMask = false;
	Input.bHasWarning = false;
	const FQuickSDFTimelineKeyStatus Missing = QuickSDFTimelineStatus::BuildKeyStatus(Input);
	TestFalse(TEXT("Missing texture and render target reports no mask"), Missing.bHasMask);
	return true;
}

#endif
