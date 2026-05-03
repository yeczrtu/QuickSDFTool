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
	FQuickSDFBipolarChannelPackingTest,
	"QuickSDFTool.Core.BipolarChannelPacking",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FQuickSDFBipolarChannelPackingTest::RunTest(const FString& Parameters)
{
	FMaskData Mask0;
	Mask0.TargetT = 0.0f;
	Mask0.bIsOpposite = false;
	Mask0.SDF = { 1.0, -1.0, 1.0, -1.0 };

	FMaskData Mask90;
	Mask90.TargetT = 0.5f;
	Mask90.bIsOpposite = false;
	Mask90.SDF = { -1.0, 1.0, 1.0, -1.0 };

	FMaskData Mask180;
	Mask180.TargetT = 1.0f;
	Mask180.bIsOpposite = false;
	Mask180.SDF = { -1.0, 1.0, -1.0, 1.0 };

	TArray<FVector4f> Combined;
	const TArray<FMaskData> Masks = { Mask0, Mask90, Mask180 };
	FSDFProcessor::CombineSDFs(Masks, Combined, 4, 1, ESDFOutputFormat::Bipolar, false);

	TestEqual(TEXT("0-90 enter value remains in internal R"), Combined[0].X, 0.5f);
	TestEqual(TEXT("0-90 exit value remains in internal B"), Combined[1].Z, 0.5f);
	TestEqual(TEXT("90-180 enter value remains in internal G"), Combined[2].Y, 0.5f);
	TestEqual(TEXT("90-180 exit value remains in internal A"), Combined[3].W, 0.5f);

	const TArray<FVector4f> SwizzleField = { FVector4f(0.25f, 0.5f, 0.75f, 1.0f) };
	const TArray<FFloat16Color> SwizzledPixels = FSDFProcessor::DownscaleAndConvert(SwizzleField, 1, 1, 1);
	TestEqual(TEXT("Output R keeps internal R"), SwizzledPixels[0].R.GetFloat(), 0.25f);
	TestEqual(TEXT("Output G receives internal A"), SwizzledPixels[0].G.GetFloat(), 1.0f);
	TestEqual(TEXT("Output B keeps internal B"), SwizzledPixels[0].B.GetFloat(), 0.75f);
	TestEqual(TEXT("Output A receives internal G"), SwizzledPixels[0].A.GetFloat(), 0.5f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FQuickSDFIslandMirrorApplyTest,
	"QuickSDFTool.Core.IslandMirrorApply",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FQuickSDFIslandMirrorApplyTest::RunTest(const FString& Parameters)
{
	TArray<FVector4f> Field = {
		FVector4f(0.1f, 0.0f, 0.3f, 0.0f),
		FVector4f(0.2f, 0.0f, 0.4f, 0.0f),
		FVector4f(0.8f, 0.0f, 0.6f, 0.0f),
		FVector4f(0.9f, 0.0f, 0.7f, 0.0f)
	};

	QuickSDFPaintToolPrivate::FQuickSDFIslandMirrorChart Source;
	Source.Key = TEXT("Source");
	Source.ChartID = 1;
	Source.UVMin = FVector2f(0.0f, 0.0f);
	Source.UVMax = FVector2f(0.5f, 1.0f);

	QuickSDFPaintToolPrivate::FQuickSDFIslandMirrorChart Target;
	Target.Key = TEXT("Target");
	Target.ChartID = 2;
	Target.UVMin = FVector2f(0.5f, 0.0f);
	Target.UVMax = FVector2f(1.0f, 1.0f);

	FQuickSDFIslandMirrorPair Pair;
	Pair.SourceIslandKey = Source.Key;
	Pair.TargetIslandKey = Target.Key;
	Pair.Transform = EQuickSDFIslandMirrorTransform::FlipU;

	const TArray<int32> PixelCharts = { 1, 1, 2, 2 };
	const TArray<uint8> AmbiguousFlags = { 0, 0, 0, 0 };
	const QuickSDFPaintToolPrivate::FQuickSDFIslandMirrorApplyResult Result = QuickSDFPaintToolPrivate::ApplyIslandMirrorToCombinedField(
		Field,
		4,
		1,
		true,
		{ Source, Target },
		PixelCharts,
		AmbiguousFlags,
		{ Pair });

	TestEqual(TEXT("Target island pixels are mirrored from source"), Result.MirroredPixels, 2);
	TestEqual(TEXT("First target pixel writes mirrored exit value to internal G"), Field[2].Y, 0.4f);
	TestEqual(TEXT("Second target pixel writes mirrored exit value to internal G"), Field[3].Y, 0.3f);
	TestEqual(TEXT("First target pixel writes mirrored enter value to internal A"), Field[2].W, 0.2f);
	TestEqual(TEXT("Second target pixel writes mirrored enter value to internal A"), Field[3].W, 0.1f);

	const TArray<FFloat16Color> IslandPixels = FSDFProcessor::DownscaleAndConvert({ Field[2] }, 1, 1, 1);
	TestTrue(TEXT("Final island G receives mirrored enter value"), FMath::IsNearlyEqual(IslandPixels[0].G.GetFloat(), 0.2f, 0.001f));
	TestTrue(TEXT("Final island A receives mirrored exit value"), FMath::IsNearlyEqual(IslandPixels[0].A.GetFloat(), 0.4f, 0.001f));

	TArray<FVector4f> SelfField = {
		FVector4f(0.1f, 0.0f, 0.0f, 0.0f),
		FVector4f(0.2f, 0.0f, 0.0f, 0.0f),
		FVector4f(0.3f, 0.0f, 0.0f, 0.0f),
		FVector4f(0.4f, 0.0f, 0.0f, 0.0f)
	};

	QuickSDFPaintToolPrivate::FQuickSDFIslandMirrorChart Self;
	Self.Key = TEXT("Self");
	Self.ChartID = 3;
	Self.UVMin = FVector2f(0.0f, 0.0f);
	Self.UVMax = FVector2f(1.0f, 1.0f);

	FQuickSDFIslandMirrorPair SelfPair;
	SelfPair.SourceIslandKey = Self.Key;
	SelfPair.TargetIslandKey = Self.Key;
	SelfPair.Transform = EQuickSDFIslandMirrorTransform::FlipU;

	const QuickSDFPaintToolPrivate::FQuickSDFIslandMirrorApplyResult SelfResult = QuickSDFPaintToolPrivate::ApplyIslandMirrorToCombinedField(
		SelfField,
		4,
		1,
		false,
		{ Self },
		{ 3, 3, 3, 3 },
		{ 0, 0, 0, 0 },
		{ SelfPair });

	TestEqual(TEXT("Self island flip mirrors every pixel in-place"), SelfResult.MirroredPixels, 4);
	TestEqual(TEXT("Self island internal A is horizontally flipped at the first pixel"), SelfField[0].W, 0.4f);
	TestEqual(TEXT("Self island internal A is horizontally flipped at the last pixel"), SelfField[3].W, 0.1f);
	TestEqual(TEXT("Self island G is unused for monopolar output"), SelfField[0].Y, 1.0f);
	TestEqual(TEXT("Self island B is unused for monopolar output"), SelfField[0].Z, 1.0f);
	const TArray<FFloat16Color> SelfPixels = FSDFProcessor::DownscaleAndConvert({ SelfField[0] }, 1, 1, 1);
	TestTrue(TEXT("Final self island G is horizontally flipped at the first pixel"), FMath::IsNearlyEqual(SelfPixels[0].G.GetFloat(), 0.4f, 0.001f));
	TestEqual(TEXT("Final self island A remains unused for monopolar output"), SelfPixels[0].A.GetFloat(), 1.0f);
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
