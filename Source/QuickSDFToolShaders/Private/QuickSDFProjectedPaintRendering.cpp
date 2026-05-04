#include "QuickSDFProjectedPaintRendering.h"

#include "GlobalRenderResources.h"
#include "GlobalShader.h"
#include "PipelineStateCache.h"
#include "RHIStaticStates.h"
#include "Shader.h"
#include "ShaderParameterUtils.h"
#include "TextureResource.h"

namespace QuickSDFProjectedPaintRendering
{
	class TQuickSDFProjectedPaintCoverageVertexShader : public FGlobalShader
	{
		DECLARE_SHADER_TYPE(TQuickSDFProjectedPaintCoverageVertexShader, Global);

	public:
		static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
		{
			return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
		}

		TQuickSDFProjectedPaintCoverageVertexShader() = default;

		TQuickSDFProjectedPaintCoverageVertexShader(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
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

	IMPLEMENT_SHADER_TYPE(, TQuickSDFProjectedPaintCoverageVertexShader, TEXT("/Plugin/QuickSDFTool/Private/QuickSDFProjectedPaint.usf"), TEXT("MainCoverageVS"), SF_Vertex);

	class TQuickSDFProjectedPaintCoveragePixelShader : public FGlobalShader
	{
		DECLARE_SHADER_TYPE(TQuickSDFProjectedPaintCoveragePixelShader, Global);

	public:
		static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
		{
			return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
		}

		TQuickSDFProjectedPaintCoveragePixelShader() = default;

		TQuickSDFProjectedPaintCoveragePixelShader(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
			: FGlobalShader(Initializer)
		{
			ProjectionOriginParameter.Bind(Initializer.ParameterMap, TEXT("c_ProjectionOrigin"));
			ProjectionAxisXParameter.Bind(Initializer.ParameterMap, TEXT("c_ProjectionAxisX"));
			ProjectionAxisYParameter.Bind(Initializer.ParameterMap, TEXT("c_ProjectionAxisY"));
			ProjectionNormalParameter.Bind(Initializer.ParameterMap, TEXT("c_ProjectionNormal"));
			BrushMetricsParameter.Bind(Initializer.ParameterMap, TEXT("c_BrushMetrics"));
			StrokeMetricsParameter.Bind(Initializer.ParameterMap, TEXT("c_StrokeMetrics"));
			StrokePointsParameter.Bind(Initializer.ParameterMap, TEXT("c_StrokePoints"));
		}

		void SetParameters(FRHIBatchedShaderParameters& BatchedParameters, const FProjectedPaintShaderParameters& InShaderParams)
		{
			SetShaderValue(BatchedParameters, ProjectionOriginParameter, InShaderParams.ProjectionOrigin);
			SetShaderValue(BatchedParameters, ProjectionAxisXParameter, InShaderParams.ProjectionAxisX);
			SetShaderValue(BatchedParameters, ProjectionAxisYParameter, InShaderParams.ProjectionAxisY);
			SetShaderValue(BatchedParameters, ProjectionNormalParameter, InShaderParams.ProjectionNormal);

			SetShaderValue(BatchedParameters, BrushMetricsParameter, FVector4f(
				InShaderParams.BrushRadius,
				InShaderParams.BrushRadialFalloffRange,
				InShaderParams.BrushDepth,
				InShaderParams.BrushDepthFalloffRange));

			SetShaderValue(BatchedParameters, StrokeMetricsParameter, FVector4f(
				static_cast<float>(InShaderParams.StrokePoints.Num()),
				InShaderParams.BrushAntialiasWidth,
				0.0f,
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
		LAYOUT_FIELD(FShaderParameter, ProjectionOriginParameter);
		LAYOUT_FIELD(FShaderParameter, ProjectionAxisXParameter);
		LAYOUT_FIELD(FShaderParameter, ProjectionAxisYParameter);
		LAYOUT_FIELD(FShaderParameter, ProjectionNormalParameter);
		LAYOUT_FIELD(FShaderParameter, BrushMetricsParameter);
		LAYOUT_FIELD(FShaderParameter, StrokeMetricsParameter);
		LAYOUT_FIELD(FShaderParameter, StrokePointsParameter);
	};

	IMPLEMENT_SHADER_TYPE(, TQuickSDFProjectedPaintCoveragePixelShader, TEXT("/Plugin/QuickSDFTool/Private/QuickSDFProjectedPaint.usf"), TEXT("MainCoveragePS"), SF_Pixel);

	class TQuickSDFProjectedPaintResolveVertexShader : public FGlobalShader
	{
		DECLARE_SHADER_TYPE(TQuickSDFProjectedPaintResolveVertexShader, Global);

	public:
		static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
		{
			return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
		}

		TQuickSDFProjectedPaintResolveVertexShader() = default;

		TQuickSDFProjectedPaintResolveVertexShader(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
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

	IMPLEMENT_SHADER_TYPE(, TQuickSDFProjectedPaintResolveVertexShader, TEXT("/Plugin/QuickSDFTool/Private/QuickSDFProjectedPaint.usf"), TEXT("MainResolveVS"), SF_Vertex);

	class TQuickSDFProjectedPaintResolvePixelShader : public FGlobalShader
	{
		DECLARE_SHADER_TYPE(TQuickSDFProjectedPaintResolvePixelShader, Global);

	public:
		static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
		{
			return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
		}

		TQuickSDFProjectedPaintResolvePixelShader() = default;

		TQuickSDFProjectedPaintResolvePixelShader(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
			: FGlobalShader(Initializer)
		{
			CoverageTextureParameter.Bind(Initializer.ParameterMap, TEXT("s_CoverageTexture"));
			CoverageTextureSamplerParameter.Bind(Initializer.ParameterMap, TEXT("s_CoverageTextureSampler"));
			BrushColorParameter.Bind(Initializer.ParameterMap, TEXT("c_BrushColor"));
			ResolveMetricsParameter.Bind(Initializer.ParameterMap, TEXT("c_ResolveMetrics"));
			GammaParameter.Bind(Initializer.ParameterMap, TEXT("c_Gamma"));
		}

		void SetParameters(
			FRHIBatchedShaderParameters& BatchedParameters,
			float InGamma,
			const FTexture* CoverageTexture,
			const FProjectedResolveShaderParameters& InShaderParams)
		{
			FRHITexture* CoverageTextureRHI = CoverageTexture ? CoverageTexture->TextureRHI.GetReference() : GWhiteTexture->TextureRHI.GetReference();
			SetTextureParameter(
				BatchedParameters,
				CoverageTextureParameter,
				CoverageTextureSamplerParameter,
				TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI(),
				CoverageTextureRHI);

			SetShaderValue(BatchedParameters, BrushColorParameter, FVector4f(
				InShaderParams.BrushColor.R,
				InShaderParams.BrushColor.G,
				InShaderParams.BrushColor.B,
				InShaderParams.BrushColor.A * InShaderParams.BrushStrength));
			SetShaderValue(BatchedParameters, ResolveMetricsParameter, InShaderParams.ResolveMetrics);
			SetShaderValue(BatchedParameters, GammaParameter, InGamma);
		}

	private:
		LAYOUT_FIELD(FShaderResourceParameter, CoverageTextureParameter);
		LAYOUT_FIELD(FShaderResourceParameter, CoverageTextureSamplerParameter);
		LAYOUT_FIELD(FShaderParameter, BrushColorParameter);
		LAYOUT_FIELD(FShaderParameter, ResolveMetricsParameter);
		LAYOUT_FIELD(FShaderParameter, GammaParameter);
	};

	IMPLEMENT_SHADER_TYPE(, TQuickSDFProjectedPaintResolvePixelShader, TEXT("/Plugin/QuickSDFTool/Private/QuickSDFProjectedPaint.usf"), TEXT("MainResolvePS"), SF_Pixel);

	using FQuickSDFProjectedPaintVertexDeclaration = FSimpleElementVertexDeclaration;
	TGlobalResource<FQuickSDFProjectedPaintVertexDeclaration> GQuickSDFProjectedPaintVertexDeclaration;

	void SetProjectedPaintCoverageShaders(
		FRHICommandList& RHICmdList,
		FGraphicsPipelineStateInitializer& GraphicsPSOInit,
		ERHIFeatureLevel::Type InFeatureLevel,
		const FMatrix& InTransform,
		const FProjectedPaintShaderParameters& InShaderParams)
	{
		TShaderMapRef<TQuickSDFProjectedPaintCoverageVertexShader> VertexShader(GetGlobalShaderMap(InFeatureLevel));
		TShaderMapRef<TQuickSDFProjectedPaintCoveragePixelShader> PixelShader(GetGlobalShaderMap(InFeatureLevel));

		GraphicsPSOInit.BoundShaderState.VertexDeclarationRHI = GQuickSDFProjectedPaintVertexDeclaration.VertexDeclarationRHI;
		GraphicsPSOInit.BoundShaderState.VertexShaderRHI = VertexShader.GetVertexShader();
		GraphicsPSOInit.BoundShaderState.PixelShaderRHI = PixelShader.GetPixelShader();
		GraphicsPSOInit.BlendState = TStaticBlendState<CW_RGBA, BO_Max, BF_One, BF_One, BO_Max, BF_One, BF_One>::GetRHI();
		GraphicsPSOInit.PrimitiveType = PT_TriangleList;

		RHICmdList.ApplyCachedRenderTargets(GraphicsPSOInit);
		SetGraphicsPipelineState(RHICmdList, GraphicsPSOInit, 0);
		SetShaderParametersLegacyVS(RHICmdList, VertexShader, FMatrix44f(InTransform));
		SetShaderParametersLegacyPS(RHICmdList, PixelShader, InShaderParams);
	}

	void SetProjectedPaintResolveShaders(
		FRHICommandList& RHICmdList,
		FGraphicsPipelineStateInitializer& GraphicsPSOInit,
		ERHIFeatureLevel::Type InFeatureLevel,
		const FMatrix& InTransform,
		float InGamma,
		const FTexture* CoverageTexture,
		const FProjectedResolveShaderParameters& InShaderParams)
	{
		TShaderMapRef<TQuickSDFProjectedPaintResolveVertexShader> VertexShader(GetGlobalShaderMap(InFeatureLevel));
		TShaderMapRef<TQuickSDFProjectedPaintResolvePixelShader> PixelShader(GetGlobalShaderMap(InFeatureLevel));

		GraphicsPSOInit.BoundShaderState.VertexDeclarationRHI = GQuickSDFProjectedPaintVertexDeclaration.VertexDeclarationRHI;
		GraphicsPSOInit.BoundShaderState.VertexShaderRHI = VertexShader.GetVertexShader();
		GraphicsPSOInit.BoundShaderState.PixelShaderRHI = PixelShader.GetPixelShader();
		GraphicsPSOInit.BlendState = TStaticBlendState<CW_RGBA, BO_Add, BF_SourceAlpha, BF_InverseSourceAlpha, BO_Add, BF_Zero, BF_One>::GetRHI();
		GraphicsPSOInit.PrimitiveType = PT_TriangleList;

		RHICmdList.ApplyCachedRenderTargets(GraphicsPSOInit);
		SetGraphicsPipelineState(RHICmdList, GraphicsPSOInit, 0);
		SetShaderParametersLegacyVS(RHICmdList, VertexShader, FMatrix44f(InTransform));
		SetShaderParametersLegacyPS(RHICmdList, PixelShader, InGamma, CoverageTexture, InShaderParams);
	}
}
