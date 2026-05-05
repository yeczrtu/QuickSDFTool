#include "QuickSDFScreenProjectionPaintRendering.h"

#include "GlobalRenderResources.h"
#include "GlobalShader.h"
#include "PipelineStateCache.h"
#include "RHIStaticStates.h"
#include "Shader.h"
#include "ShaderParameterUtils.h"

namespace QuickSDFScreenProjectionPaintRendering
{
	class TQuickSDFScreenProjectionPaintCoverageVertexShader : public FGlobalShader
	{
		DECLARE_SHADER_TYPE(TQuickSDFScreenProjectionPaintCoverageVertexShader, Global);

	public:
		static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
		{
			return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
		}

		TQuickSDFScreenProjectionPaintCoverageVertexShader() = default;

		TQuickSDFScreenProjectionPaintCoverageVertexShader(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
			: FGlobalShader(Initializer)
		{
			TransformParameter.Bind(Initializer.ParameterMap, TEXT("c_Transform"));
		}

		void SetParameters(FRHIBatchedShaderParameters& BatchedParameters, const FMatrix44f& InTransform)
		{
			SetShaderValue(BatchedParameters, TransformParameter, InTransform);
		}

	private:
		LAYOUT_FIELD(FShaderParameter, TransformParameter);
	};

	IMPLEMENT_SHADER_TYPE(, TQuickSDFScreenProjectionPaintCoverageVertexShader, TEXT("/Plugin/QuickSDFTool/Private/QuickSDFScreenProjectionPaint.usf"), TEXT("MainCoverageVS"), SF_Vertex);

	class TQuickSDFScreenProjectionPaintCoveragePixelShader : public FGlobalShader
	{
		DECLARE_SHADER_TYPE(TQuickSDFScreenProjectionPaintCoveragePixelShader, Global);

	public:
		static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
		{
			return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
		}

		TQuickSDFScreenProjectionPaintCoveragePixelShader() = default;

		TQuickSDFScreenProjectionPaintCoveragePixelShader(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
			: FGlobalShader(Initializer)
		{
			ViewOriginParameter.Bind(Initializer.ParameterMap, TEXT("c_ViewOrigin"));
			ViewRightParameter.Bind(Initializer.ParameterMap, TEXT("c_ViewRight"));
			ViewUpParameter.Bind(Initializer.ParameterMap, TEXT("c_ViewUp"));
			ViewForwardParameter.Bind(Initializer.ParameterMap, TEXT("c_ViewForward"));
			ScreenOffsetAndModeParameter.Bind(Initializer.ParameterMap, TEXT("c_ScreenOffsetAndMode"));
			ProjectionScaleAndViewportParameter.Bind(Initializer.ParameterMap, TEXT("c_ProjectionScaleAndViewport"));
			BrushMetricsParameter.Bind(Initializer.ParameterMap, TEXT("c_BrushMetrics"));
			StrokeMetricsParameter.Bind(Initializer.ParameterMap, TEXT("c_StrokeMetrics"));
			StrokePointsParameter.Bind(Initializer.ParameterMap, TEXT("c_StrokePoints"));
		}

		void SetParameters(FRHIBatchedShaderParameters& BatchedParameters, const FScreenProjectionPaintShaderParameters& InShaderParams)
		{
			SetShaderValue(BatchedParameters, ViewOriginParameter, InShaderParams.ViewOrigin);
			SetShaderValue(BatchedParameters, ViewRightParameter, InShaderParams.ViewRight);
			SetShaderValue(BatchedParameters, ViewUpParameter, InShaderParams.ViewUp);
			SetShaderValue(BatchedParameters, ViewForwardParameter, InShaderParams.ViewForward);
			SetShaderValue(BatchedParameters, ScreenOffsetAndModeParameter, InShaderParams.ScreenOffsetAndMode);
			SetShaderValue(BatchedParameters, ProjectionScaleAndViewportParameter, InShaderParams.ProjectionScaleAndViewport);
			SetShaderValue(BatchedParameters, BrushMetricsParameter, InShaderParams.BrushMetrics);
			SetShaderValue(BatchedParameters, StrokeMetricsParameter, FVector4f(
				static_cast<float>(InShaderParams.StrokePoints.Num()),
				InShaderParams.StrokeMetrics.Y,
				InShaderParams.StrokeMetrics.Z,
				0.0f));

			if (InShaderParams.StrokePoints.Num() > 0)
			{
				SetShaderValueArray(
					BatchedParameters,
					StrokePointsParameter,
					InShaderParams.StrokePoints.GetData(),
					static_cast<uint32>(InShaderParams.StrokePoints.Num()));
			}
		}

	private:
		LAYOUT_FIELD(FShaderParameter, ViewOriginParameter);
		LAYOUT_FIELD(FShaderParameter, ViewRightParameter);
		LAYOUT_FIELD(FShaderParameter, ViewUpParameter);
		LAYOUT_FIELD(FShaderParameter, ViewForwardParameter);
		LAYOUT_FIELD(FShaderParameter, ScreenOffsetAndModeParameter);
		LAYOUT_FIELD(FShaderParameter, ProjectionScaleAndViewportParameter);
		LAYOUT_FIELD(FShaderParameter, BrushMetricsParameter);
		LAYOUT_FIELD(FShaderParameter, StrokeMetricsParameter);
		LAYOUT_FIELD(FShaderParameter, StrokePointsParameter);
	};

	IMPLEMENT_SHADER_TYPE(, TQuickSDFScreenProjectionPaintCoveragePixelShader, TEXT("/Plugin/QuickSDFTool/Private/QuickSDFScreenProjectionPaint.usf"), TEXT("MainCoveragePS"), SF_Pixel);

	using FQuickSDFScreenProjectionPaintVertexDeclaration = FSimpleElementVertexDeclaration;
	TGlobalResource<FQuickSDFScreenProjectionPaintVertexDeclaration> GQuickSDFScreenProjectionPaintVertexDeclaration;

	void SetScreenProjectionPaintCoverageShaders(
		FRHICommandList& RHICmdList,
		FGraphicsPipelineStateInitializer& GraphicsPSOInit,
		ERHIFeatureLevel::Type InFeatureLevel,
		const FMatrix& InTransform,
		const FScreenProjectionPaintShaderParameters& InShaderParams)
	{
		TShaderMapRef<TQuickSDFScreenProjectionPaintCoverageVertexShader> VertexShader(GetGlobalShaderMap(InFeatureLevel));
		TShaderMapRef<TQuickSDFScreenProjectionPaintCoveragePixelShader> PixelShader(GetGlobalShaderMap(InFeatureLevel));

		GraphicsPSOInit.BoundShaderState.VertexDeclarationRHI = GQuickSDFScreenProjectionPaintVertexDeclaration.VertexDeclarationRHI;
		GraphicsPSOInit.BoundShaderState.VertexShaderRHI = VertexShader.GetVertexShader();
		GraphicsPSOInit.BoundShaderState.PixelShaderRHI = PixelShader.GetPixelShader();
		GraphicsPSOInit.BlendState = TStaticBlendState<CW_RGBA, BO_Max, BF_One, BF_One, BO_Max, BF_One, BF_One>::GetRHI();
		GraphicsPSOInit.PrimitiveType = PT_TriangleList;

		RHICmdList.ApplyCachedRenderTargets(GraphicsPSOInit);
		SetGraphicsPipelineState(RHICmdList, GraphicsPSOInit, 0);
		SetShaderParametersLegacyVS(RHICmdList, VertexShader, FMatrix44f(InTransform));
		SetShaderParametersLegacyPS(RHICmdList, PixelShader, InShaderParams);
	}
}
