#pragma once

#include "CoreMinimal.h"
#include "BatchedElements.h"

class FGraphicsPipelineStateInitializer;
class FRHICommandList;

namespace QuickSDFProjectedPaintRendering
{
	inline constexpr int32 MaxProjectedPaintStrokePoints = 256;

	struct FProjectedPaintShaderParameters
	{
		FVector4f ProjectionOrigin = FVector4f(0.0f, 0.0f, 0.0f, 0.0f);
		FVector4f ProjectionAxisX = FVector4f(1.0f, 0.0f, 0.0f, 0.0f);
		FVector4f ProjectionAxisY = FVector4f(0.0f, 1.0f, 0.0f, 0.0f);
		FVector4f ProjectionNormal = FVector4f(0.0f, 0.0f, 1.0f, 0.0f);
		float BrushRadius = 1.0f;
		float BrushRadialFalloffRange = 0.0f;
		float BrushDepth = 1.0f;
		float BrushDepthFalloffRange = 0.0f;
		float BrushAntialiasWidth = 0.0f;
		TArray<FVector4f, TInlineAllocator<MaxProjectedPaintStrokePoints>> StrokePoints;
	};

	struct FProjectedResolveShaderParameters
	{
		FLinearColor BrushColor = FLinearColor::White;
		float BrushStrength = 1.0f;
		FVector4f ResolveMetrics = FVector4f(1.0f, 1.0f, 1.0f, 0.0f);
	};

	void QUICKSDFTOOLSHADERS_API SetProjectedPaintCoverageShaders(
		FRHICommandList& RHICmdList,
		FGraphicsPipelineStateInitializer& GraphicsPSOInit,
		ERHIFeatureLevel::Type InFeatureLevel,
		const FMatrix& InTransform,
		const FProjectedPaintShaderParameters& InShaderParams);

	void QUICKSDFTOOLSHADERS_API SetProjectedPaintResolveShaders(
		FRHICommandList& RHICmdList,
		FGraphicsPipelineStateInitializer& GraphicsPSOInit,
		ERHIFeatureLevel::Type InFeatureLevel,
		const FMatrix& InTransform,
		float InGamma,
		const FTexture* CoverageTexture,
		const FProjectedResolveShaderParameters& InShaderParams);
}

class QUICKSDFTOOLSHADERS_API FQuickSDFProjectedPaintCoverageBatchedElementParameters : public FBatchedElementParameters
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
		QuickSDFProjectedPaintRendering::SetProjectedPaintCoverageShaders(
			RHICmdList,
			GraphicsPSOInit,
			InFeatureLevel,
			InTransform,
			ShaderParams);
	}

	QuickSDFProjectedPaintRendering::FProjectedPaintShaderParameters ShaderParams;
};

class QUICKSDFTOOLSHADERS_API FQuickSDFProjectedPaintResolveBatchedElementParameters : public FBatchedElementParameters
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
		QuickSDFProjectedPaintRendering::SetProjectedPaintResolveShaders(
			RHICmdList,
			GraphicsPSOInit,
			InFeatureLevel,
			InTransform,
			InGamma,
			Texture,
			ShaderParams);
	}

	QuickSDFProjectedPaintRendering::FProjectedResolveShaderParameters ShaderParams;
};
