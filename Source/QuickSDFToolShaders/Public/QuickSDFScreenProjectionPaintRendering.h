#pragma once

#include "CoreMinimal.h"
#include "BatchedElements.h"

class FGraphicsPipelineStateInitializer;
class FRHICommandList;

namespace QuickSDFScreenProjectionPaintRendering
{
	inline constexpr int32 MaxScreenProjectionPaintStrokePoints = 256;

	struct FScreenProjectionPaintShaderParameters
	{
		FVector4f ViewOrigin = FVector4f(0.0f, 0.0f, 0.0f, 0.0f);
		FVector4f ViewRight = FVector4f(1.0f, 0.0f, 0.0f, 0.0f);
		FVector4f ViewUp = FVector4f(0.0f, 0.0f, 1.0f, 0.0f);
		FVector4f ViewForward = FVector4f(0.0f, 1.0f, 0.0f, 0.0f);
		FVector4f ScreenOffsetAndMode = FVector4f(0.0f, 0.0f, 0.0f, 0.0f);
		FVector4f ProjectionScaleAndViewport = FVector4f(1.0f, 1.0f, 1.0f, 1.0f);
		FVector4f BrushMetrics = FVector4f(32.0f, 0.0f, 1.0f, 0.0f);
		FVector4f StrokeMetrics = FVector4f(0.0f, 1.0f, 0.02f, 0.0f);
		TArray<FVector4f, TInlineAllocator<MaxScreenProjectionPaintStrokePoints>> StrokePoints;
	};

	void QUICKSDFTOOLSHADERS_API SetScreenProjectionPaintCoverageShaders(
		FRHICommandList& RHICmdList,
		FGraphicsPipelineStateInitializer& GraphicsPSOInit,
		ERHIFeatureLevel::Type InFeatureLevel,
		const FMatrix& InTransform,
		const FScreenProjectionPaintShaderParameters& InShaderParams);
}

class QUICKSDFTOOLSHADERS_API FQuickSDFScreenProjectionPaintCoverageBatchedElementParameters : public FBatchedElementParameters
{
public:
	virtual void BindShaders(
		FRHICommandList& RHICmdList,
		FGraphicsPipelineStateInitializer& GraphicsPSOInit,
		ERHIFeatureLevel::Type InFeatureLevel,
		const FMatrix& InTransform,
		const float InGamma,
		const FMatrix& ColorWeights,
		const FTexture* Texture) override
	{
		QuickSDFScreenProjectionPaintRendering::SetScreenProjectionPaintCoverageShaders(
			RHICmdList,
			GraphicsPSOInit,
			InFeatureLevel,
			InTransform,
			ShaderParams);
	}

	QuickSDFScreenProjectionPaintRendering::FScreenProjectionPaintShaderParameters ShaderParams;
};
