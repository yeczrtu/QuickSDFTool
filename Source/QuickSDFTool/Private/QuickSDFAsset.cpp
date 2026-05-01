#include "QuickSDFAsset.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/Canvas.h"
#include "CanvasItem.h"
#include "RenderResource.h"
#include "TextureResource.h"

UQuickSDFAsset::UQuickSDFAsset()
{
	Resolution = FIntPoint(1024, 1024);
	UVChannel = 0;
	FinalSDFTexture = nullptr;
}

void UQuickSDFAsset::InitializeRenderTargets(UWorld* InWorld)
{
	for (FQuickSDFAngleData& Data : AngleDataList)
	{
		// RenderTargetがまだ無い場合は生成する
		if (!Data.PaintRenderTarget)
		{
			Data.PaintRenderTarget = NewObject<UTextureRenderTarget2D>(this);
			Data.PaintRenderTarget->RenderTargetFormat = RTF_R8;
			Data.PaintRenderTarget->ClearColor = FLinearColor::White;
			Data.PaintRenderTarget->InitAutoFormat(Resolution.X, Resolution.Y);
			Data.PaintRenderTarget->UpdateResourceImmediate(true);

			// もし保存済みのTexture2Dがあれば、RenderTargetにロードして続きを描けるようにする
			if (Data.TextureMask && InWorld)
			{
				FTextureRenderTargetResource* RTResource = Data.PaintRenderTarget->GameThread_GetRenderTargetResource();
				if (RTResource)
				{
					FCanvas Canvas(RTResource, nullptr, InWorld, GMaxRHIFeatureLevel);
					FCanvasTileItem TileItem(FVector2D::ZeroVector, Data.TextureMask->GetResource(), FVector2D(Resolution.X, Resolution.Y), FLinearColor::White);
					TileItem.BlendMode = SE_BLEND_Opaque;
					Canvas.DrawItem(TileItem);
					Canvas.Flush_GameThread(true);
				}
			}
		}
	}
}

void UQuickSDFAsset::BakeToTextures()
{
	// RenderTargetの内容をアセットとして保存する処理 (後ほど実装)
}
