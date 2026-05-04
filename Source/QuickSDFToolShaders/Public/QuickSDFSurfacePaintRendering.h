#pragma once

#include "CoreMinimal.h"
#include "BatchedElements.h"

class FGraphicsPipelineStateInitializer;
class FRHICommandList;

namespace QuickSDFSurfacePaintRendering
{
	inline constexpr int32 MaxSurfacePaintStrokePoints = 128;

	struct FSurfacePaintShaderParameters
	{
		FMatrix WorldToBrushMatrix = FMatrix::Identity;
		float BrushRadius = 1.0f;
		float BrushRadialFalloffRange = 0.0f;
		float BrushDepth = 1.0f;
		float BrushDepthFalloffRange = 0.0f;
		float BrushStrength = 1.0f;
		float BrushAntialiasWidth = 0.0f;
		float BrushLineLength = 0.0f;
		float BrushIsLine = 0.0f;
		float BrushIsPolyline = 0.0f;
		float BrushIsPointStroke = 0.0f;
		FLinearColor BrushColor = FLinearColor::White;
		TArray<FVector4f, TInlineAllocator<MaxSurfacePaintStrokePoints>> StrokePoints;
		TArray<FVector4f, TInlineAllocator<MaxSurfacePaintStrokePoints>> StrokeNormals;
	};

	void QUICKSDFTOOLSHADERS_API SetSurfacePaintShaders(
		FRHICommandList& RHICmdList,
		FGraphicsPipelineStateInitializer& GraphicsPSOInit,
		ERHIFeatureLevel::Type InFeatureLevel,
		const FMatrix& InTransform,
		const float InGamma,
		const FSurfacePaintShaderParameters& InShaderParams);
}

class QUICKSDFTOOLSHADERS_API FQuickSDFSurfacePaintBatchedElementParameters : public FBatchedElementParameters
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
		QuickSDFSurfacePaintRendering::SetSurfacePaintShaders(
			RHICmdList,
			GraphicsPSOInit,
			InFeatureLevel,
			InTransform,
			InGamma,
			ShaderParams);
	}

	QuickSDFSurfacePaintRendering::FSurfacePaintShaderParameters ShaderParams;
};
