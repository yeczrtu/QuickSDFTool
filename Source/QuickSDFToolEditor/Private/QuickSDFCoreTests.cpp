#include "QuickSDFAsset.h"
#include "QuickSDFMaskImportModel.h"
#include "QuickSDFPaintToolPrivate.h"
#include "QuickSDFTextureSetSync.h"
#include "QuickSDFToolProperties.h"
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

	UQuickSDFToolProperties* DefaultProperties = NewObject<UQuickSDFToolProperties>();
	TestEqual(TEXT("Default symmetry mode is Auto"), static_cast<uint8>(DefaultProperties->SymmetryMode), static_cast<uint8>(EQuickSDFSymmetryMode::Auto));
	TestTrue(TEXT("Auto symmetry uses the 0-90 editing range"), DefaultProperties->UsesFrontHalfAngles());
	DefaultProperties->SetSymmetryEnabled(false);
	TestEqual(TEXT("Compact toggle off selects full 0-180 painting"), static_cast<uint8>(DefaultProperties->SymmetryMode), static_cast<uint8>(EQuickSDFSymmetryMode::None180));
	DefaultProperties->SetSymmetryEnabled(true);
	TestEqual(TEXT("Compact toggle on returns to Auto"), static_cast<uint8>(DefaultProperties->SymmetryMode), static_cast<uint8>(EQuickSDFSymmetryMode::Auto));

	TestEqual(TEXT("Zero bake offset preserves 0 degrees"), DefaultProperties->GetMaterialAngle(0.0f), 0.0f);
	TestEqual(TEXT("Zero bake offset preserves 90 degrees"), DefaultProperties->GetMaterialAngle(90.0f), 90.0f);

	DefaultProperties->BakeAngleOffsetDegrees = 20.0f;
	TestTrue(TEXT("0-90 offset maps 0 degrees to negative offset"), FMath::IsNearlyEqual(DefaultProperties->GetMaterialAngle(0.0f), -20.0f));
	TestTrue(TEXT("0-90 offset keeps 90 degrees fixed"), FMath::IsNearlyEqual(DefaultProperties->GetMaterialAngle(90.0f), 90.0f));

	DefaultProperties->SetSymmetryEnabled(false);
	TestTrue(TEXT("0-180 offset maps 0 degrees to negative offset"), FMath::IsNearlyEqual(DefaultProperties->GetMaterialAngle(0.0f), -20.0f));
	TestTrue(TEXT("0-180 offset keeps 90 degrees fixed"), FMath::IsNearlyEqual(DefaultProperties->GetMaterialAngle(90.0f), 90.0f));
	TestTrue(TEXT("0-180 offset expands 180 degrees above the authored range"), FMath::IsNearlyEqual(DefaultProperties->GetMaterialAngle(180.0f), 200.0f));

	DefaultProperties->SetSymmetryEnabled(true);
	DefaultProperties->BakeAngleOffsetDegrees = 20.0f;
	TestTrue(TEXT("Image bake shift is added after the global bake offset logic"), FMath::IsNearlyEqual(DefaultProperties->GetMaterialAngle(45.0f, 5.0f), 40.0f));

	DefaultProperties->BakeAngleOffsetDegrees = 0.0f;
	DefaultProperties->TargetAngles = { 13.0f, 26.0f, 39.0f };
	DefaultProperties->TargetAngleOffsetDeltas = { 0.0f, -30.0f, 0.0f };
	const float ClampedNegativeMiddleShift = DefaultProperties->GetClampedAngleOffsetDelta(1, -30.0f);
	DefaultProperties->TargetAngleOffsetDeltas[1] = ClampedNegativeMiddleShift;
	float MinPreviewAngle = 0.0f;
	float MaxPreviewAngle = 90.0f;
	DefaultProperties->GetAngleOffsetPreviewRange(1, MinPreviewAngle, MaxPreviewAngle);
	TestTrue(TEXT("Per-image offset preview is clamped above previous key"), DefaultProperties->GetMaterialAngleForKey(1) >= MinPreviewAngle - KINDA_SMALL_NUMBER);
	TestTrue(TEXT("Per-image offset preview is clamped below next key"), DefaultProperties->GetMaterialAngleForKey(1) <= MaxPreviewAngle + KINDA_SMALL_NUMBER);
	TestTrue(TEXT("Negative image bake shift moves the resolved preview angle left"), DefaultProperties->GetMaterialAngleForKey(1) < DefaultProperties->TargetAngles[1]);

	const FQuickSDFTimelineOffsetVisual NegativeMiddleVisual = QuickSDFTimelineStatus::BuildOffsetVisual(
		DefaultProperties->TargetAngles[1],
		DefaultProperties->GetMaterialAngleForKey(1),
		ClampedNegativeMiddleShift,
		90.0f);
	TestTrue(TEXT("Non-zero delta produces a visible offset vector"), NegativeMiddleVisual.bVisible);
	TestTrue(TEXT("Negative offset vector places effective coordinate left of authored"), NegativeMiddleVisual.AuthoredPercent > NegativeMiddleVisual.EffectivePercent);
	TestTrue(TEXT("Offset vector width is positive when effective differs from authored"), NegativeMiddleVisual.WidthPercent > 0.0f);

	DefaultProperties->TargetAngleOffsetDeltas[1] = DefaultProperties->GetClampedAngleOffsetDelta(1, 30.0f);
	TestTrue(TEXT("Positive image bake shift moves the resolved preview angle right"), DefaultProperties->GetMaterialAngleForKey(1) > DefaultProperties->TargetAngles[1]);
	const FQuickSDFTimelineOffsetVisual PositiveMiddleVisual = QuickSDFTimelineStatus::BuildOffsetVisual(
		DefaultProperties->TargetAngles[1],
		DefaultProperties->GetMaterialAngleForKey(1),
		DefaultProperties->TargetAngleOffsetDeltas[1],
		90.0f);
	TestTrue(TEXT("Positive offset vector places effective coordinate right of authored"), PositiveMiddleVisual.EffectivePercent > PositiveMiddleVisual.AuthoredPercent);

	TestFalse(TEXT("Zero delta hides the offset visual"), QuickSDFTimelineStatus::ShouldShowOffsetVisual(0.0f));
	TestTrue(TEXT("Positive delta shows the offset visual"), QuickSDFTimelineStatus::ShouldShowOffsetVisual(2.0f));
	TestTrue(TEXT("Negative delta shows the offset visual"), QuickSDFTimelineStatus::ShouldShowOffsetVisual(-2.0f));
	TestTrue(TEXT("0-90 timeline percent normalizes midpoint"), FMath::IsNearlyEqual(QuickSDFTimelineStatus::NormalizeAngleToTimelinePercent(45.0f, 90.0f), 0.5f));
	TestTrue(TEXT("0-180 timeline percent normalizes midpoint"), FMath::IsNearlyEqual(QuickSDFTimelineStatus::NormalizeAngleToTimelinePercent(90.0f, 180.0f), 0.5f));
	TestEqual(TEXT("Timeline percent clamps below zero"), QuickSDFTimelineStatus::NormalizeAngleToTimelinePercent(-5.0f, 90.0f), 0.0f);
	TestEqual(TEXT("Timeline percent clamps above max"), QuickSDFTimelineStatus::NormalizeAngleToTimelinePercent(270.0f, 180.0f), 1.0f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FQuickSDFAutoSymmetryAnalysisTest,
	"QuickSDFTool.Core.AutoSymmetryAnalysis",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FQuickSDFAutoSymmetryAnalysisTest::RunTest(const FString& Parameters)
{
	const TArray<int32> TextureSymmetricCharts = { 1, INDEX_NONE, INDEX_NONE, 1 };
	const float TextureScore = QuickSDFPaintToolPrivate::MeasureTextureMirrorOccupancyScore(TextureSymmetricCharts, 4, 1);
	TestTrue(TEXT("Texture-level mirrored occupancy scores as symmetric"), FMath::IsNearlyEqual(TextureScore, 1.0f));
	TestEqual(
		TEXT("Auto chooses Texture when UV occupancy is mirrored"),
		static_cast<uint8>(QuickSDFPaintToolPrivate::ResolveAutoSymmetryModeFromAnalysis(true, TextureScore, 0, 0)),
		static_cast<uint8>(EQuickSDFSymmetryMode::WholeTextureFlip90));

	const TArray<int32> IslandCharts = { 1, 1, INDEX_NONE, 1 };
	const float IslandScore = QuickSDFPaintToolPrivate::MeasureTextureMirrorOccupancyScore(IslandCharts, 4, 1);
	TestTrue(TEXT("Non-mirrored occupancy scores below the texture threshold"), IslandScore < 0.97f);
	TestEqual(
		TEXT("Auto chooses Island when UV occupancy is not texture-level mirrored"),
		static_cast<uint8>(QuickSDFPaintToolPrivate::ResolveAutoSymmetryModeFromAnalysis(true, IslandScore, 0, 0)),
		static_cast<uint8>(EQuickSDFSymmetryMode::UVIslandChannelFlip90));

	TestEqual(
		TEXT("Ambiguous UV pixels force Island"),
		static_cast<uint8>(QuickSDFPaintToolPrivate::ResolveAutoSymmetryModeFromAnalysis(true, 1.0f, 1, 0)),
		static_cast<uint8>(EQuickSDFSymmetryMode::UVIslandChannelFlip90));
	TestEqual(
		TEXT("Side-aware overlapped UVs force Overlap before generic ambiguous fallback"),
		static_cast<uint8>(QuickSDFPaintToolPrivate::ResolveAutoSymmetryModeFromAnalysis(true, 1.0f, 1, 0, true)),
		static_cast<uint8>(EQuickSDFSymmetryMode::OverlappedUVSplit90));
	TestEqual(
		TEXT("Missing UV analysis falls back to Texture"),
		static_cast<uint8>(QuickSDFPaintToolPrivate::ResolveAutoSymmetryModeFromAnalysis(false, 0.0f, 0, 0)),
		static_cast<uint8>(EQuickSDFSymmetryMode::WholeTextureFlip90));
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

	TArray<uint8> FullShapeMask;
	FullShapeMask.Init(1, 16 * 16);

	QuickSDFPaintToolPrivate::FQuickSDFIslandMirrorChart LeftIsland;
	LeftIsland.Key = TEXT("LeftIsland");
	LeftIsland.ChartID = 10;
	LeftIsland.UVMin = FVector2f(0.0f, 0.0f);
	LeftIsland.UVMax = FVector2f(0.4f, 1.0f);
	LeftIsland.Area = 0.4f;
	LeftIsland.AspectRatio = 0.4f;
	LeftIsland.ShapeMask = FullShapeMask;

	QuickSDFPaintToolPrivate::FQuickSDFIslandMirrorChart RightIsland;
	RightIsland.Key = TEXT("RightIsland");
	RightIsland.ChartID = 11;
	RightIsland.UVMin = FVector2f(0.6f, 0.0f);
	RightIsland.UVMax = FVector2f(1.0f, 1.0f);
	RightIsland.Area = 0.4f;
	RightIsland.AspectRatio = 0.4f;
	RightIsland.ShapeMask = FullShapeMask;

	TArray<FQuickSDFIslandMirrorPair> AutoPairs;
	QuickSDFPaintToolPrivate::AutoBuildIslandMirrorPairs({ LeftIsland, RightIsland }, {}, AutoPairs);
	const FQuickSDFIslandMirrorPair* RightPair = AutoPairs.FindByPredicate([](const FQuickSDFIslandMirrorPair& Pair)
	{
		return Pair.TargetIslandKey == TEXT("RightIsland");
	});
	const FQuickSDFIslandMirrorPair* LeftPair = AutoPairs.FindByPredicate([](const FQuickSDFIslandMirrorPair& Pair)
	{
		return Pair.TargetIslandKey == TEXT("LeftIsland");
	});
	TestTrue(TEXT("Auto island pairing finds the right-side target"), RightPair != nullptr);
	TestTrue(TEXT("Auto island pairing finds the left-side target"), LeftPair != nullptr);
	if (RightPair && LeftPair)
	{
		TestEqual(TEXT("Right island samples from left island"), RightPair->SourceIslandKey, FString(TEXT("LeftIsland")));
		TestEqual(TEXT("Left island samples from right island"), LeftPair->SourceIslandKey, FString(TEXT("RightIsland")));
		TestTrue(TEXT("Auto island pair confidence is accepted"), RightPair->Confidence >= 0.75f && LeftPair->Confidence >= 0.75f);
	}

	FQuickSDFIslandMirrorPair LockedPair;
	LockedPair.SourceIslandKey = TEXT("ManualSource");
	LockedPair.TargetIslandKey = TEXT("RightIsland");
	LockedPair.Transform = EQuickSDFIslandMirrorTransform::FlipV;
	LockedPair.bUserLocked = true;
	TArray<FQuickSDFIslandMirrorPair> LockedPairs;
	QuickSDFPaintToolPrivate::AutoBuildIslandMirrorPairs({ LeftIsland, RightIsland }, { LockedPair }, LockedPairs);
	const FQuickSDFIslandMirrorPair* LockedRightPair = LockedPairs.FindByPredicate([](const FQuickSDFIslandMirrorPair& Pair)
	{
		return Pair.TargetIslandKey == TEXT("RightIsland");
	});
	TestTrue(TEXT("User-locked island pair is preserved"), LockedRightPair && LockedRightPair->SourceIslandKey == TEXT("ManualSource"));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FQuickSDFOverlappedUVSplitApplyTest,
	"QuickSDFTool.Core.OverlappedUVSplitApply",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FQuickSDFOverlappedUVSplitApplyTest::RunTest(const FString& Parameters)
{
	TArray<uint8> SideFlags = {
		static_cast<uint8>(QuickSDFPaintToolPrivate::QuickSDFOverlappedUVPositiveSideFlag | QuickSDFPaintToolPrivate::QuickSDFOverlappedUVNegativeSideFlag),
		QuickSDFPaintToolPrivate::QuickSDFOverlappedUVPositiveSideFlag,
		QuickSDFPaintToolPrivate::QuickSDFOverlappedUVNegativeSideFlag,
		0
	};
	QuickSDFPaintToolPrivate::FQuickSDFOverlappedUVSplitAnalysis Analysis;
	const float OverlapScore = QuickSDFPaintToolPrivate::MeasureOverlappedUVSplitScore(SideFlags, 4, 1, &Analysis);
	TestTrue(TEXT("Overlap score counts pixels occupied by both mesh sides"), FMath::IsNearlyEqual(OverlapScore, 1.0f / 3.0f));
	TestEqual(TEXT("Overlap analysis counts positive pixels"), Analysis.PositivePixels, 2);
	TestEqual(TEXT("Overlap analysis counts negative pixels"), Analysis.NegativePixels, 2);
	TestEqual(TEXT("Overlap analysis counts shared UV pixels"), Analysis.OverlappedPixels, 1);

	TArray<FVector4f> Field = {
		FVector4f(0.1f, 0.0f, 0.3f, 0.0f),
		FVector4f(0.2f, 0.0f, 0.4f, 0.0f),
		FVector4f(0.8f, 0.0f, 0.6f, 0.0f),
		FVector4f(0.9f, 0.0f, 0.7f, 0.0f)
	};

	QuickSDFPaintToolPrivate::FQuickSDFIslandMirrorChart OverlapChart;
	OverlapChart.Key = TEXT("Overlap");
	OverlapChart.ChartID = 7;
	OverlapChart.UVMin = FVector2f(0.0f, 0.0f);
	OverlapChart.UVMax = FVector2f(1.0f, 1.0f);

	const TArray<uint8> FullOverlapFlags = {
		static_cast<uint8>(QuickSDFPaintToolPrivate::QuickSDFOverlappedUVPositiveSideFlag | QuickSDFPaintToolPrivate::QuickSDFOverlappedUVNegativeSideFlag),
		static_cast<uint8>(QuickSDFPaintToolPrivate::QuickSDFOverlappedUVPositiveSideFlag | QuickSDFPaintToolPrivate::QuickSDFOverlappedUVNegativeSideFlag),
		static_cast<uint8>(QuickSDFPaintToolPrivate::QuickSDFOverlappedUVPositiveSideFlag | QuickSDFPaintToolPrivate::QuickSDFOverlappedUVNegativeSideFlag),
		static_cast<uint8>(QuickSDFPaintToolPrivate::QuickSDFOverlappedUVPositiveSideFlag | QuickSDFPaintToolPrivate::QuickSDFOverlappedUVNegativeSideFlag)
	};
	const TArray<int32> NegativeCharts = { 7, 7, 7, 7 };
	const QuickSDFPaintToolPrivate::FQuickSDFOverlappedUVSplitApplyResult Result = QuickSDFPaintToolPrivate::ApplyOverlappedUVSplitToCombinedField(
		Field,
		4,
		1,
		true,
		{ OverlapChart },
		NegativeCharts,
		FullOverlapFlags);

	TestEqual(TEXT("Every overlapped UV pixel writes left-side channels"), Result.MirroredPixels, 4);
	TestEqual(TEXT("Right-side enter value stays in internal R"), Field[0].X, 0.1f);
	TestEqual(TEXT("Left-side exit value comes from mirrored internal B"), Field[0].Y, 0.7f);
	TestEqual(TEXT("Left-side enter value comes from mirrored internal R"), Field[0].W, 0.9f);
	TestEqual(TEXT("Last pixel mirrors from first pixel for left-side enter"), Field[3].W, 0.1f);

	const TArray<FFloat16Color> NativePixels = FSDFProcessor::DownscaleAndConvert({ Field[0] }, 1, 1, 1);
	TestTrue(TEXT("Final R keeps right-side enter"), FMath::IsNearlyEqual(NativePixels[0].R.GetFloat(), 0.1f, 0.001f));
	TestTrue(TEXT("Final G receives left-side enter"), FMath::IsNearlyEqual(NativePixels[0].G.GetFloat(), 0.9f, 0.001f));
	TestTrue(TEXT("Final B keeps right-side exit"), FMath::IsNearlyEqual(NativePixels[0].B.GetFloat(), 0.3f, 0.001f));
	TestTrue(TEXT("Final A receives left-side exit"), FMath::IsNearlyEqual(NativePixels[0].A.GetFloat(), 0.7f, 0.001f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FQuickSDFOutputFormatConversionTest,
	"QuickSDFTool.Core.OutputFormatConversion",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FQuickSDFOutputFormatConversionTest::RunTest(const FString& Parameters)
{
	UQuickSDFToolProperties* DefaultProperties = NewObject<UQuickSDFToolProperties>();
	TestNotNull(TEXT("Default properties can be created"), DefaultProperties);
	TestEqual(
		TEXT("Default SDF output format keeps legacy native output"),
		static_cast<uint8>(DefaultProperties->SDFOutputFormat),
		static_cast<uint8>(EQuickSDFThresholdMapOutputMode::Native));

	const TArray<FVector4f> Field = { FVector4f(0.25f, 0.5f, 0.75f, 1.0f) };
	const TArray<FFloat16Color> CanonicalPixels = FSDFProcessor::DownscaleCombinedFieldToCanonical(Field, 1, 1, 1);
	TestEqual(TEXT("Canonical intermediate keeps internal R in R"), CanonicalPixels[0].R.GetFloat(), 0.25f);
	TestEqual(TEXT("Canonical intermediate keeps internal G in G"), CanonicalPixels[0].G.GetFloat(), 0.5f);
	TestEqual(TEXT("Canonical intermediate keeps internal B in B"), CanonicalPixels[0].B.GetFloat(), 0.75f);
	TestEqual(TEXT("Canonical intermediate keeps internal A in A"), CanonicalPixels[0].A.GetFloat(), 1.0f);

	const TArray<FFloat16Color> NativeFromCanonical = FSDFProcessor::ConvertCanonicalToNative(CanonicalPixels, 1, 1);
	const TArray<FFloat16Color> NativePixels = FSDFProcessor::DownscaleAndConvert(Field, 1, 1, 1);
	TestEqual(TEXT("Native conversion from canonical matches legacy R"), NativeFromCanonical[0].R.GetFloat(), NativePixels[0].R.GetFloat());
	TestEqual(TEXT("Native conversion from canonical matches legacy G"), NativeFromCanonical[0].G.GetFloat(), NativePixels[0].G.GetFloat());
	TestEqual(TEXT("Native conversion from canonical matches legacy B"), NativeFromCanonical[0].B.GetFloat(), NativePixels[0].B.GetFloat());
	TestEqual(TEXT("Native conversion from canonical matches legacy A"), NativeFromCanonical[0].A.GetFloat(), NativePixels[0].A.GetFloat());

	const TArray<FFloat16Color> GrayscalePixels = FSDFProcessor::ConvertCanonicalToGrayscale(CanonicalPixels, 1, 1);
	TestEqual(TEXT("Grayscale conversion uses canonical R"), GrayscalePixels[0].R.GetFloat(), 0.25f);

	const TArray<FFloat16Color> LilToonInternalY = FSDFProcessor::ConvertCanonicalToLilToon(
		CanonicalPixels,
		1,
		1,
		EQuickSDFLilToonLeftChannelSource::InternalY);
	TestEqual(TEXT("LilToon conversion writes right SDF to R"), LilToonInternalY[0].R.GetFloat(), 0.25f);
	TestEqual(TEXT("LilToon conversion writes left SDF from internal Y to G"), LilToonInternalY[0].G.GetFloat(), 0.5f);
	TestEqual(TEXT("LilToon conversion disables normal-shadow blend in B"), LilToonInternalY[0].B.GetFloat(), 0.0f);
	TestEqual(TEXT("LilToon conversion writes full strength to A"), LilToonInternalY[0].A.GetFloat(), 1.0f);

	const TArray<FFloat16Color> LilToonInternalW = FSDFProcessor::ConvertCanonicalToLilToon(
		CanonicalPixels,
		1,
		1,
		EQuickSDFLilToonLeftChannelSource::InternalW);
	TestEqual(TEXT("Island mirror left SDF uses internal W"), LilToonInternalW[0].G.GetFloat(), 1.0f);

	TArray<FFloat16Color> TwoPixelCanonical;
	TwoPixelCanonical.SetNum(2);
	TwoPixelCanonical[0].R = 0.25f;
	TwoPixelCanonical[0].G = 0.5f;
	TwoPixelCanonical[0].B = 0.0f;
	TwoPixelCanonical[0].A = 0.75f;
	TwoPixelCanonical[1].R = 0.75f;
	TwoPixelCanonical[1].G = 0.5f;
	TwoPixelCanonical[1].B = 0.0f;
	TwoPixelCanonical[1].A = 0.25f;
	const TArray<FFloat16Color> LilToonMirroredX = FSDFProcessor::ConvertCanonicalToLilToon(
		TwoPixelCanonical,
		2,
		1,
		EQuickSDFLilToonLeftChannelSource::MirroredX);
	TestEqual(TEXT("Mirrored liltoon left side samples opposite X for first pixel"), LilToonMirroredX[0].G.GetFloat(), 0.75f);
	TestEqual(TEXT("Mirrored liltoon left side samples opposite X for second pixel"), LilToonMirroredX[1].G.GetFloat(), 0.25f);

	FQuickSDFIntermediateMetadata IslandMetadata;
	IslandMetadata.Polarity = EQuickSDFIntermediatePolarity::Monopolar;
	IslandMetadata.SymmetryMode = EQuickSDFIntermediateSymmetryMode::UVIslandChannelFlip90;
	IslandMetadata.bForceRGBA16F = true;
	TestTrue(TEXT("Island-channel intermediate metadata can force RGBA16F even for Monopolar data"), IslandMetadata.bForceRGBA16F);
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
	TestEqual(TEXT("Migrated texture set starts with zero bake angle offset"), Asset->TextureSets[0].BakeAngleOffsetDegrees, 0.0f);

	Asset->TextureSets[0].BakeAngleOffsetDegrees = 15.0f;
	UQuickSDFToolProperties* Properties = NewObject<UQuickSDFToolProperties>();
	QuickSDFTextureSetSync::SyncPropertiesFromActiveAsset(Properties, Asset);
	TestEqual(TEXT("Active texture set sync copies bake angle offset"), Properties->BakeAngleOffsetDegrees, 15.0f);
	TestEqual(TEXT("Active texture set sync copies image bake angle shift"), Properties->TargetAngleOffsetDeltas[0], Asset->TextureSets[0].AngleDataList[0].AngleOffsetDeltaDegrees);

	FQuickSDFTextureSetData& SecondTextureSet = Asset->TextureSets.AddDefaulted_GetRef();
	SecondTextureSet.MaterialSlotIndex = 1;
	SecondTextureSet.BakeAngleOffsetDegrees = 25.0f;
	Asset->ActiveTextureSetIndex = 1;
	QuickSDFTextureSetSync::SyncPropertiesFromActiveAsset(Properties, Asset);
	TestEqual(TEXT("Texture set sync reads the selected slot offset"), Properties->BakeAngleOffsetDegrees, 25.0f);
#if WITH_EDITORONLY_DATA
	TestNull(TEXT("Migrated legacy asset has no intermediate texture until SDF is regenerated"), Asset->TextureSets[0].IntermediateSDFTexture);
	TestEqual(TEXT("Intermediate metadata defaults to current schema"), Asset->TextureSets[0].IntermediateMetadata.SchemaVersion, 1);
#endif
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
		EQuickSDFApplyMode::Single,
		EQuickSDFApplyDirection::Both,
		nullptr,
		false);
	TestTrue(TEXT("Current target includes only the active key"), Current.IsKeyInTargetRange(2));
	TestFalse(TEXT("Current target excludes earlier keys"), Current.IsKeyInTargetRange(1));
	TestFalse(TEXT("Current target excludes later keys"), Current.IsKeyInTargetRange(3));
	TestEqual(TEXT("Current range starts at the previous midpoint"), Current.TargetRangeLeftAngle, 67.5f);
	TestEqual(TEXT("Current range ends at the next midpoint"), Current.TargetRangeRightAngle, 112.5f);

	const FQuickSDFTimelineRangeStatus All = QuickSDFTimelineStatus::BuildRangeStatus(
		Angles,
		2,
		EQuickSDFApplyMode::SolidRange,
		EQuickSDFApplyDirection::Both,
		nullptr,
		false);
	TestTrue(TEXT("Solid Range + Both includes all keys"), All.IsKeyInTargetRange(0) && All.IsKeyInTargetRange(3));
	TestEqual(TEXT("All range starts at timeline start"), All.TargetRangeLeftAngle, 0.0f);
	TestEqual(TEXT("All range ends at final segment midpoint to max angle"), All.TargetRangeRightAngle, 180.0f);

	const FQuickSDFTimelineRangeStatus Before = QuickSDFTimelineStatus::BuildRangeStatus(
		Angles,
		2,
		EQuickSDFApplyMode::SolidRange,
		EQuickSDFApplyDirection::Before,
		nullptr,
		false);
	TestTrue(TEXT("Before includes first key"), Before.IsKeyInTargetRange(0));
	TestTrue(TEXT("Before includes active key"), Before.IsKeyInTargetRange(2));
	TestFalse(TEXT("Before excludes later key"), Before.IsKeyInTargetRange(3));

	const FQuickSDFTimelineRangeStatus After = QuickSDFTimelineStatus::BuildRangeStatus(
		Angles,
		2,
		EQuickSDFApplyMode::SolidRange,
		EQuickSDFApplyDirection::After,
		nullptr,
		false);
	TestFalse(TEXT("After excludes earlier key"), After.IsKeyInTargetRange(1));
	TestTrue(TEXT("After includes active key"), After.IsKeyInTargetRange(2));
	TestTrue(TEXT("After includes final key"), After.IsKeyInTargetRange(3));

	const TArray<float> MirroredAngles = { 0.0f, 45.0f, 90.0f, 135.0f, 180.0f };
	const FQuickSDFTimelineRangeStatus Symmetry = QuickSDFTimelineStatus::BuildRangeStatus(
		MirroredAngles,
		1,
		EQuickSDFApplyMode::SolidRange,
		EQuickSDFApplyDirection::Both,
		nullptr,
		true);
	TestEqual(TEXT("Symmetry view keeps only 0-90 degree keys"), Symmetry.VisibleKeyIndices.Num(), 3);
	TestTrue(TEXT("Symmetry range includes visible key"), Symmetry.IsKeyInTargetRange(2));
	TestFalse(TEXT("Symmetry range excludes hidden mirrored key"), Symmetry.IsKeyInTargetRange(3));
	TestEqual(TEXT("Symmetry max range ends at 90 degrees"), Symmetry.TargetRangeRightAngle, 90.0f);

	FRuntimeFloatCurve GradientCurve;
	GradientCurve.GetRichCurve()->AddKey(0.0f, 1.0f);
	GradientCurve.GetRichCurve()->AddKey(1.0f, 0.0f);
	const FQuickSDFTimelineRangeStatus Gradient = QuickSDFTimelineStatus::BuildRangeStatus(
		Angles,
		2,
		EQuickSDFApplyMode::GradientRange,
		EQuickSDFApplyDirection::Both,
		&GradientCurve,
		false);
	TestTrue(TEXT("Gradient Range includes the active key"), Gradient.IsKeyInTargetRange(2));
	TestTrue(TEXT("Gradient Range includes distant keys even when radius reaches zero"), Gradient.IsKeyInTargetRange(0));
	TestEqual(TEXT("Gradient active key keeps full radius"), Gradient.GetKeyRadiusScale(2), 1.0f);
	TestEqual(TEXT("Gradient edge reaches zero radius"), Gradient.GetKeyRadiusScale(0), 0.0f);
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
	Input.GlobalAngleOffset = 10.0f;
	Input.AngleOffsetDelta = 2.5f;
	Input.EffectivePreviewAngle = 38.75f;
	Input.MinPreviewAngle = 20.0f;
	Input.MaxPreviewAngle = 60.0f;
	Input.bIsActive = true;
	Input.bInPaintTargetRange = true;
	Input.ApplyMode = EQuickSDFApplyMode::GradientRange;
	Input.ApplyDirection = EQuickSDFApplyDirection::Before;
	Input.RadiusScale = 0.5f;
	Input.bHasPaintRenderTarget = true;
	Input.bGuardEnabled = true;

	const FQuickSDFTimelineKeyStatus Status = QuickSDFTimelineStatus::BuildKeyStatus(Input);
	TestTrue(TEXT("Paint render target counts as mask presence"), Status.bHasMask);
	TestTrue(TEXT("Guard state is preserved"), Status.bGuardEnabled);

	const FString Tooltip = QuickSDFTimelineStatus::BuildKeyTooltip(Status).ToString();
	TestTrue(TEXT("Tooltip contains authored angle"), Tooltip.Contains(TEXT("Authored Angle: 45 deg")));
	TestTrue(TEXT("Tooltip contains bake shift"), Tooltip.Contains(TEXT("Bake Shift: +2.5 deg")));
	TestTrue(TEXT("Tooltip contains bake range"), Tooltip.Contains(TEXT("Allowed Bake Range: 20.0..60.0 deg")));
	TestTrue(TEXT("Tooltip explains authored key stays fixed"), Tooltip.Contains(TEXT("Authored key stays fixed")));
	TestTrue(TEXT("Tooltip reports apply target range"), Tooltip.Contains(TEXT("Apply Target: Included")));
	TestTrue(TEXT("Tooltip reports apply mode"), Tooltip.Contains(TEXT("Apply Mode: Gradient Range")));
	TestTrue(TEXT("Tooltip reports radius scale"), Tooltip.Contains(TEXT("Radius Scale: 50%")));

	Input.bHasPaintRenderTarget = false;
	Input.bHasTextureMask = false;
	const FQuickSDFTimelineKeyStatus Missing = QuickSDFTimelineStatus::BuildKeyStatus(Input);
	TestFalse(TEXT("Missing texture and render target reports no mask"), Missing.bHasMask);
	return true;
}

#endif
