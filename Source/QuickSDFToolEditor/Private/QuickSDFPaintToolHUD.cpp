#include "QuickSDFPaintTool.h"
#include "QuickSDFPaintToolPrivate.h"
#include "QuickSDFMeshComponentAdapter.h"
#include "QuickSDFToolSubsystem.h"
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
            // 1. プレビューテクスチャの描画
            PreviewCanvasOrigin = FVector2D(10.0f, 10.0f);
            PreviewCanvasSize = FVector2D(256.0f, 256.0f);
            const FVector2D PreviewOrigin = GetPreviewOrigin();
            const FVector2D PreviewSize = GetPreviewSize();
            
            if (PreviewMaterial)
            {
                PreviewMaterial->SetTextureParameterValue(TEXT("BaseColor"), RT);
                FCanvasTileItem TileItem(PreviewOrigin, PreviewMaterial->GetRenderProxy(), PreviewSize);
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
                    if (Properties->EditAngleIndex > 0 && ActiveAsset->AngleDataList.IsValidIndex(Properties->EditAngleIndex - 1))
                    {
                        UTextureRenderTarget2D* PrevRT = ActiveAsset->AngleDataList[Properties->EditAngleIndex - 1].PaintRenderTarget;
                        if (PrevRT)
                        {
                            FCanvasTileItem PrevTile(PreviewOrigin, PrevRT->GetResource(), PreviewSize, FLinearColor(1.0f, 0.0f, 0.0f, 0.5f));
                            PrevTile.BlendMode = SE_BLEND_Additive;
                            Canvas->DrawItem(PrevTile);
                        }
                    }
                    
                    // Next frame (Green)
                    if (Properties->EditAngleIndex < ActiveAsset->AngleDataList.Num() - 1 && ActiveAsset->AngleDataList.IsValidIndex(Properties->EditAngleIndex + 1))
                    {
                        UTextureRenderTarget2D* NextRT = ActiveAsset->AngleDataList[Properties->EditAngleIndex + 1].PaintRenderTarget;
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
                    // 線の色と不透明度を設定
                    FLinearColor UVLineColor(0.0f, 1.0f, 0.0f, 0.3f); // 半透明の緑色

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

                            // UV(0-1) を プレビューのピクセル座標に変換するラムダ関数
                            auto UVToScreen = [&](const FVector2f& UV) -> FVector2D {
                                return FVector2D(
                                    PreviewOrigin.X + (double)UV.X * PreviewSize.X,
                                    PreviewOrigin.Y + (double)UV.Y * PreviewSize.Y
                                );
                            };

                            FVector2D P0 = UVToScreen(UV0);
                            FVector2D P1 = UVToScreen(UV1);
                            FVector2D P2 = UVToScreen(UV2);

                            // 三角形の3辺を描画
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

            // 2. ボーダーの描画
            FCanvasBoxItem BorderItem(PreviewOrigin, PreviewSize);
            BorderItem.SetColor(IsInPreviewBounds(LastInputScreenPosition) ? FLinearColor::Yellow : FLinearColor::Gray);
            Canvas->DrawItem(BorderItem);
        }
    }


	const FString PaintModeLabel = IsPaintingShadow() ? TEXT("Shadow") : TEXT("Light");
	const FString ShortcutLabel = bAdjustingBrushRadius ? TEXT("Ctrl+F active: move mouse, click to confirm") : TEXT("Shift: toggle paint, Ctrl+F: resize brush");
	FCanvasTextItem ModeText(FVector2D(10.0f, 275.0f), FText::FromString(FString::Printf(TEXT("Paint: %s"), *PaintModeLabel)), GEngine->GetSmallFont(), FLinearColor::White);
	ModeText.EnableShadow(FLinearColor::Black);
	Canvas->DrawItem(ModeText);

	FCanvasTextItem ShortcutText(FVector2D(10.0f, 292.0f), FText::FromString(ShortcutLabel), GEngine->GetSmallFont(), FLinearColor::White);
	ShortcutText.EnableShadow(FLinearColor::Black);
	Canvas->DrawItem(ShortcutText);

	if (bAdjustingBrushRadius)
	{
		FCanvasTextItem RadiusText(FVector2D(10.0f, 309.0f), FText::FromString(FString::Printf(TEXT("Brush Radius: %.1f"), BrushProperties ? BrushProperties->BrushRadius : 0.0f)), GEngine->GetSmallFont(), FLinearColor::Yellow);
		RadiusText.EnableShadow(FLinearColor::Black);
		Canvas->DrawItem(RadiusText);
	}
}
#undef LOCTEXT_NAMESPACE
