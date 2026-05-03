#include "QuickSDFPaintTool.h"
#include "QuickSDFPaintToolPrivate.h"
#include "QuickSDFMeshComponentAdapter.h"
#include "QuickSDFToolSubsystem.h"
#include "QuickSDFToolUI.h"
#include "QuickSDFAsset.h"
#include "SDFProcessor.h"
#include "BaseGizmos/BrushStampIndicator.h"
#include "InteractiveToolManager.h"
#include "ToolBuilderUtil.h"
#include "EngineUtils.h"
#include "Engine/DirectionalLight.h"
#include "CollisionQueryParams.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/Texture2D.h"
#include "BaseBehaviors/ClickDragBehavior.h"
#include "TargetInterfaces/PrimitiveComponentBackedTarget.h"
#include "TargetInterfaces/MeshDescriptionProvider.h"
#include "DynamicMesh/MeshTransforms.h"
#include "DynamicMesh/DynamicMeshAttributeSet.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Components/PrimitiveComponent.h"
#include "GameFramework/Actor.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/SkinnedMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Kismet/KismetRenderingLibrary.h"
#include "Intersection/IntrRay3Triangle3.h"
#include "Spatial/SpatialInterfaces.h"
#include "IndexTypes.h"
#include "Engine/Canvas.h"
#include "CanvasItem.h"
#include "CanvasTypes.h"
#include "TextureResource.h"
#include "RenderResource.h"
#include "Math/UnrealMathUtility.h"
#include "InputCoreTypes.h"
#include "HAL/PlatformApplicationMisc.h"
#include "HAL/PlatformTime.h"
#include "InteractiveToolChange.h"
#include "Misc/ScopedSlowTask.h"
#include "Misc/MessageDialog.h"
#include "DesktopPlatformModule.h"
#include "Engine/Selection.h"
#include "Framework/Application/SlateApplication.h"
#include "IDesktopPlatform.h"
#include "Misc/DefaultValueHelper.h"
#include "Containers/Ticker.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetToolsModule.h"
#include "IAssetTools.h"
#include "Misc/PackageName.h"
#include "ObjectTools.h"

#if WITH_EDITOR
#include "Editor.h"
#include "IMaterialBakingModule.h"
#include "MaterialBakingStructures.h"
#endif

#define LOCTEXT_NAMESPACE "QuickSDFPaintTool"

using namespace QuickSDFPaintToolPrivate;

void UQuickSDFPaintTool::InvalidateUVOverlayCache()
{
	bUVOverlayDirty = true;
	InvalidatePaintChartCache();
}

UTextureRenderTarget2D* UQuickSDFPaintTool::GetUVOverlayRenderTarget()
{
	if (!Properties || !Properties->bOverlayUV || !TargetMesh.IsValid() || !TargetMesh->HasAttributes())
	{
		return nullptr;
	}

	const FVector2D PreviewSize = GetPreviewSize();
	const int32 Width = FMath::Max(1, FMath::RoundToInt(PreviewSize.X)) * QuickSDFUVOverlaySupersample;
	const int32 Height = FMath::Max(1, FMath::RoundToInt(PreviewSize.Y)) * QuickSDFUVOverlaySupersample;
	const FIntPoint DesiredSize(Width, Height);

	const bool bSettingsChanged =
		CachedUVOverlaySize != DesiredSize ||
		CachedUVOverlayUVChannel != Properties->UVChannel ||
		CachedUVOverlayMaterialSlot != Properties->TargetMaterialSlot ||
		bCachedUVOverlayIsolateTargetMaterialSlot != Properties->bIsolateTargetMaterialSlot;

	if (bUVOverlayDirty || bSettingsChanged || !UVOverlayRenderTarget)
	{
		RebuildUVOverlayRenderTarget(Width, Height);
	}

	return UVOverlayRenderTarget;
}

void UQuickSDFPaintTool::RebuildUVOverlayRenderTarget(int32 Width, int32 Height)
{
	if (!Properties || !TargetMesh.IsValid() || !TargetMesh->HasAttributes())
	{
		UVOverlayRenderTarget = nullptr;
		return;
	}

	const UE::Geometry::FDynamicMeshUVOverlay* UVOverlay = TargetMesh->Attributes()->GetUVLayer(Properties->UVChannel);
	if (!UVOverlay)
	{
		UVOverlayRenderTarget = nullptr;
		return;
	}

	if (!UVOverlayRenderTarget || UVOverlayRenderTarget->SizeX != Width || UVOverlayRenderTarget->SizeY != Height)
	{
		UVOverlayRenderTarget = NewObject<UTextureRenderTarget2D>(this);
		UVOverlayRenderTarget->RenderTargetFormat = RTF_RGBA8;
		UVOverlayRenderTarget->ClearColor = FLinearColor::Transparent;
		UVOverlayRenderTarget->Filter = TF_Bilinear;
		UVOverlayRenderTarget->InitAutoFormat(Width, Height);
		UVOverlayRenderTarget->UpdateResourceImmediate(true);
	}

	FTextureRenderTargetResource* RTResource = UVOverlayRenderTarget->GameThread_GetRenderTargetResource();
	if (!RTResource)
	{
		return;
	}

	FCanvas Canvas(RTResource, nullptr, GetToolManager()->GetContextQueriesAPI()->GetCurrentEditingWorld(), GMaxRHIFeatureLevel);
	Canvas.Clear(FLinearColor::Transparent);

	auto UVToOverlay = [Width, Height](const FVector2f& UV) -> FVector2D
	{
		return FVector2D(static_cast<double>(UV.X) * Width, static_cast<double>(UV.Y) * Height);
	};

	TMap<FQuickSDFUVEdgeKey, int32> UniqueEdgeIndices;
	TArray<FQuickSDFUVOverlayEdge> UniqueEdges;
	auto AddUniqueEdge = [&UniqueEdgeIndices, &UniqueEdges](const FVector2f& A, const FVector2f& B)
	{
		const FQuickSDFUVEdgeKey EdgeKey = MakeUVEdgeKey(A, B);
		if (!UniqueEdgeIndices.Contains(EdgeKey))
		{
			UniqueEdgeIndices.Add(EdgeKey, UniqueEdges.Num());
			UniqueEdges.Add({ A, B });
		}
	};

	for (int32 Tid : TargetMesh->TriangleIndicesItr())
	{
		if (!IsTriangleInTargetMaterialSlot(Tid) || !UVOverlay->IsSetTriangle(Tid))
		{
			continue;
		}

		const UE::Geometry::FIndex3i UVIndices = UVOverlay->GetTriangle(Tid);
		const FVector2f UV0 = UVOverlay->GetElement(UVIndices.A);
		const FVector2f UV1 = UVOverlay->GetElement(UVIndices.B);
		const FVector2f UV2 = UVOverlay->GetElement(UVIndices.C);
		AddUniqueEdge(UV0, UV1);
		AddUniqueEdge(UV1, UV2);
		AddUniqueEdge(UV2, UV0);
	}

	const FLinearColor UVLineColor(0.0f, 0.42f, 0.18f, 0.045f);
	for (int32 EdgeIndex = 0; EdgeIndex < UniqueEdges.Num(); ++EdgeIndex)
	{
		const FQuickSDFUVOverlayEdge& Edge = UniqueEdges[EdgeIndex];
		FCanvasLineItem Line(UVToOverlay(Edge.A), UVToOverlay(Edge.B));
		Line.SetColor(UVLineColor);
		Line.BlendMode = SE_BLEND_Translucent;
		Line.LineThickness = 1.0f;
		Canvas.DrawItem(Line);
	}

	Canvas.Flush_GameThread(true);
	ENQUEUE_RENDER_COMMAND(UpdateQuickSDFUVOverlayRTCommand)(
		[RTResource](FRHICommandListImmediate& RHICmdList)
		{
			TransitionAndCopyTexture(RHICmdList, RTResource->GetRenderTargetTexture(), RTResource->TextureRHI, {});
		});

	CachedUVOverlaySize = FIntPoint(Width, Height);
	CachedUVOverlayUVChannel = Properties->UVChannel;
	CachedUVOverlayMaterialSlot = Properties->TargetMaterialSlot;
	bCachedUVOverlayIsolateTargetMaterialSlot = Properties->bIsolateTargetMaterialSlot;
	bUVOverlayDirty = false;
}

void UQuickSDFPaintTool::DrawQuickLineHUDPreview(FCanvas* Canvas)
{
	if (!Canvas ||
		ActiveStrokeInputMode != EQuickSDFStrokeInputMode::TexturePreview ||
		!bQuickLineActive ||
		!bHasQuickLineStartSample ||
		!bHasQuickLineEndSample ||
		QuickLineSourceSamples.Num() < 2)
	{
		return;
	}

	const FVector2D PreviewOrigin = GetPreviewOrigin();
	const FVector2D PreviewSize = GetPreviewSize();
	auto UVToScreen = [&PreviewOrigin, &PreviewSize](const FVector2f& UV) -> FVector2D
	{
		return FVector2D(
			PreviewOrigin.X + static_cast<double>(UV.X) * PreviewSize.X,
			PreviewOrigin.Y + static_cast<double>(UV.Y) * PreviewSize.Y);
	};

	const FQuickSDFStrokeSample& SourceStart = QuickLineSourceSamples[0];
	const FQuickSDFStrokeSample& SourceEnd = QuickLineSourceSamples.Last();
	TArray<FVector2D> ControlPoints;
	ControlPoints.Reserve(QuickLineSourceSamples.Num());

	for (const FQuickSDFStrokeSample& SourceSample : QuickLineSourceSamples)
	{
		const FQuickSDFStrokeSample PreviewSample = TransformQuickLineSample(
			SourceSample,
			SourceStart,
			SourceEnd,
			QuickLineStartSample,
			QuickLineEndSample);
		const FVector2D ScreenPoint = UVToScreen(PreviewSample.UV);

		if (ControlPoints.Num() == 0 || FVector2D::Distance(ControlPoints.Last(), ScreenPoint) >= 2.0)
		{
			ControlPoints.Add(ScreenPoint);
		}
		else
		{
			ControlPoints.Last() = ScreenPoint;
		}
	}

	if (ControlPoints.Num() < 2)
	{
		return;
	}

	TArray<FVector2D> DrawPoints;
	const int32 MaxDrawPoints = 96;
	DrawPoints.Reserve(FMath::Min(MaxDrawPoints, ControlPoints.Num() * 4));
	auto AddDrawPoint = [&DrawPoints, MaxDrawPoints](const FVector2D& Point)
	{
		if (DrawPoints.Num() < MaxDrawPoints)
		{
			DrawPoints.Add(Point);
		}
		else if (DrawPoints.Num() > 0)
		{
			DrawPoints.Last() = Point;
		}
	};

	AddDrawPoint(ControlPoints[0]);
	for (int32 Index = 0; Index < ControlPoints.Num() - 1; ++Index)
	{
		const FVector2D& P0 = Index > 0 ? ControlPoints[Index - 1] : ControlPoints[Index];
		const FVector2D& P1 = ControlPoints[Index];
		const FVector2D& P2 = ControlPoints[Index + 1];
		const FVector2D& P3 = Index + 2 < ControlPoints.Num() ? ControlPoints[Index + 2] : ControlPoints[Index + 1];
		const FVector2D Tangent1 = (P2 - P0) * 0.5;
		const FVector2D Tangent2 = (P3 - P1) * 0.5;
		const int32 StepCount = FMath::Clamp(FMath::CeilToInt(FVector2D::Distance(P1, P2) / 8.0), 1, 8);
		for (int32 Step = 1; Step <= StepCount; ++Step)
		{
			const float Alpha = static_cast<float>(Step) / static_cast<float>(StepCount);
			AddDrawPoint(FMath::CubicInterp(P1, Tangent1, P2, Tangent2, Alpha));
		}
	}

	const FLinearColor PreviewColor = IsPaintingShadow()
		? FLinearColor(0.05f, 0.05f, 0.05f, 0.95f)
		: FLinearColor(1.0f, 0.78f, 0.12f, 0.95f);
	const FLinearColor ShadowColor(0.0f, 0.0f, 0.0f, 0.55f);
	for (int32 Index = 1; Index < DrawPoints.Num(); ++Index)
	{
		FCanvasLineItem ShadowLine(DrawPoints[Index - 1], DrawPoints[Index]);
		ShadowLine.SetColor(ShadowColor);
		ShadowLine.BlendMode = SE_BLEND_Translucent;
		ShadowLine.LineThickness = 5.0f;
		Canvas->DrawItem(ShadowLine);

		FCanvasLineItem Line(DrawPoints[Index - 1], DrawPoints[Index]);
		Line.SetColor(PreviewColor);
		Line.BlendMode = SE_BLEND_Translucent;
		Line.LineThickness = 3.0f;
		Canvas->DrawItem(Line);
	}
}

void UQuickSDFPaintTool::DrawHUD(FCanvas* Canvas, IToolsContextRenderAPI* RenderAPI)
{
    Super::DrawHUD(Canvas, RenderAPI);

	if (!Properties)
	{
		return;
	}

    if (Properties->bShowPreview)
    {
        UTextureRenderTarget2D* RT = GetActiveRenderTarget();
        if (RT)
        {
            // 1. 繝励Ξ繝薙Η繝ｼ繝・け繧ｹ繝√Ε縺ｮ謠冗判
            PreviewCanvasOrigin = FVector2D(10.0f, 10.0f);
            PreviewCanvasSize = FVector2D(256.0f, 256.0f);
            const FVector2D PreviewOrigin = GetPreviewOrigin();
            const FVector2D PreviewSize = GetPreviewSize();
            
            if (PreviewHUDMaterial)
            {
                PreviewHUDMaterial->SetTextureParameterValue(TEXT("BaseColor"), RT);
                PreviewHUDMaterial->SetScalarParameterValue(TEXT("PreviewMode"), 0.0f);
                PreviewHUDMaterial->SetScalarParameterValue(TEXT("UVChannel"), static_cast<float>(Properties->UVChannel));
                PreviewHUDMaterial->SetScalarParameterValue(TEXT("OverlayOriginalShadow"), 0.0f);
                FCanvasTileItem TileItem(PreviewOrigin, PreviewHUDMaterial->GetRenderProxy(), PreviewSize);
                TileItem.BlendMode = SE_BLEND_Opaque;
                Canvas->DrawItem(TileItem);
            }
            else
            {
                FCanvasTileItem TileItem(PreviewOrigin, RT->GetResource(), PreviewSize, FLinearColor::White);
                TileItem.BlendMode = SE_BLEND_Opaque;
                Canvas->DrawItem(TileItem);
            }

            if (Properties->bEnableOnionSkin)
            {
                UQuickSDFToolSubsystem* Subsystem = GEditor->GetEditorSubsystem<UQuickSDFToolSubsystem>();
                if (Subsystem && Subsystem->GetActiveSDFAsset())
                {
                    UQuickSDFAsset* ActiveAsset = Subsystem->GetActiveSDFAsset();
                    
                    // Previous frame (Red)
                    if (Properties->EditAngleIndex > 0 && ActiveAsset->GetActiveAngleDataList().IsValidIndex(Properties->EditAngleIndex - 1))
                    {
                        UTextureRenderTarget2D* PrevRT = ActiveAsset->GetActiveAngleDataList()[Properties->EditAngleIndex - 1].PaintRenderTarget;
                        if (PrevRT)
                        {
                            FCanvasTileItem PrevTile(PreviewOrigin, PrevRT->GetResource(), PreviewSize, FLinearColor(1.0f, 0.0f, 0.0f, 0.5f));
                            PrevTile.BlendMode = SE_BLEND_Additive;
                            Canvas->DrawItem(PrevTile);
                        }
                    }
                    
                    // Next frame (Green)
                    if (Properties->EditAngleIndex < ActiveAsset->GetActiveAngleDataList().Num() - 1 && ActiveAsset->GetActiveAngleDataList().IsValidIndex(Properties->EditAngleIndex + 1))
                    {
                        UTextureRenderTarget2D* NextRT = ActiveAsset->GetActiveAngleDataList()[Properties->EditAngleIndex + 1].PaintRenderTarget;
                        if (NextRT)
                        {
                            FCanvasTileItem NextTile(PreviewOrigin, NextRT->GetResource(), PreviewSize, FLinearColor(0.0f, 1.0f, 0.0f, 0.5f));
                            NextTile.BlendMode = SE_BLEND_Additive;
                            Canvas->DrawItem(NextTile);
                        }
                    }
                }
            }

            if (UTextureRenderTarget2D* UVOverlayRT = GetUVOverlayRenderTarget())
            {
                FCanvasTileItem UVTile(PreviewOrigin, UVOverlayRT->GetResource(), PreviewSize, FLinearColor::White);
                UVTile.BlendMode = SE_BLEND_Translucent;
                Canvas->DrawItem(UVTile);
            }
            
            if (false && TargetMesh.IsValid() && TargetMesh->HasAttributes() && Properties->bOverlayUV)
            {
                const UE::Geometry::FDynamicMeshUVOverlay* UVOverlay = TargetMesh->Attributes()->GetUVLayer(Properties->UVChannel);
                if (UVOverlay)
                {
                    // 邱壹・濶ｲ縺ｨ荳埼乗・蠎ｦ繧定ｨｭ螳・
                    FLinearColor UVLineColor(0.0f, 1.0f, 0.0f, 0.3f); // 蜊企乗・縺ｮ邱題牡

                    for (int32 Tid : TargetMesh->TriangleIndicesItr())
                    {
                        if (!IsTriangleInTargetMaterialSlot(Tid))
                        {
                            continue;
                        }

                        if (UVOverlay->IsSetTriangle(Tid))
                        {
                            UE::Geometry::FIndex3i UVIndices = UVOverlay->GetTriangle(Tid);
                            FVector2f UV0 = UVOverlay->GetElement(UVIndices.A);
                            FVector2f UV1 = UVOverlay->GetElement(UVIndices.B);
                            FVector2f UV2 = UVOverlay->GetElement(UVIndices.C);

                            // UV(0-1) 繧・繝励Ξ繝薙Η繝ｼ縺ｮ繝斐け繧ｻ繝ｫ蠎ｧ讓吶↓螟画鋤縺吶ｋ繝ｩ繝繝髢｢謨ｰ
                            auto UVToScreen = [&](const FVector2f& UV) -> FVector2D {
                                return FVector2D(
                                    PreviewOrigin.X + (double)UV.X * PreviewSize.X,
                                    PreviewOrigin.Y + (double)UV.Y * PreviewSize.Y
                                );
                            };

                            FVector2D P0 = UVToScreen(UV0);
                            FVector2D P1 = UVToScreen(UV1);
                            FVector2D P2 = UVToScreen(UV2);

                            // 荳芽ｧ貞ｽ｢縺ｮ3霎ｺ繧呈緒逕ｻ
                            FCanvasLineItem Line0(P0, P1); Line0.SetColor(UVLineColor);
                            Canvas->DrawItem(Line0);
                            FCanvasLineItem Line1(P1, P2); Line1.SetColor(UVLineColor);
                            Canvas->DrawItem(Line1);
                            FCanvasLineItem Line2(P2, P0); Line2.SetColor(UVLineColor);
                            Canvas->DrawItem(Line2);
                        }
                    }
                }
            }

            // 2. 繝懊・繝繝ｼ縺ｮ謠冗判
            FCanvasBoxItem BorderItem(PreviewOrigin, PreviewSize);
            BorderItem.SetColor(IsInPreviewBounds(LastInputScreenPosition) ? FLinearColor::Yellow : FLinearColor::Gray);
            Canvas->DrawItem(BorderItem);
        }
    }

	DrawQuickLineHUDPreview(Canvas);

	const FString PaintModeLabel = IsPaintingShadow() ? TEXT("Shadow") : TEXT("Light");
	const FString MeshLabel = CurrentComponent.IsValid()
		? FString::Printf(TEXT("%s / %s"),
			CurrentComponent->GetOwner() ? *CurrentComponent->GetOwner()->GetActorLabel() : TEXT("Mesh"),
			*CurrentComponent->GetName())
		: FString(TEXT("No Mesh"));
	const FString TextureSetLabel = GetActiveTextureSetLabel().ToString();
	const int32 ActiveAngleIndex = Properties ? FMath::Clamp(Properties->EditAngleIndex, 0, FMath::Max(Properties->TargetAngles.Num() - 1, 0)) : 0;
	const FString AngleLabel = Properties && Properties->TargetAngles.IsValidIndex(ActiveAngleIndex)
		? FString::Printf(TEXT("%.0f deg"), Properties->TargetAngles[ActiveAngleIndex])
		: FString(TEXT("--"));
	const FString TargetModeLabel = QuickSDFToolUI::GetPaintTargetModeLabel(QuickSDFToolUI::GetPaintTargetMode(Properties)).ToString();
	const FString TextureSetStatus = Properties ? GetTextureSetStatusText(Properties->ActiveTextureSetIndex).ToString() : FString(TEXT("Idle"));
	const FString ProjectionModeLabel = Properties && Properties->BrushProjectionMode == EQuickSDFBrushProjectionMode::ViewProjected
		? FString(TEXT("View Projected"))
		: FString(TEXT("Surface / UV"));

	FCanvasTextItem SetText(
		FVector2D(10.0f, 275.0f),
		FText::FromString(FString::Printf(TEXT("%s > %s > %s"), *MeshLabel, *TextureSetLabel, *AngleLabel)),
		GEngine->GetSmallFont(),
		FLinearColor::White);
	SetText.EnableShadow(FLinearColor::Black);
	Canvas->DrawItem(SetText);

	FCanvasTextItem ModeText(
		FVector2D(10.0f, 292.0f),
		FText::FromString(FString::Printf(TEXT("Paint: %s  Target: %s  Projection: %s  Set: %s"), *PaintModeLabel, *TargetModeLabel, *ProjectionModeLabel, *TextureSetStatus)),
		GEngine->GetSmallFont(),
		FLinearColor::White);
	ModeText.EnableShadow(FLinearColor::Black);
	Canvas->DrawItem(ModeText);

	if (bAdjustingBrushRadius)
	{
		FCanvasTextItem RadiusText(FVector2D(10.0f, 309.0f), FText::FromString(FString::Printf(TEXT("Brush Radius: %.1f"), BrushProperties ? BrushProperties->BrushRadius : 0.0f)), GEngine->GetSmallFont(), FLinearColor::Yellow);
		RadiusText.EnableShadow(FLinearColor::Black);
		Canvas->DrawItem(RadiusText);
	}
}
#undef LOCTEXT_NAMESPACE
