#include "QuickSDFAsset.h"
#include "Engine/TextureRenderTarget2D.h"

UQuickSDFAsset::UQuickSDFAsset()
{
	Resolution = FIntPoint(1024, 1024);
	UVChannel = 0;
	FinalSDFTexture = nullptr;
}

void UQuickSDFAsset::InitializeRenderTargets()
{
	for (FQuickSDFAngleData& Data : AngleDataList)
	{
		if (!Data.PaintRenderTarget)
		{
			Data.PaintRenderTarget = NewObject<UTextureRenderTarget2D>(this);
			Data.PaintRenderTarget->RenderTargetFormat = RTF_RGBA8;
			Data.PaintRenderTarget->ClearColor = FLinearColor::White;
			Data.PaintRenderTarget->InitAutoFormat(Resolution.X, Resolution.Y);
			Data.PaintRenderTarget->UpdateResourceImmediate(true);
		}
	}
}

void UQuickSDFAsset::BakeToTextures()
{
	// Implementation to read RenderTargets to CPU and compress as UTexture2D
}
