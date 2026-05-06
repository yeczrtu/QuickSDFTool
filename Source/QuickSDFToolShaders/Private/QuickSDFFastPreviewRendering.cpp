#include "QuickSDFFastPreviewRendering.h"

#include "GlobalShader.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"
#include "RenderingThread.h"
#include "ShaderParameterStruct.h"

namespace QuickSDFFastPreviewRendering
{
namespace
{
constexpr int32 ThreadGroupSize = 8;

class FQuickSDFDownsampleMaskCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FQuickSDFDownsampleMaskCS);
	SHADER_USE_PARAMETER_STRUCT(FQuickSDFDownsampleMaskCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, SourceTexture)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D, OutputMask)
		SHADER_PARAMETER(FIntPoint, SourceSize)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};

class FQuickSDFFastSeedCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FQuickSDFFastSeedCS);
	SHADER_USE_PARAMETER_STRUCT(FQuickSDFFastSeedCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, InputMask)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D, OutputSeed)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};

class FQuickSDFFastJumpFloodCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FQuickSDFFastJumpFloodCS);
	SHADER_USE_PARAMETER_STRUCT(FQuickSDFFastJumpFloodCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, InputSeed)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D, OutputJumpSeed)
		SHADER_PARAMETER(int32, JumpStepSize)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};

class FQuickSDFFastResolvePreviewCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FQuickSDFFastResolvePreviewCS);
	SHADER_USE_PARAMETER_STRUCT(FQuickSDFFastResolvePreviewCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, SeedTexture0)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, SeedTexture1)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, SeedTexture2)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, SeedTexture3)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, SeedTexture4)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, SeedTexture5)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, SeedTexture6)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, SeedTexture7)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D, OutputPreview)
		SHADER_PARAMETER(FVector4f, TargetT0)
		SHADER_PARAMETER(FVector4f, TargetT1)
		SHADER_PARAMETER(float, CurrentTargetT)
		SHADER_PARAMETER(int32, MaskCount)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};

IMPLEMENT_GLOBAL_SHADER(FQuickSDFDownsampleMaskCS, "/Plugin/QuickSDFTool/Private/QuickSDFFastPreview.usf", "DownsampleMaskMain", SF_Compute);
IMPLEMENT_GLOBAL_SHADER(FQuickSDFFastSeedCS, "/Plugin/QuickSDFTool/Private/QuickSDFFastPreview.usf", "FastSeedMain", SF_Compute);
IMPLEMENT_GLOBAL_SHADER(FQuickSDFFastJumpFloodCS, "/Plugin/QuickSDFTool/Private/QuickSDFFastPreview.usf", "FastJumpFloodMain", SF_Compute);
IMPLEMENT_GLOBAL_SHADER(FQuickSDFFastResolvePreviewCS, "/Plugin/QuickSDFTool/Private/QuickSDFFastPreview.usf", "FastResolvePreviewMain", SF_Compute);

int32 GetInitialJumpStep(const FIntPoint& Size)
{
	const int32 MaxDim = FMath::Max(Size.X, Size.Y);
	if (MaxDim <= 4)
	{
		return 1;
	}

	return FMath::RoundUpToPowerOfTwo(MaxDim) / 2;
}

void SetTargetTComponent(FVector4f& TargetT0, FVector4f& TargetT1, int32 Index, float Value)
{
	FVector4f& TargetT = Index < 4 ? TargetT0 : TargetT1;
	switch (Index)
	{
	case 0:
	case 4:
		TargetT.X = Value;
		break;
	case 1:
	case 5:
		TargetT.Y = Value;
		break;
	case 2:
	case 6:
		TargetT.Z = Value;
		break;
	case 3:
	case 7:
		TargetT.W = Value;
		break;
	default:
		break;
	}
}

void AddDownsamplePass(
	FRDGBuilder& GraphBuilder,
	ERHIFeatureLevel::Type FeatureLevel,
	FRDGTextureRef SourceTexture,
	const FIntPoint& SourceSize,
	FRDGTextureRef OutputMask,
	const FIntPoint& OutputSize)
{
	TShaderMapRef<FQuickSDFDownsampleMaskCS> ComputeShader(GetGlobalShaderMap(FeatureLevel));
	FQuickSDFDownsampleMaskCS::FParameters* Parameters = GraphBuilder.AllocParameters<FQuickSDFDownsampleMaskCS::FParameters>();
	Parameters->SourceTexture = SourceTexture;
	Parameters->OutputMask = GraphBuilder.CreateUAV(FRDGTextureUAVDesc(OutputMask));
	Parameters->SourceSize = SourceSize;

	FComputeShaderUtils::AddPass(
		GraphBuilder,
		RDG_EVENT_NAME("QuickSDF Live Downsample Mask"),
		ComputeShader,
		Parameters,
		FComputeShaderUtils::GetGroupCount(OutputSize, ThreadGroupSize));
}

void AddSeedPass(
	FRDGBuilder& GraphBuilder,
	ERHIFeatureLevel::Type FeatureLevel,
	FRDGTextureRef InputMask,
	FRDGTextureRef OutputSeed,
	const FIntPoint& OutputSize)
{
	TShaderMapRef<FQuickSDFFastSeedCS> ComputeShader(GetGlobalShaderMap(FeatureLevel));
	FQuickSDFFastSeedCS::FParameters* Parameters = GraphBuilder.AllocParameters<FQuickSDFFastSeedCS::FParameters>();
	Parameters->InputMask = InputMask;
	Parameters->OutputSeed = GraphBuilder.CreateUAV(FRDGTextureUAVDesc(OutputSeed));

	FComputeShaderUtils::AddPass(
		GraphBuilder,
		RDG_EVENT_NAME("QuickSDF Live Seed"),
		ComputeShader,
		Parameters,
		FComputeShaderUtils::GetGroupCount(OutputSize, ThreadGroupSize));
}

FRDGTextureRef AddJumpFloodPasses(
	FRDGBuilder& GraphBuilder,
	ERHIFeatureLevel::Type FeatureLevel,
	FRDGTextureRef InitialSeed,
	const FRDGTextureDesc& SeedDesc,
	const FIntPoint& OutputSize)
{
	FRDGTextureRef CurrentSeed = InitialSeed;
	FRDGTextureRef PingPongSeed = GraphBuilder.CreateTexture(
		SeedDesc,
		TEXT("QuickSDF.Live.JumpSeed"));

	TShaderMapRef<FQuickSDFFastJumpFloodCS> ComputeShader(GetGlobalShaderMap(FeatureLevel));
	for (int32 Step = GetInitialJumpStep(OutputSize); Step >= 1; Step /= 2)
	{
		FQuickSDFFastJumpFloodCS::FParameters* Parameters = GraphBuilder.AllocParameters<FQuickSDFFastJumpFloodCS::FParameters>();
		Parameters->InputSeed = CurrentSeed;
		Parameters->OutputJumpSeed = GraphBuilder.CreateUAV(FRDGTextureUAVDesc(PingPongSeed));
		Parameters->JumpStepSize = Step;

		FComputeShaderUtils::AddPass(
			GraphBuilder,
			RDG_EVENT_NAME("QuickSDF Live JFA Step %d", Step),
			ComputeShader,
			Parameters,
			FComputeShaderUtils::GetGroupCount(OutputSize, ThreadGroupSize));

		Swap(CurrentSeed, PingPongSeed);
	}

	return CurrentSeed;
}

void AddResolvePass(
	FRDGBuilder& GraphBuilder,
	ERHIFeatureLevel::Type FeatureLevel,
	const TArray<FRDGTextureRef, TInlineAllocator<MaxPreviewMasks>>& SeedTextures,
	const FVector4f& TargetT0,
	const FVector4f& TargetT1,
	float CurrentTargetT,
	int32 MaskCount,
	FRDGTextureRef OutputTexture,
	const FIntPoint& OutputSize)
{
	if (SeedTextures.Num() == 0)
	{
		return;
	}

	FRDGTextureRef FallbackSeed = SeedTextures[0];
	TShaderMapRef<FQuickSDFFastResolvePreviewCS> ComputeShader(GetGlobalShaderMap(FeatureLevel));
	FQuickSDFFastResolvePreviewCS::FParameters* Parameters = GraphBuilder.AllocParameters<FQuickSDFFastResolvePreviewCS::FParameters>();
	Parameters->SeedTexture0 = SeedTextures.IsValidIndex(0) ? SeedTextures[0] : FallbackSeed;
	Parameters->SeedTexture1 = SeedTextures.IsValidIndex(1) ? SeedTextures[1] : FallbackSeed;
	Parameters->SeedTexture2 = SeedTextures.IsValidIndex(2) ? SeedTextures[2] : FallbackSeed;
	Parameters->SeedTexture3 = SeedTextures.IsValidIndex(3) ? SeedTextures[3] : FallbackSeed;
	Parameters->SeedTexture4 = SeedTextures.IsValidIndex(4) ? SeedTextures[4] : FallbackSeed;
	Parameters->SeedTexture5 = SeedTextures.IsValidIndex(5) ? SeedTextures[5] : FallbackSeed;
	Parameters->SeedTexture6 = SeedTextures.IsValidIndex(6) ? SeedTextures[6] : FallbackSeed;
	Parameters->SeedTexture7 = SeedTextures.IsValidIndex(7) ? SeedTextures[7] : FallbackSeed;
	Parameters->OutputPreview = GraphBuilder.CreateUAV(FRDGTextureUAVDesc(OutputTexture));
	Parameters->TargetT0 = TargetT0;
	Parameters->TargetT1 = TargetT1;
	Parameters->CurrentTargetT = CurrentTargetT;
	Parameters->MaskCount = MaskCount;

	FComputeShaderUtils::AddPass(
		GraphBuilder,
		RDG_EVENT_NAME("QuickSDF Live Resolve Preview"),
		ComputeShader,
		Parameters,
		FComputeShaderUtils::GetGroupCount(OutputSize, ThreadGroupSize));
}
}

void RenderFastPreview_RenderThread(FRHICommandListImmediate& RHICmdList, const FFastPreviewRenderRequest& Request)
{
	check(IsInRenderingThread());

	if (!Request.OutputRenderTargetTexture.IsValid() ||
		Request.OutputSize.X <= 0 ||
		Request.OutputSize.Y <= 0 ||
		Request.Masks.Num() == 0)
	{
		return;
	}

	FRDGBuilder GraphBuilder(RHICmdList);
	const FIntPoint OutputSize = Request.OutputSize;
	const FRDGTextureDesc MaskDesc = FRDGTextureDesc::Create2D(
		OutputSize,
		PF_FloatRGBA,
		FClearValueBinding::White,
		TexCreate_ShaderResource | TexCreate_UAV);
	const FRDGTextureDesc SeedDesc = FRDGTextureDesc::Create2D(
		OutputSize,
		PF_FloatRGBA,
		FClearValueBinding::None,
		TexCreate_ShaderResource | TexCreate_UAV);

	TArray<FRDGTextureRef, TInlineAllocator<MaxPreviewMasks>> FinalSeedTextures;
	FVector4f TargetT0(1.0f, 1.0f, 1.0f, 1.0f);
	FVector4f TargetT1(1.0f, 1.0f, 1.0f, 1.0f);
	const int32 MaskCount = FMath::Min(Request.Masks.Num(), MaxPreviewMasks);

	for (int32 MaskIndex = 0; MaskIndex < MaskCount; ++MaskIndex)
	{
		const FFastPreviewMask& Mask = Request.Masks[MaskIndex];
		if (!Mask.Texture.IsValid() || Mask.Size.X <= 0 || Mask.Size.Y <= 0)
		{
			continue;
		}

		FRDGTextureRef SourceTexture = RegisterExternalTexture(
			GraphBuilder,
			Mask.Texture.GetReference(),
			TEXT("QuickSDF.Live.SourceMask"));
		FRDGTextureRef DownsampledMask = GraphBuilder.CreateTexture(
			MaskDesc,
			TEXT("QuickSDF.Live.DownsampledMask"));
		FRDGTextureRef InitialSeed = GraphBuilder.CreateTexture(
			SeedDesc,
			TEXT("QuickSDF.Live.InitialSeed"));

		AddDownsamplePass(GraphBuilder, Request.FeatureLevel, SourceTexture, Mask.Size, DownsampledMask, OutputSize);
		AddSeedPass(GraphBuilder, Request.FeatureLevel, DownsampledMask, InitialSeed, OutputSize);
		const int32 SeedIndex = FinalSeedTextures.Num();
		FinalSeedTextures.Add(AddJumpFloodPasses(GraphBuilder, Request.FeatureLevel, InitialSeed, SeedDesc, OutputSize));
		SetTargetTComponent(TargetT0, TargetT1, SeedIndex, Mask.TargetT);
	}

	if (FinalSeedTextures.Num() == 0)
	{
		return;
	}

	FRDGTextureRef OutputTexture = GraphBuilder.CreateTexture(
		FRDGTextureDesc::Create2D(
			OutputSize,
			PF_FloatRGBA,
			FClearValueBinding::White,
			TexCreate_ShaderResource | TexCreate_UAV),
		TEXT("QuickSDF.Live.OutputPreview"));
	AddResolvePass(
		GraphBuilder,
		Request.FeatureLevel,
		FinalSeedTextures,
		TargetT0,
		TargetT1,
		Request.CurrentTargetT,
		FinalSeedTextures.Num(),
		OutputTexture,
		OutputSize);

	FRDGTextureRef OutputRenderTargetTexture = RegisterExternalTexture(
		GraphBuilder,
		Request.OutputRenderTargetTexture.GetReference(),
		TEXT("QuickSDF.Live.OutputRenderTarget"));
	AddCopyTexturePass(GraphBuilder, OutputTexture, OutputRenderTargetTexture, FRHICopyTextureInfo());

	if (Request.OutputTexture.IsValid() && Request.OutputTexture.GetReference() != Request.OutputRenderTargetTexture.GetReference())
	{
		FRDGTextureRef OutputShaderTexture = RegisterExternalTexture(
			GraphBuilder,
			Request.OutputTexture.GetReference(),
			TEXT("QuickSDF.Live.OutputTexture"));
		AddCopyTexturePass(GraphBuilder, OutputTexture, OutputShaderTexture, FRHICopyTextureInfo());
	}

	GraphBuilder.Execute();
}
}
