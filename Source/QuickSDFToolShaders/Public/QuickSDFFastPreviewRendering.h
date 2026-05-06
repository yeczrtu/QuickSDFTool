#pragma once

#include "CoreMinimal.h"
#include "RHIFeatureLevel.h"
#include "RHICommandList.h"
#include "RHIResources.h"

namespace QuickSDFFastPreviewRendering
{
	inline constexpr int32 MaxPreviewMasks = 8;

	struct FFastPreviewMask
	{
		FTextureRHIRef Texture;
		FIntPoint Size = FIntPoint::ZeroValue;
		float TargetT = 0.0f;
	};

	struct FFastPreviewRenderRequest
	{
		TArray<FFastPreviewMask, TInlineAllocator<MaxPreviewMasks>> Masks;
		FTextureRHIRef OutputRenderTargetTexture;
		FTextureRHIRef OutputTexture;
		FIntPoint OutputSize = FIntPoint::ZeroValue;
		float CurrentTargetT = 0.0f;
		ERHIFeatureLevel::Type FeatureLevel = ERHIFeatureLevel::SM5;
	};

	void QUICKSDFTOOLSHADERS_API RenderFastPreview_RenderThread(
		FRHICommandListImmediate& RHICmdList,
		const FFastPreviewRenderRequest& Request);
}
