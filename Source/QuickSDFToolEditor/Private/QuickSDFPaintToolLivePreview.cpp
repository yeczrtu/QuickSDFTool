#include "QuickSDFPaintTool.h"

#include "Async/Async.h"
#include "Editor.h"
#include "Engine/TextureRenderTarget2D.h"
#include "QuickSDFFastPreviewRendering.h"
#include "QuickSDFAsset.h"
#include "QuickSDFToolSubsystem.h"
#include "RenderingThread.h"
#include "RHI.h"
#include "TextureResource.h"

namespace
{
constexpr int32 QuickSDFLivePreviewDefaultResolution = 512;
constexpr int32 QuickSDFLivePreviewMaxResolution = 1024;
constexpr int32 QuickSDFLivePreviewMinSize = 16;
constexpr double QuickSDFLivePreviewStrokeInterval = 0.1;

struct FQuickSDFLivePreviewMaskSource
{
	UTextureRenderTarget2D* RenderTarget = nullptr;
	float TargetT = 0.0f;
};

UQuickSDFAsset* GetActiveQuickSDFAsset()
{
	UQuickSDFToolSubsystem* Subsystem = GEditor ? GEditor->GetEditorSubsystem<UQuickSDFToolSubsystem>() : nullptr;
	return Subsystem ? Subsystem->GetActiveSDFAsset() : nullptr;
}

float GetLivePreviewMaterialAngle(const UQuickSDFToolProperties* Properties, const FQuickSDFAngleData& AngleData)
{
	return Properties
		? Properties->GetMaterialAngle(AngleData.Angle, AngleData.AngleOffsetDeltaDegrees)
		: AngleData.Angle;
}

float GetLivePreviewCurrentTargetT(const UQuickSDFToolProperties* Properties, const UQuickSDFAsset* Asset)
{
	if (!Properties)
	{
		return 0.0f;
	}

	const float MaxAngle = FMath::Max(Properties->GetPaintMaxAngle(), 1.0f);
	float CurrentAngle = 0.0f;
	if (Properties->TargetAngles.IsValidIndex(Properties->EditAngleIndex))
	{
		CurrentAngle = Properties->GetMaterialAngleForKey(Properties->EditAngleIndex);
	}
	else if (Asset && Asset->GetActiveAngleDataList().Num() > 0)
	{
		const int32 AngleIndex = FMath::Clamp(Properties->EditAngleIndex, 0, Asset->GetActiveAngleDataList().Num() - 1);
		const FQuickSDFAngleData& AngleData = Asset->GetActiveAngleDataList()[AngleIndex];
		CurrentAngle = GetLivePreviewMaterialAngle(Properties, AngleData);
	}

	return FMath::Clamp(FMath::Abs(CurrentAngle) / MaxAngle, 0.0f, 1.0f);
}

FTextureRHIRef GetRenderTargetShaderTexture(UTextureRenderTarget2D* RenderTarget)
{
	if (!RenderTarget)
	{
		return nullptr;
	}

	FTextureRenderTargetResource* Resource = RenderTarget->GameThread_GetRenderTargetResource();
	if (!Resource)
	{
		return nullptr;
	}

	return Resource->TextureRHI.IsValid()
		? Resource->TextureRHI
		: Resource->GetRenderTargetTexture();
}

bool TryGetRenderTargetOutputTextures(
	UTextureRenderTarget2D* RenderTarget,
	FTextureRHIRef& OutRenderTargetTexture,
	FTextureRHIRef& OutShaderTexture)
{
	if (!RenderTarget)
	{
		return false;
	}

	FTextureRenderTargetResource* Resource = RenderTarget->GameThread_GetRenderTargetResource();
	if (!Resource)
	{
		return false;
	}

	OutRenderTargetTexture = Resource->GetRenderTargetTexture();
	OutShaderTexture = Resource->TextureRHI.IsValid() ? Resource->TextureRHI : OutRenderTargetTexture;
	return OutRenderTargetTexture.IsValid();
}

int32 GetLiveSDFPreviewRequestedResolution(const UQuickSDFToolProperties* Properties)
{
	if (!Properties)
	{
		return QuickSDFLivePreviewDefaultResolution;
	}

	switch (Properties->LiveSDFPreviewResolution)
	{
	case EQuickSDFLivePreviewResolution::Preview128:
		return 128;
	case EQuickSDFLivePreviewResolution::Preview256:
		return 256;
	case EQuickSDFLivePreviewResolution::Preview1024:
		return 1024;
	case EQuickSDFLivePreviewResolution::Preview512:
	default:
		return 512;
	}
}
}

void UQuickSDFPaintTool::ResetLiveSDFPreviewState()
{
	LiveSDFPreviewRenderTarget = nullptr;
	++LiveSDFSourceRevision;
	LiveSDFPreviewRevision = INDEX_NONE;
	LiveSDFPreviewRequestedRevision = INDEX_NONE;
	bLiveSDFPreviewDirty = true;
	bLiveSDFPreviewRenderPending = false;
	LastLiveSDFPreviewRequestTime = -1000.0;
}

bool UQuickSDFPaintTool::CanUseLiveSDFPreview() const
{
	if (!Properties || GMaxRHIFeatureLevel < ERHIFeatureLevel::SM5)
	{
		return false;
	}

	const UQuickSDFAsset* Asset = GetActiveQuickSDFAsset();
	if (!Asset)
	{
		return false;
	}

	for (const FQuickSDFAngleData& AngleData : Asset->GetActiveAngleDataList())
	{
		if (AngleData.PaintRenderTarget)
		{
			return true;
		}
	}

	return false;
}

void UQuickSDFPaintTool::MarkLiveSDFPreviewDirty()
{
	++LiveSDFSourceRevision;
	bLiveSDFPreviewDirty = true;
}

FIntPoint UQuickSDFPaintTool::GetLiveSDFPreviewSize() const
{
	FIntPoint SourceSize = Properties ? Properties->Resolution : FIntPoint(1024, 1024);
	if (const UQuickSDFAsset* Asset = GetActiveQuickSDFAsset())
	{
		SourceSize = Asset->GetActiveResolution();
	}

	const int32 RequestedResolution = FMath::Clamp(
		GetLiveSDFPreviewRequestedResolution(Properties),
		QuickSDFLivePreviewMinSize,
		QuickSDFLivePreviewMaxResolution);
	const int32 SourceMaxDim = FMath::Max(FMath::Max(SourceSize.X, SourceSize.Y), 1);
	const float PreviewScale = static_cast<float>(RequestedResolution) / static_cast<float>(SourceMaxDim);
	return FIntPoint(
		FMath::Clamp(FMath::RoundToInt(static_cast<float>(FMath::Max(SourceSize.X, 1)) * PreviewScale), QuickSDFLivePreviewMinSize, QuickSDFLivePreviewMaxResolution),
		FMath::Clamp(FMath::RoundToInt(static_cast<float>(FMath::Max(SourceSize.Y, 1)) * PreviewScale), QuickSDFLivePreviewMinSize, QuickSDFLivePreviewMaxResolution));
}

void UQuickSDFPaintTool::UpdateLiveSDFPreview()
{
	if (!Properties || Properties->MaterialPreviewMode != EQuickSDFMaterialPreviewMode::LiveSDF)
	{
		return;
	}

	if (!CanUseLiveSDFPreview() || bLiveSDFPreviewRenderPending)
	{
		return;
	}

	const bool bNeedsInitialPreview =
		!LiveSDFPreviewRenderTarget ||
		LiveSDFPreviewRevision != LiveSDFSourceRevision;
	if (!bLiveSDFPreviewDirty && !bNeedsInitialPreview)
	{
		return;
	}

	const double CurrentTime = GetToolCurrentTime();
	const double MinInterval = bStrokeTransactionActive ? QuickSDFLivePreviewStrokeInterval : 0.0;
	if (CurrentTime - LastLiveSDFPreviewRequestTime < MinInterval)
	{
		return;
	}

	QueueLiveSDFPreviewRender();
}

bool UQuickSDFPaintTool::QueueLiveSDFPreviewRender()
{
	if (!Properties || Properties->MaterialPreviewMode != EQuickSDFMaterialPreviewMode::LiveSDF)
	{
		return false;
	}

	UQuickSDFAsset* Asset = GetActiveQuickSDFAsset();
	if (!Asset)
	{
		return false;
	}

	const FIntPoint PreviewSize = GetLiveSDFPreviewSize();
	if (!LiveSDFPreviewRenderTarget ||
		LiveSDFPreviewRenderTarget->SizeX != PreviewSize.X ||
		LiveSDFPreviewRenderTarget->SizeY != PreviewSize.Y)
	{
		LiveSDFPreviewRenderTarget = NewObject<UTextureRenderTarget2D>(this, NAME_None, RF_Transient);
		LiveSDFPreviewRenderTarget->ClearColor = FLinearColor::White;
		LiveSDFPreviewRenderTarget->SRGB = false;
		LiveSDFPreviewRenderTarget->bAutoGenerateMips = false;
		LiveSDFPreviewRenderTarget->bSupportsUAV = true;
		LiveSDFPreviewRenderTarget->InitCustomFormat(PreviewSize.X, PreviewSize.Y, PF_FloatRGBA, true);
		LiveSDFPreviewRenderTarget->UpdateResourceImmediate(true);
		LiveSDFPreviewRevision = INDEX_NONE;
	}

	FTextureRHIRef OutputRenderTargetTexture;
	FTextureRHIRef OutputShaderTexture;
	if (!TryGetRenderTargetOutputTextures(LiveSDFPreviewRenderTarget, OutputRenderTargetTexture, OutputShaderTexture))
	{
		return false;
	}

	TArray<FQuickSDFLivePreviewMaskSource> Sources;
	const float MaxAngle = FMath::Max(Properties->GetPaintMaxAngle(), 1.0f);
	for (const FQuickSDFAngleData& AngleData : Asset->GetActiveAngleDataList())
	{
		if (!AngleData.PaintRenderTarget)
		{
			continue;
		}

		FQuickSDFLivePreviewMaskSource& Source = Sources.AddDefaulted_GetRef();
		Source.RenderTarget = AngleData.PaintRenderTarget;
		Source.TargetT = FMath::Clamp(FMath::Abs(GetLivePreviewMaterialAngle(Properties, AngleData)) / MaxAngle, 0.0f, 1.0f);
	}

	if (Sources.Num() == 0)
	{
		return false;
	}

	Sources.Sort([](const FQuickSDFLivePreviewMaskSource& A, const FQuickSDFLivePreviewMaskSource& B)
	{
		return A.TargetT < B.TargetT;
	});

	const float CurrentTargetT = GetLivePreviewCurrentTargetT(Properties, Asset);
	int32 UpperIndex = Sources.IndexOfByPredicate([CurrentTargetT](const FQuickSDFLivePreviewMaskSource& Source)
	{
		return Source.TargetT >= CurrentTargetT;
	});
	if (UpperIndex == INDEX_NONE)
	{
		UpperIndex = Sources.Num() - 1;
	}

	TArray<int32, TInlineAllocator<QuickSDFFastPreviewRendering::MaxPreviewMasks>> SelectedSourceIndices;
	if (Sources.Num() <= QuickSDFFastPreviewRendering::MaxPreviewMasks)
	{
		for (int32 SourceIndex = 0; SourceIndex < Sources.Num(); ++SourceIndex)
		{
			SelectedSourceIndices.Add(SourceIndex);
		}
	}
	else
	{
		auto AddSelectedSourceIndex = [&SelectedSourceIndices, SourceCount = Sources.Num()](int32 Index)
		{
			if (Index >= 0 &&
				Index < SourceCount &&
				SelectedSourceIndices.Num() < QuickSDFFastPreviewRendering::MaxPreviewMasks &&
				!SelectedSourceIndices.Contains(Index))
			{
				SelectedSourceIndices.Add(Index);
			}
		};

		AddSelectedSourceIndex(0);
		AddSelectedSourceIndex(Sources.Num() - 1);
		AddSelectedSourceIndex(UpperIndex - 2);
		AddSelectedSourceIndex(UpperIndex - 1);
		AddSelectedSourceIndex(UpperIndex);
		AddSelectedSourceIndex(UpperIndex + 1);
		AddSelectedSourceIndex(UpperIndex + 2);
		for (int32 EvenIndex = 0;
			EvenIndex < QuickSDFFastPreviewRendering::MaxPreviewMasks && SelectedSourceIndices.Num() < QuickSDFFastPreviewRendering::MaxPreviewMasks;
			++EvenIndex)
		{
			const float Alpha = static_cast<float>(EvenIndex) / static_cast<float>(QuickSDFFastPreviewRendering::MaxPreviewMasks - 1);
			AddSelectedSourceIndex(FMath::RoundToInt(Alpha * static_cast<float>(Sources.Num() - 1)));
		}
	}
	SelectedSourceIndices.Sort();

	QuickSDFFastPreviewRendering::FFastPreviewRenderRequest Request;
	Request.OutputRenderTargetTexture = OutputRenderTargetTexture;
	Request.OutputTexture = OutputShaderTexture;
	Request.OutputSize = PreviewSize;
	Request.CurrentTargetT = CurrentTargetT;
	Request.FeatureLevel = GMaxRHIFeatureLevel;
	for (int32 SelectedIndex = 0; SelectedIndex < SelectedSourceIndices.Num(); ++SelectedIndex)
	{
		const int32 SourceIndex = SelectedSourceIndices[SelectedIndex];
		const FQuickSDFLivePreviewMaskSource& Source = Sources[SourceIndex];
		FTextureRHIRef SourceTexture = GetRenderTargetShaderTexture(Source.RenderTarget);
		if (!SourceTexture.IsValid())
		{
			continue;
		}

		QuickSDFFastPreviewRendering::FFastPreviewMask& Mask = Request.Masks.AddDefaulted_GetRef();
		Mask.Texture = SourceTexture;
		Mask.Size = FIntPoint(Source.RenderTarget->SizeX, Source.RenderTarget->SizeY);
		Mask.TargetT = Source.TargetT;
	}

	if (Request.Masks.Num() == 0)
	{
		return false;
	}

	const int32 RequestedRevision = LiveSDFSourceRevision;
	LiveSDFPreviewRequestedRevision = RequestedRevision;
	bLiveSDFPreviewDirty = false;
	bLiveSDFPreviewRenderPending = true;
	LastLiveSDFPreviewRequestTime = GetToolCurrentTime();

	TWeakObjectPtr<UQuickSDFPaintTool> WeakThis(this);
	ENQUEUE_RENDER_COMMAND(QuickSDFLivePreviewRenderCommand)(
		[Request = MoveTemp(Request), WeakThis, RequestedRevision](FRHICommandListImmediate& RHICmdList) mutable
		{
			QuickSDFFastPreviewRendering::RenderFastPreview_RenderThread(RHICmdList, Request);
			AsyncTask(ENamedThreads::GameThread, [WeakThis, RequestedRevision]()
			{
				if (WeakThis.IsValid())
				{
					WeakThis->OnLiveSDFPreviewRenderComplete(RequestedRevision);
				}
			});
		});

	UpdateGeneratedSDFMaterialParameters();
	ApplyMaterialPreviewMode();
	if (GEditor)
	{
		GEditor->RedrawAllViewports(false);
	}
	return true;
}

void UQuickSDFPaintTool::OnLiveSDFPreviewRenderComplete(int32 CompletedRevision)
{
	const bool bCurrentRequest = CompletedRevision == LiveSDFPreviewRequestedRevision;
	if (bCurrentRequest)
	{
		bLiveSDFPreviewRenderPending = false;
	}
	LiveSDFPreviewRevision = FMath::Max(LiveSDFPreviewRevision, CompletedRevision);
	if (!bCurrentRequest)
	{
		return;
	}

	RefreshPreviewMaterial();
	if (GEditor)
	{
		GEditor->RedrawAllViewports(false);
	}
}
