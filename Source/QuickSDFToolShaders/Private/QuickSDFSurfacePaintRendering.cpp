#include "QuickSDFSurfacePaintRendering.h"

#include "GlobalShader.h"
#include "PipelineStateCache.h"
#include "RHIStaticStates.h"
#include "Shader.h"
#include "ShaderParameterUtils.h"

namespace QuickSDFSurfacePaintRendering
{
	class TQuickSDFSurfacePaintVertexShader : public FGlobalShader
	{
		DECLARE_SHADER_TYPE(TQuickSDFSurfacePaintVertexShader, Global);

	public:
		static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
		{
			return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
		}

		TQuickSDFSurfacePaintVertexShader() = default;

		TQuickSDFSurfacePaintVertexShader(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
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

	IMPLEMENT_SHADER_TYPE(, TQuickSDFSurfacePaintVertexShader, TEXT("/Plugin/QuickSDFTool/Private/QuickSDFSurfacePaint.usf"), TEXT("MainVS"), SF_Vertex);

	class TQuickSDFSurfacePaintPixelShader : public FGlobalShader
	{
		DECLARE_SHADER_TYPE(TQuickSDFSurfacePaintPixelShader, Global);

	public:
		static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
		{
			return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
		}

		TQuickSDFSurfacePaintPixelShader() = default;

		TQuickSDFSurfacePaintPixelShader(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
			: FGlobalShader(Initializer)
		{
			WorldToBrushMatrixParameter.Bind(Initializer.ParameterMap, TEXT("c_WorldToBrushMatrix"));
			BrushMetricsParameter.Bind(Initializer.ParameterMap, TEXT("c_BrushMetrics"));
			BrushStrengthParameter.Bind(Initializer.ParameterMap, TEXT("c_BrushStrength"));
			BrushColorParameter.Bind(Initializer.ParameterMap, TEXT("c_BrushColor"));
			AntialiasWidthParameter.Bind(Initializer.ParameterMap, TEXT("c_AntialiasWidth"));
			BrushLineMetricsParameter.Bind(Initializer.ParameterMap, TEXT("c_BrushLineMetrics"));
			StrokeMetricsParameter.Bind(Initializer.ParameterMap, TEXT("c_StrokeMetrics"));
			StrokePointsParameter.Bind(Initializer.ParameterMap, TEXT("c_StrokePoints"));
			StrokeNormalsParameter.Bind(Initializer.ParameterMap, TEXT("c_StrokeNormals"));
			GammaParameter.Bind(Initializer.ParameterMap, TEXT("c_Gamma"));
		}

		void SetParameters(FRHIBatchedShaderParameters& BatchedParameters, const float InGamma, const FSurfacePaintShaderParameters& InShaderParams)
		{
			SetShaderValue(BatchedParameters, WorldToBrushMatrixParameter, FMatrix44f(InShaderParams.WorldToBrushMatrix));

			const FVector4f BrushMetrics(
				InShaderParams.BrushRadius,
				InShaderParams.BrushRadialFalloffRange,
				InShaderParams.BrushDepth,
				InShaderParams.BrushDepthFalloffRange);
			SetShaderValue(BatchedParameters, BrushMetricsParameter, BrushMetrics);

			SetShaderValue(BatchedParameters, BrushStrengthParameter, InShaderParams.BrushStrength);
			SetShaderValue(BatchedParameters, BrushColorParameter, InShaderParams.BrushColor);
			SetShaderValue(BatchedParameters, AntialiasWidthParameter, InShaderParams.BrushAntialiasWidth);
			SetShaderValue(BatchedParameters, BrushLineMetricsParameter, FVector4f(
				InShaderParams.BrushLineLength,
				InShaderParams.BrushIsLine,
				0.0f,
				0.0f));
			SetShaderValue(BatchedParameters, StrokeMetricsParameter, FVector4f(
				static_cast<float>(InShaderParams.StrokePoints.Num()),
				InShaderParams.BrushIsPolyline,
				InShaderParams.BrushIsPointStroke,
				0.0f));
			if (InShaderParams.StrokePoints.Num() > 0)
			{
				SetShaderValueArray(
					BatchedParameters,
					StrokePointsParameter,
					InShaderParams.StrokePoints.GetData(),
					static_cast<uint32>(InShaderParams.StrokePoints.Num()));
			}
			if (InShaderParams.StrokeNormals.Num() > 0)
			{
				SetShaderValueArray(
					BatchedParameters,
					StrokeNormalsParameter,
					InShaderParams.StrokeNormals.GetData(),
					static_cast<uint32>(InShaderParams.StrokeNormals.Num()));
			}
			SetShaderValue(BatchedParameters, GammaParameter, InGamma);
		}

	private:
		LAYOUT_FIELD(FShaderParameter, WorldToBrushMatrixParameter);
		LAYOUT_FIELD(FShaderParameter, BrushMetricsParameter);
		LAYOUT_FIELD(FShaderParameter, BrushStrengthParameter);
		LAYOUT_FIELD(FShaderParameter, BrushColorParameter);
		LAYOUT_FIELD(FShaderParameter, AntialiasWidthParameter);
		LAYOUT_FIELD(FShaderParameter, BrushLineMetricsParameter);
		LAYOUT_FIELD(FShaderParameter, StrokeMetricsParameter);
		LAYOUT_FIELD(FShaderParameter, StrokePointsParameter);
		LAYOUT_FIELD(FShaderParameter, StrokeNormalsParameter);
		LAYOUT_FIELD(FShaderParameter, GammaParameter);
	};

	IMPLEMENT_SHADER_TYPE(, TQuickSDFSurfacePaintPixelShader, TEXT("/Plugin/QuickSDFTool/Private/QuickSDFSurfacePaint.usf"), TEXT("MainPS"), SF_Pixel);

	using FQuickSDFSurfacePaintVertexDeclaration = FSimpleElementVertexDeclaration;
	TGlobalResource<FQuickSDFSurfacePaintVertexDeclaration> GQuickSDFSurfacePaintVertexDeclaration;

	void SetSurfacePaintShaders(
		FRHICommandList& RHICmdList,
		FGraphicsPipelineStateInitializer& GraphicsPSOInit,
		ERHIFeatureLevel::Type InFeatureLevel,
		const FMatrix& InTransform,
		const float InGamma,
		const FSurfacePaintShaderParameters& InShaderParams)
	{
		TShaderMapRef<TQuickSDFSurfacePaintVertexShader> VertexShader(GetGlobalShaderMap(InFeatureLevel));
		TShaderMapRef<TQuickSDFSurfacePaintPixelShader> PixelShader(GetGlobalShaderMap(InFeatureLevel));

		GraphicsPSOInit.BoundShaderState.VertexDeclarationRHI = GQuickSDFSurfacePaintVertexDeclaration.VertexDeclarationRHI;
		GraphicsPSOInit.BoundShaderState.VertexShaderRHI = VertexShader.GetVertexShader();
		GraphicsPSOInit.BoundShaderState.PixelShaderRHI = PixelShader.GetPixelShader();
		GraphicsPSOInit.BlendState = TStaticBlendState<CW_RGB, BO_Add, BF_SourceAlpha, BF_InverseSourceAlpha, BO_Add, BF_Zero, BF_One>::GetRHI();
		GraphicsPSOInit.PrimitiveType = PT_TriangleList;

		RHICmdList.ApplyCachedRenderTargets(GraphicsPSOInit);
		SetGraphicsPipelineState(RHICmdList, GraphicsPSOInit, 0);

		SetShaderParametersLegacyVS(RHICmdList, VertexShader, FMatrix44f(InTransform));
		SetShaderParametersLegacyPS(RHICmdList, PixelShader, InGamma, InShaderParams);
	}
}
