#include "QuickSDFPaintTool.h"
#include "QuickSDFMonotonicGuard.h"
#include "QuickSDFPaintToolPrivate.h"
#include "QuickSDFMeshComponentAdapter.h"
#include "QuickSDFToolSubsystem.h"
#include "QuickSDFAsset.h"
#include "SDFProcessor.h"
#include "BaseGizmos/BrushStampIndicator.h"
#include "InteractiveToolManager.h"
#include "ToolBuilderUtil.h"
#include "EngineUtils.h"
#include "Engine/DirectionalLight.h"
#include "CollisionQueryParams.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/Texture2D.h"
#include "BaseBehaviors/ClickDragBehavior.h"
#include "TargetInterfaces/PrimitiveComponentBackedTarget.h"
#include "TargetInterfaces/MeshDescriptionProvider.h"
#include "DynamicMesh/MeshTransforms.h"
#include "DynamicMesh/DynamicMeshAttributeSet.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Components/PrimitiveComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/SkinnedMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Kismet/KismetRenderingLibrary.h"
#include "Intersection/IntrRay3Triangle3.h"
#include "Spatial/SpatialInterfaces.h"
#include "IndexTypes.h"
#include "Engine/Canvas.h"
#include "CanvasItem.h"
#include "CanvasTypes.h"
#include "TextureResource.h"
#include "RenderResource.h"
#include "RenderingThread.h"
#include "Math/UnrealMathUtility.h"
#include "InputCoreTypes.h"
#include "HAL/PlatformApplicationMisc.h"
#include "HAL/PlatformTime.h"
#include "InteractiveToolChange.h"
#include "Misc/ScopedSlowTask.h"
#include "Misc/MessageDialog.h"
#include "DesktopPlatformModule.h"
#include "Engine/Selection.h"
#include "Framework/Application/SlateApplication.h"
#include "IDesktopPlatform.h"
#include "Misc/DefaultValueHelper.h"
#include "Containers/Ticker.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetToolsModule.h"
#include "IAssetTools.h"
#include "Misc/PackageName.h"
#include "ObjectTools.h"

#if WITH_EDITOR
#include "Editor.h"
#include "IMaterialBakingModule.h"
#include "MaterialBakingStructures.h"
#endif

#define LOCTEXT_NAMESPACE "QuickSDFPaintTool"

using namespace QuickSDFPaintToolPrivate;

namespace
{
float GetQuickSDFMaterialPreviewModeValue(EQuickSDFMaterialPreviewMode Mode)
{
	switch (Mode)
	{
	case EQuickSDFMaterialPreviewMode::UV:
		return 1.0f;
	case EQuickSDFMaterialPreviewMode::OriginalShadow:
		return 2.0f;
	case EQuickSDFMaterialPreviewMode::Mask:
	case EQuickSDFMaterialPreviewMode::OriginalMaterial:
	case EQuickSDFMaterialPreviewMode::GeneratedSDF:
	default:
		return 0.0f;
	}
}

float GetQuickSDFCurrentPreviewAngle(const UQuickSDFToolProperties* Properties)
{
	if (!Properties)
	{
		return 0.0f;
	}

	if (Properties->TargetAngles.IsValidIndex(Properties->EditAngleIndex))
	{
		return Properties->TargetAngles[Properties->EditAngleIndex];
	}

	UQuickSDFToolSubsystem* Subsystem = GEditor ? GEditor->GetEditorSubsystem<UQuickSDFToolSubsystem>() : nullptr;
	const UQuickSDFAsset* Asset = Subsystem ? Subsystem->GetActiveSDFAsset() : nullptr;
	if (!Asset)
	{
		return 0.0f;
	}

	const int32 AngleIndex = Asset->GetActiveAngleDataList().Num() > 0
		? FMath::Clamp(Properties->EditAngleIndex, 0, Asset->GetActiveAngleDataList().Num() - 1)
		: INDEX_NONE;
	return Asset->GetActiveAngleDataList().IsValidIndex(AngleIndex)
		? Asset->GetActiveAngleDataList()[AngleIndex].Angle
		: 0.0f;
}

bool CaptureRenderTargetPixelsInRect(UTextureRenderTarget2D* RenderTarget, const FIntRect& Rect, TArray<FColor>& OutPixels)
{
	if (!RenderTarget || Rect.Width() <= 0 || Rect.Height() <= 0 ||
		Rect.Min.X < 0 || Rect.Min.Y < 0 || Rect.Max.X > RenderTarget->SizeX || Rect.Max.Y > RenderTarget->SizeY)
	{
		return false;
	}

	FTextureRenderTargetResource* RTResource = RenderTarget->GameThread_GetRenderTargetResource();
	if (!RTResource)
	{
		return false;
	}

	FReadSurfaceDataFlags ReadFlags(RCM_UNorm);
	ReadFlags.SetLinearToGamma(false);
	return RTResource->ReadPixels(OutPixels, ReadFlags, Rect) && OutPixels.Num() == Rect.Width() * Rect.Height();
}

FIntRect UnionRects(const FIntRect& A, const FIntRect& B)
{
	return FIntRect(
		FMath::Min(A.Min.X, B.Min.X),
		FMath::Min(A.Min.Y, B.Min.Y),
		FMath::Max(A.Max.X, B.Max.X),
		FMath::Max(A.Max.Y, B.Max.Y));
}

struct FQuickSDFGuardStrokeEntry
{
	int32 AngleIndex = INDEX_NONE;
	float Angle = 0.0f;
	UTextureRenderTarget2D* BeforeRenderTarget = nullptr;
	UTextureRenderTarget2D* PaintRenderTarget = nullptr;
	TArray<FColor> BeforePixels;
	TArray<FColor> AfterPixels;
	bool bChangedByStroke = false;
	bool bNeedsRestore = false;
};

TArray<FQuickSDFGuardStrokeEntry> MakeSortedGuardStrokeEntries(
	UQuickSDFAsset* Asset,
	const TArray<int32>& GuardAngleIndices,
	const TArray<int32>& StrokeAngleIndices,
	const TArray<TObjectPtr<UTextureRenderTarget2D>>& BeforeRenderTargets)
{
	TArray<FQuickSDFGuardStrokeEntry> Entries;
	if (!Asset)
	{
		return Entries;
	}

	Entries.Reserve(GuardAngleIndices.Num());
	for (int32 AngleIndex : GuardAngleIndices)
	{
		if (!Asset->GetActiveAngleDataList().IsValidIndex(AngleIndex))
		{
			continue;
		}

		FQuickSDFAngleData& AngleData = Asset->GetActiveAngleDataList()[AngleIndex];
		UTextureRenderTarget2D* PaintRenderTarget = AngleData.PaintRenderTarget;
		if (!PaintRenderTarget)
		{
			continue;
		}

		FQuickSDFGuardStrokeEntry& Entry = Entries.AddDefaulted_GetRef();
		Entry.AngleIndex = AngleIndex;
		Entry.Angle = AngleData.Angle;
		Entry.PaintRenderTarget = PaintRenderTarget;

		const int32 TransactionIndex = StrokeAngleIndices.IndexOfByKey(AngleIndex);
		if (BeforeRenderTargets.IsValidIndex(TransactionIndex))
		{
			Entry.BeforeRenderTarget = BeforeRenderTargets[TransactionIndex].Get();
			Entry.bChangedByStroke = Entry.BeforeRenderTarget != nullptr;
		}
	}

	Entries.Sort([](const FQuickSDFGuardStrokeEntry& A, const FQuickSDFGuardStrokeEntry& B)
	{
		return A.Angle < B.Angle;
	});
	return Entries;
}
}

void UQuickSDFPaintTool::BuildBrushMaskTexture()
{
	if (BrushMaskTexture)
	{
		return;
	}

	BrushMaskTexture = UTexture2D::CreateTransient(QuickSDFBrushMaskResolution, QuickSDFBrushMaskResolution, PF_B8G8R8A8);
	if (!BrushMaskTexture || !BrushMaskTexture->GetPlatformData() || BrushMaskTexture->GetPlatformData()->Mips.Num() == 0)
	{
		return;
	}

	BrushMaskTexture->MipGenSettings = TMGS_NoMipmaps;
	BrushMaskTexture->Filter = TF_Bilinear;
	BrushMaskTexture->SRGB = false;

	FTexture2DMipMap& Mip = BrushMaskTexture->GetPlatformData()->Mips[0];
	FColor* Pixels = static_cast<FColor*>(Mip.BulkData.Lock(LOCK_READ_WRITE));
	if (!Pixels)
	{
		Mip.BulkData.Unlock();
		return;
	}

	const bool bUseAntialiasing = !Properties || Properties->bEnableBrushAntialiasing;
	const float AAWidth = bUseAntialiasing
		? FMath::Max(Properties ? Properties->BrushAntialiasingWidth : 1.25f, 0.25f)
		: 0.0f;
	const float Radius = (QuickSDFBrushMaskResolution - 1) * 0.5f;
	const FVector2f Center(Radius, Radius);

	for (int32 Y = 0; Y < QuickSDFBrushMaskResolution; ++Y)
	{
		for (int32 X = 0; X < QuickSDFBrushMaskResolution; ++X)
		{
			const FVector2f Pos(static_cast<float>(X), static_cast<float>(Y));
			const float Dist = FVector2f::Distance(Pos, Center);
			float Coverage = Dist <= Radius ? 1.0f : 0.0f;
			if (bUseAntialiasing)
			{
				const float SignedDistance = Radius - Dist;
				Coverage = FMath::SmoothStep(-AAWidth, AAWidth, SignedDistance);
			}
			const uint8 Alpha = static_cast<uint8>(FMath::Clamp(Coverage, 0.0f, 1.0f) * 255.0f);

			Pixels[Y * QuickSDFBrushMaskResolution + X] = FColor(255, 255, 255, Alpha);
		}
	}

	Mip.BulkData.Unlock();
	BrushMaskTexture->UpdateResource();
}

void UQuickSDFPaintTool::UpdatePreviewMaterialParameters(UMaterialInstanceDynamic* Material)
{
	if (!Material || !Properties)
	{
		return;
	}

	if (UTextureRenderTarget2D* ActiveRT = GetActiveRenderTarget())
	{
		Material->SetTextureParameterValue(TEXT("BaseColor"), ActiveRT);
	}

	const float Angle = GetQuickSDFCurrentPreviewAngle(Properties);
	const FQuickSDFMeshBakeBasis BakeBasis = FQuickSDFMeshComponentAdapter::GetBakeBasisForComponent(CurrentComponent.Get());
	const EQuickSDFMaterialPreviewMode PreviewMode = Properties->MaterialPreviewMode;

	Material->SetScalarParameterValue(TEXT("PreviewMode"), GetQuickSDFMaterialPreviewModeValue(PreviewMode));
	Material->SetScalarParameterValue(TEXT("UVChannel"), static_cast<float>(Properties->UVChannel));
	Material->SetScalarParameterValue(TEXT("Angle"), Angle);
	Material->SetScalarParameterValue(TEXT("BakeForwardAngleOffset"), BakeBasis.ForwardAngleOffsetDegrees);
}

UTexture2D* UQuickSDFPaintTool::GetActiveFinalSDFTexture() const
{
	UQuickSDFToolSubsystem* Subsystem = GEditor ? GEditor->GetEditorSubsystem<UQuickSDFToolSubsystem>() : nullptr;
	const UQuickSDFAsset* Asset = Subsystem ? Subsystem->GetActiveSDFAsset() : nullptr;
	return Asset ? Asset->GetActiveFinalSDFTexture() : nullptr;
}

bool UQuickSDFPaintTool::CanUseGeneratedSDFPreview() const
{
	return GetActiveFinalSDFTexture() != nullptr;
}

FText UQuickSDFPaintTool::GetGeneratedSDFPreviewUnavailableText() const
{
	return LOCTEXT("GeneratedSDFPreviewUnavailable", "SDFテクスチャがまだありません。Generate Selected SDF を実行すると、この表示で生成結果を確認できます。");
}

FText UQuickSDFPaintTool::GetMaterialPreviewStatusText() const
{
	const EQuickSDFMaterialPreviewMode PreviewMode = Properties
		? Properties->MaterialPreviewMode
		: EQuickSDFMaterialPreviewMode::OriginalMaterial;

	if (PreviewMode == EQuickSDFMaterialPreviewMode::GeneratedSDF)
	{
		if (UTexture2D* FinalTexture = GetActiveFinalSDFTexture())
		{
			return FText::Format(
				LOCTEXT("GeneratedSDFPreviewStatusWithTexture", "Generated SDF ({0})"),
				FText::FromString(FinalTexture->GetName()));
		}
		return LOCTEXT("GeneratedSDFPreviewStatusMissing", "Generated SDF (missing)");
	}

	switch (PreviewMode)
	{
	case EQuickSDFMaterialPreviewMode::OriginalMaterial:
		return LOCTEXT("OriginalPaintPreviewStatus", "Original + Painted");
	case EQuickSDFMaterialPreviewMode::Mask:
		return LOCTEXT("PaintedTexturePreviewStatus", "Painted Texture");
	case EQuickSDFMaterialPreviewMode::UV:
		return LOCTEXT("PaintUVPreviewStatus", "Painted + UV");
	case EQuickSDFMaterialPreviewMode::OriginalShadow:
		return LOCTEXT("PaintShadowPreviewStatus", "Painted + Shadow");
	default:
		return LOCTEXT("UnknownPreviewStatus", "Unknown");
	}
}

void UQuickSDFPaintTool::UpdateGeneratedSDFMaterialParameters()
{
	if (!SDFToonPreviewMaterial || !Properties)
	{
		return;
	}

	UTexture2D* FinalTexture = GetActiveFinalSDFTexture();
	if (FinalTexture)
	{
		SDFToonPreviewMaterial->SetTextureParameterValue(TEXT("ThresholdMap"), FinalTexture);
		SDFToonPreviewMaterial->SetTextureParameterValue(TEXT("ThresholdMapGray"), FinalTexture);
	}

	const bool bSymmetryUV = Properties->UsesWholeTextureSymmetry();
	const bool bUseGrayscaleTexture = FinalTexture &&
		FinalTexture->CompressionSettings == TC_Grayscale &&
		!Properties->UsesIslandChannelSymmetry();

	const FQuickSDFMeshBakeBasis BakeBasis = FQuickSDFMeshComponentAdapter::GetBakeBasisForComponent(CurrentComponent.Get());
	const FVector ForwardVector = BakeBasis.Forward.GetSafeNormal();

	SDFToonPreviewMaterial->SetScalarParameterValue(TEXT("UVChannel"), static_cast<float>(Properties->UVChannel));
	SDFToonPreviewMaterial->SetScalarParameterValue(TEXT("SymmetryUV"), bSymmetryUV ? 1.0f : 0.0f);
	SDFToonPreviewMaterial->SetScalarParameterValue(TEXT("SymmetryMode"), static_cast<float>(static_cast<uint8>(Properties->SymmetryMode)));
	SDFToonPreviewMaterial->SetScalarParameterValue(TEXT("UseGrayscaleTexture"), bUseGrayscaleTexture ? 1.0f : 0.0f);
	SDFToonPreviewMaterial->SetVectorParameterValue(TEXT("ForwardVector"), FLinearColor(ForwardVector.X, ForwardVector.Y, ForwardVector.Z, 0.0f));
}

void UQuickSDFPaintTool::RefreshPreviewMaterial()
{
	if (Properties &&
		Properties->MaterialPreviewMode == EQuickSDFMaterialPreviewMode::GeneratedSDF &&
		!CanUseGeneratedSDFPreview())
	{
		Properties->MaterialPreviewMode = EQuickSDFMaterialPreviewMode::OriginalMaterial;
	}

	UpdatePreviewMaterialParameters(PreviewMaterial);
	UpdatePreviewMaterialParameters(PreviewOverlayMaterial);
	UpdateGeneratedSDFMaterialParameters();
	ApplyMaterialPreviewMode();
	ClearPreviewMaterialDirtyState();
}

bool UQuickSDFPaintTool::CaptureRenderTargetPixels(UTextureRenderTarget2D* RenderTarget, TArray<FColor>& OutPixels) const
{
	if (!RenderTarget) return false;
	FTextureRenderTargetResource* RTResource = RenderTarget->GameThread_GetRenderTargetResource();
	if (!RTResource) return false;
	FReadSurfaceDataFlags ReadFlags(RCM_UNorm);
	ReadFlags.SetLinearToGamma(false);
	return RTResource->ReadPixels(OutPixels, ReadFlags);
}

bool UQuickSDFPaintTool::RestoreRenderTargetPixels(UTextureRenderTarget2D* RenderTarget, const TArray<FColor>& Pixels) const
{
	if (!RenderTarget || Pixels.Num() != RenderTarget->SizeX * RenderTarget->SizeY) return false;
	UTexture2D* TempTexture = CreateTransientTextureFromPixels(Pixels, RenderTarget->SizeX, RenderTarget->SizeY);
	return RestoreRenderTargetTexture(RenderTarget, TempTexture);
}

bool UQuickSDFPaintTool::RestoreRenderTargetPixelsInRect(UTextureRenderTarget2D* RenderTarget, const FIntRect& Rect, const TArray<FColor>& Pixels) const
{
	if (!RenderTarget || Rect.Width() <= 0 || Rect.Height() <= 0 ||
		Rect.Min.X < 0 || Rect.Min.Y < 0 || Rect.Max.X > RenderTarget->SizeX || Rect.Max.Y > RenderTarget->SizeY ||
		Pixels.Num() != Rect.Width() * Rect.Height())
	{
		return false;
	}

	UTexture2D* TempTexture = CreateTransientTextureFromPixels(Pixels, Rect.Width(), Rect.Height());
	if (!TempTexture)
	{
		return false;
	}

	FTextureRenderTargetResource* RTResource = RenderTarget->GameThread_GetRenderTargetResource();
	if (!RTResource) return false;
	FCanvas Canvas(RTResource, nullptr, GetToolManager()->GetContextQueriesAPI()->GetCurrentEditingWorld(), GMaxRHIFeatureLevel);
	FCanvasTileItem TileItem(FVector2D(Rect.Min.X, Rect.Min.Y), TempTexture->GetResource(), FVector2D(Rect.Width(), Rect.Height()), FLinearColor::White);
	TileItem.BlendMode = SE_BLEND_Opaque;
	Canvas.DrawItem(TileItem);
	Canvas.Flush_GameThread(true);
	ENQUEUE_RENDER_COMMAND(RestoreQuickSDFPaintRTRectCommand)(
		[RTResource](FRHICommandListImmediate& RHICmdList) {
			TransitionAndCopyTexture(RHICmdList, RTResource->GetRenderTargetTexture(), RTResource->TextureRHI, {});
		});
	FlushRenderingCommands();
	return true;
}

UTexture2D* UQuickSDFPaintTool::CreateTransientTextureFromPixels(const TArray<FColor>& Pixels, int32 Width, int32 Height) const
{
	if (Pixels.Num() != Width * Height) return nullptr;
	UTexture2D* TempTexture = UTexture2D::CreateTransient(Width, Height, PF_B8G8R8A8);
	if (!TempTexture || !TempTexture->GetPlatformData() || TempTexture->GetPlatformData()->Mips.Num() == 0) return nullptr;
	TempTexture->MipGenSettings = TMGS_NoMipmaps;
	TempTexture->Filter = TF_Nearest;
	TempTexture->SRGB = false;
	FTexture2DMipMap& Mip = TempTexture->GetPlatformData()->Mips[0];
	void* Data = Mip.BulkData.Lock(LOCK_READ_WRITE);
	FMemory::Memcpy(Data, Pixels.GetData(), Pixels.Num() * sizeof(FColor));
	Mip.BulkData.Unlock();
	TempTexture->UpdateResource();
	FlushRenderingCommands();
	return TempTexture;
}

bool UQuickSDFPaintTool::RestoreRenderTargetTexture(UTextureRenderTarget2D* RenderTarget, UTexture2D* Texture) const
{
	if (!RenderTarget || !Texture) return false;
	FTextureRenderTargetResource* RTResource = RenderTarget->GameThread_GetRenderTargetResource();
	if (!RTResource) return false;
	FCanvas Canvas(RTResource, nullptr, GetToolManager()->GetContextQueriesAPI()->GetCurrentEditingWorld(), GMaxRHIFeatureLevel);
	FCanvasTileItem TileItem(FVector2D::ZeroVector, Texture->GetResource(), FVector2D(RenderTarget->SizeX, RenderTarget->SizeY), FLinearColor::White);
	TileItem.BlendMode = SE_BLEND_Opaque;
	Canvas.DrawItem(TileItem);
	Canvas.Flush_GameThread(true);
	ENQUEUE_RENDER_COMMAND(RestoreQuickSDFPaintRTCommand)(
		[RTResource](FRHICommandListImmediate& RHICmdList) {
			TransitionAndCopyTexture(RHICmdList, RTResource->GetRenderTargetTexture(), RTResource->TextureRHI, {});
		});
	FlushRenderingCommands();
	return true;
}

bool UQuickSDFPaintTool::CopyRenderTargetToRenderTarget(UTextureRenderTarget2D* SourceRenderTarget, UTextureRenderTarget2D* DestinationRenderTarget) const
{
	if (!SourceRenderTarget || !DestinationRenderTarget)
	{
		return false;
	}

	FTextureRenderTargetResource* DestinationResource = DestinationRenderTarget->GameThread_GetRenderTargetResource();
	if (!DestinationResource)
	{
		return false;
	}

	FCanvas Canvas(DestinationResource, nullptr, GetToolManager()->GetContextQueriesAPI()->GetCurrentEditingWorld(), GMaxRHIFeatureLevel);
	FCanvasTileItem TileItem(
		FVector2D::ZeroVector,
		SourceRenderTarget->GetResource(),
		FVector2D(DestinationRenderTarget->SizeX, DestinationRenderTarget->SizeY),
		FLinearColor::White);
	TileItem.BlendMode = SE_BLEND_Opaque;
	Canvas.DrawItem(TileItem);
	Canvas.Flush_GameThread(false);
	ENQUEUE_RENDER_COMMAND(CopyQuickSDFPaintRTCommand)(
		[DestinationResource](FRHICommandListImmediate& RHICmdList) {
			TransitionAndCopyTexture(RHICmdList, DestinationResource->GetRenderTargetTexture(), DestinationResource->TextureRHI, {});
		});
	FlushRenderingCommands();
	return true;
}

bool UQuickSDFPaintTool::RestoreStrokeStartPixels() const
{
	UQuickSDFToolSubsystem* Subsystem = GEditor->GetEditorSubsystem<UQuickSDFToolSubsystem>();
	UQuickSDFAsset* Asset = Subsystem ? Subsystem->GetActiveSDFAsset() : nullptr;
	if (!Asset)
	{
		return false;
	}

	bool bRestoredAny = false;
	for (int32 Index = 0; Index < StrokeTransactionAngleIndices.Num() && Index < StrokeBeforeRenderTargetsByAngle.Num(); ++Index)
	{
		const int32 AngleIndex = StrokeTransactionAngleIndices[Index];
		if (!Asset->GetActiveAngleDataList().IsValidIndex(AngleIndex))
		{
			continue;
		}

		UTextureRenderTarget2D* PaintRenderTarget = Asset->GetActiveAngleDataList()[AngleIndex].PaintRenderTarget;
		UTextureRenderTarget2D* BeforeRenderTarget = StrokeBeforeRenderTargetsByAngle[Index];
		if (!PaintRenderTarget || !BeforeRenderTarget)
		{
			continue;
		}

		if (ActiveStrokeInputMode == EQuickSDFStrokeInputMode::MeshSurface && bQuickLineActive)
		{
			if (CopyRenderTargetToRenderTarget(BeforeRenderTarget, PaintRenderTarget))
			{
				bRestoredAny = true;
			}
			continue;
		}

		const FIntRect DirtyRect = StrokeDirtyRectsByAngle.IsValidIndex(Index) ? StrokeDirtyRectsByAngle[Index] : FIntRect();
		const FIntRect ClampedRect(
			FMath::Clamp(DirtyRect.Min.X, 0, PaintRenderTarget->SizeX),
			FMath::Clamp(DirtyRect.Min.Y, 0, PaintRenderTarget->SizeY),
			FMath::Clamp(DirtyRect.Max.X, 0, PaintRenderTarget->SizeX),
			FMath::Clamp(DirtyRect.Max.Y, 0, PaintRenderTarget->SizeY));
		if (ClampedRect.Width() <= 0 || ClampedRect.Height() <= 0)
		{
			continue;
		}

		TArray<FColor> BeforePixels;
		if (CaptureRenderTargetPixelsInRect(BeforeRenderTarget, ClampedRect, BeforePixels) &&
			RestoreRenderTargetPixelsInRect(PaintRenderTarget, ClampedRect, BeforePixels))
		{
			bRestoredAny = true;
		}
	}

	if (bRestoredAny)
	{
		const_cast<UQuickSDFPaintTool*>(this)->RefreshPreviewMaterial();
	}
	return bRestoredAny;
}

bool UQuickSDFPaintTool::ApplyRenderTargetPixels(int32 AngleIndex, const TArray<FColor>& Pixels)
{
	if (!Properties) return false;
	
	UQuickSDFToolSubsystem* Subsystem = GEditor->GetEditorSubsystem<UQuickSDFToolSubsystem>();
	if (!Subsystem || !Subsystem->GetActiveSDFAsset()) return false;

	UQuickSDFAsset* Asset = Subsystem->GetActiveSDFAsset();
	if (!Asset->GetActiveAngleDataList().IsValidIndex(AngleIndex)) return false;
	
	const bool bRestored = RestoreRenderTargetPixels(Asset->GetActiveAngleDataList()[AngleIndex].PaintRenderTarget, Pixels);
	if (bRestored)
	{
		RefreshPreviewMaterial();
		MarkMasksChanged();
	}
	return bRestored;
}

bool UQuickSDFPaintTool::ApplyRenderTargetPixelsByGuid(const FGuid& AngleGuid, const TArray<FColor>& Pixels)
{
	UQuickSDFToolSubsystem* Subsystem = GEditor ? GEditor->GetEditorSubsystem<UQuickSDFToolSubsystem>() : nullptr;
	UQuickSDFAsset* Asset = Subsystem ? Subsystem->GetActiveSDFAsset() : nullptr;
	const int32 AngleIndex = FindAngleIndexByGuid(Asset, AngleGuid);
	if (AngleIndex == INDEX_NONE)
	{
		return false;
	}

	return ApplyRenderTargetPixels(AngleIndex, Pixels);
}

bool UQuickSDFPaintTool::ApplyRenderTargetPixelsInRect(int32 AngleIndex, const FIntRect& Rect, const TArray<FColor>& Pixels)
{
	if (!Properties) return false;

	UQuickSDFToolSubsystem* Subsystem = GEditor->GetEditorSubsystem<UQuickSDFToolSubsystem>();
	if (!Subsystem || !Subsystem->GetActiveSDFAsset()) return false;

	UQuickSDFAsset* Asset = Subsystem->GetActiveSDFAsset();
	if (!Asset->GetActiveAngleDataList().IsValidIndex(AngleIndex)) return false;

	const bool bRestored = RestoreRenderTargetPixelsInRect(Asset->GetActiveAngleDataList()[AngleIndex].PaintRenderTarget, Rect, Pixels);
	if (bRestored)
	{
		RefreshPreviewMaterial();
		MarkMasksChanged();
	}
	return bRestored;
}

bool UQuickSDFPaintTool::ApplyRenderTargetPixelsInRectByGuid(const FGuid& AngleGuid, int32 FallbackIndex, const FIntRect& Rect, const TArray<FColor>& Pixels)
{
	UQuickSDFToolSubsystem* Subsystem = GEditor ? GEditor->GetEditorSubsystem<UQuickSDFToolSubsystem>() : nullptr;
	UQuickSDFAsset* Asset = Subsystem ? Subsystem->GetActiveSDFAsset() : nullptr;
	const int32 AngleIndex = FindAngleIndexByGuid(Asset, AngleGuid);
	if (AngleIndex != INDEX_NONE)
	{
		return ApplyRenderTargetPixelsInRect(AngleIndex, Rect, Pixels);
	}
	if (!AngleGuid.IsValid())
	{
		return ApplyRenderTargetPixelsInRect(FallbackIndex, Rect, Pixels);
	}
	return false;
}

bool UQuickSDFPaintTool::ApplyTextureSlotChange(const FGuid& AngleGuid, int32 FallbackIndex, UTexture2D* Texture, bool bAllowSourceTextureOverwrite, const TArray<FColor>& Pixels)
{
	if (!Properties)
	{
		return false;
	}

	UQuickSDFToolSubsystem* Subsystem = GEditor ? GEditor->GetEditorSubsystem<UQuickSDFToolSubsystem>() : nullptr;
	UQuickSDFAsset* Asset = Subsystem ? Subsystem->GetActiveSDFAsset() : nullptr;
	if (!Subsystem || !Asset)
	{
		return false;
	}

	const int32 GuidIndex = FindAngleIndexByGuid(Asset, AngleGuid);
	const int32 AngleIndex = GuidIndex != INDEX_NONE ? GuidIndex : (AngleGuid.IsValid() ? INDEX_NONE : FallbackIndex);
	if (!Asset->GetActiveAngleDataList().IsValidIndex(AngleIndex))
	{
		return false;
	}

	FQuickSDFAngleData& AngleData = Asset->GetActiveAngleDataList()[AngleIndex];
	if (!AngleData.PaintRenderTarget)
	{
		Asset->InitializeRenderTargets(GetToolManager()->GetContextQueriesAPI()->GetCurrentEditingWorld());
	}
	if (!AngleData.PaintRenderTarget)
	{
		return false;
	}

	AngleData.TextureMask = Texture;
	AngleData.bAllowSourceTextureOverwrite = bAllowSourceTextureOverwrite;
	if (Properties->TargetTextures.IsValidIndex(AngleIndex))
	{
		Properties->TargetTextures[AngleIndex] = Texture;
	}

	bool bRestored = false;
	if (Pixels.Num() == AngleData.PaintRenderTarget->SizeX * AngleData.PaintRenderTarget->SizeY)
	{
		bRestored = RestoreRenderTargetPixels(AngleData.PaintRenderTarget, Pixels);
	}
	else if (Texture)
	{
		Subsystem->DrawTextureToRenderTarget(Texture, AngleData.PaintRenderTarget);
		bRestored = true;
	}
	else
	{
		Subsystem->ClearRenderTarget(AngleData.PaintRenderTarget);
		bRestored = true;
	}

	if (bRestored)
	{
		RefreshPreviewMaterial();
		MarkMasksChanged();
	}
	return bRestored;
}

void UQuickSDFPaintTool::RestoreMaskStateByGuid(const TArray<FGuid>& MaskGuids, const TArray<float>& Angles, const TArray<UTexture2D*>& Textures, const TArray<bool>& AllowSourceTextureOverwrites, const TArray<TArray<FColor>>& PixelsByMask)
{
	if (!Properties)
	{
		return;
	}

	UQuickSDFToolSubsystem* Subsystem = GEditor ? GEditor->GetEditorSubsystem<UQuickSDFToolSubsystem>() : nullptr;
	UQuickSDFAsset* Asset = Subsystem ? Subsystem->GetActiveSDFAsset() : nullptr;
	if (!Asset)
	{
		return;
	}

	Asset->GetActiveAngleDataList().SetNum(MaskGuids.Num());
	for (int32 SnapshotIndex = 0; SnapshotIndex < MaskGuids.Num(); ++SnapshotIndex)
	{
		FQuickSDFAngleData& AngleData = Asset->GetActiveAngleDataList()[SnapshotIndex];
		AngleData.Angle = Angles.IsValidIndex(SnapshotIndex) ? Angles[SnapshotIndex] : 0.0f;
		AngleData.MaskGuid = MaskGuids[SnapshotIndex].IsValid() ? MaskGuids[SnapshotIndex] : FGuid::NewGuid();
		AngleData.TextureMask = Textures.IsValidIndex(SnapshotIndex) ? Textures[SnapshotIndex] : nullptr;
		AngleData.bAllowSourceTextureOverwrite = AllowSourceTextureOverwrites.IsValidIndex(SnapshotIndex) ? AllowSourceTextureOverwrites[SnapshotIndex] : false;
		AngleData.PaintRenderTarget = nullptr;
	}

	Asset->InitializeRenderTargets(GetToolManager()->GetContextQueriesAPI()->GetCurrentEditingWorld());

	for (int32 SnapshotIndex = 0; SnapshotIndex < MaskGuids.Num(); ++SnapshotIndex)
	{
		if (!Asset->GetActiveAngleDataList().IsValidIndex(SnapshotIndex))
		{
			continue;
		}

		FQuickSDFAngleData& AngleData = Asset->GetActiveAngleDataList()[SnapshotIndex];
		if (PixelsByMask.IsValidIndex(SnapshotIndex) && PixelsByMask[SnapshotIndex].Num() > 0 && AngleData.PaintRenderTarget)
		{
			RestoreRenderTargetPixels(AngleData.PaintRenderTarget, PixelsByMask[SnapshotIndex]);
		}
		else if (AngleData.TextureMask && AngleData.PaintRenderTarget && Subsystem)
		{
			Subsystem->DrawTextureToRenderTarget(AngleData.TextureMask, AngleData.PaintRenderTarget);
		}
	}

	SyncPropertiesFromActiveAsset();
	RefreshPreviewMaterial();
	MarkMasksChanged();
}

bool UQuickSDFPaintTool::ApplyPixelsWithUndo(int32 AngleIndex, const TArray<FColor>& Pixels, const FText& ChangeDescription)
{
	if (!Properties)
	{
		return false;
	}

	UQuickSDFToolSubsystem* Subsystem = GEditor->GetEditorSubsystem<UQuickSDFToolSubsystem>();
	UQuickSDFAsset* Asset = Subsystem ? Subsystem->GetActiveSDFAsset() : nullptr;
	if (!Asset || !Asset->GetActiveAngleDataList().IsValidIndex(AngleIndex))
	{
		return false;
	}
	EnsureMaskGuids(Asset);

	UTextureRenderTarget2D* RenderTarget = Asset->GetActiveAngleDataList()[AngleIndex].PaintRenderTarget;
	TArray<FColor> BeforePixels;
	if (!RenderTarget || !CaptureRenderTargetPixels(RenderTarget, BeforePixels) || BeforePixels.Num() != Pixels.Num())
	{
		return false;
	}

	if (BeforePixels == Pixels)
	{
		return false;
	}

	if (!RestoreRenderTargetPixels(RenderTarget, Pixels))
	{
		return false;
	}

	if (!bSuppressMaskPixelUndo)
	{
		TUniquePtr<FQuickSDFRenderTargetsChange> Change = MakeUnique<FQuickSDFRenderTargetsChange>();
		Change->AngleIndices.Add(AngleIndex);
		Change->AngleGuids.Add(Asset->GetActiveAngleDataList()[AngleIndex].MaskGuid);
		Change->BeforePixelsByAngle.Add(MoveTemp(BeforePixels));
		Change->AfterPixelsByAngle.Add(Pixels);
		GetToolManager()->EmitObjectChange(this, MoveTemp(Change), ChangeDescription);
	}
	RefreshPreviewMaterial();
	MarkMasksChanged();
	return true;
}

bool UQuickSDFPaintTool::CopyNearestMaskToAngle(int32 DestinationIndex)
{
	UQuickSDFToolSubsystem* Subsystem = GEditor->GetEditorSubsystem<UQuickSDFToolSubsystem>();
	UQuickSDFAsset* Asset = Subsystem ? Subsystem->GetActiveSDFAsset() : nullptr;
	if (!Asset || !Asset->GetActiveAngleDataList().IsValidIndex(DestinationIndex))
	{
		return false;
	}

	const float DestinationAngle = Asset->GetActiveAngleDataList()[DestinationIndex].Angle;
	int32 SourceIndex = INDEX_NONE;
	float BestDistance = TNumericLimits<float>::Max();
	for (int32 Index = 0; Index < Asset->GetActiveAngleDataList().Num(); ++Index)
	{
		if (Index == DestinationIndex || !Asset->GetActiveAngleDataList()[Index].PaintRenderTarget)
		{
			continue;
		}

		const float Distance = FMath::Abs(Asset->GetActiveAngleDataList()[Index].Angle - DestinationAngle);
		if (Distance < BestDistance)
		{
			BestDistance = Distance;
			SourceIndex = Index;
		}
	}

	UTextureRenderTarget2D* DestinationRT = Asset->GetActiveAngleDataList()[DestinationIndex].PaintRenderTarget;
	if (!DestinationRT)
	{
		return false;
	}

	TArray<FColor> Pixels;
	if (SourceIndex != INDEX_NONE && CaptureRenderTargetPixels(Asset->GetActiveAngleDataList()[SourceIndex].PaintRenderTarget, Pixels))
	{
		return ApplyPixelsWithUndo(DestinationIndex, Pixels, LOCTEXT("CopyNearestMaskChange", "Copy Nearest Quick SDF Mask"));
	}

	return ApplyPixelsWithUndo(
		DestinationIndex,
		MakeSolidPixels(DestinationRT->SizeX, DestinationRT->SizeY, FLinearColor::White),
		LOCTEXT("FillMissingMaskWhiteChange", "Fill Missing Quick SDF Mask White"));
}

void UQuickSDFPaintTool::AddStrokeDirtyRect(UTextureRenderTarget2D* RenderTarget, const FIntRect& Rect)
{
	if (!bStrokeTransactionActive || !RenderTarget || Rect.Width() <= 0 || Rect.Height() <= 0)
	{
		return;
	}

	const FIntRect ClampedRect(
		FMath::Clamp(Rect.Min.X, 0, RenderTarget->SizeX),
		FMath::Clamp(Rect.Min.Y, 0, RenderTarget->SizeY),
		FMath::Clamp(Rect.Max.X, 0, RenderTarget->SizeX),
		FMath::Clamp(Rect.Max.Y, 0, RenderTarget->SizeY));

	if (ClampedRect.Width() <= 0 || ClampedRect.Height() <= 0)
	{
		return;
	}

	UQuickSDFToolSubsystem* Subsystem = GEditor ? GEditor->GetEditorSubsystem<UQuickSDFToolSubsystem>() : nullptr;
	UQuickSDFAsset* Asset = Subsystem ? Subsystem->GetActiveSDFAsset() : nullptr;
	int32 TransactionIndex = INDEX_NONE;
	if (Asset)
	{
		for (int32 Index = 0; Index < StrokeTransactionAngleIndices.Num(); ++Index)
		{
			const int32 AngleIndex = StrokeTransactionAngleIndices[Index];
			if (Asset->GetActiveAngleDataList().IsValidIndex(AngleIndex) &&
				Asset->GetActiveAngleDataList()[AngleIndex].PaintRenderTarget == RenderTarget)
			{
				TransactionIndex = Index;
				break;
			}
		}
	}

	if (TransactionIndex == INDEX_NONE)
	{
		return;
	}

	StrokeDirtyRectsByAngle.SetNum(StrokeTransactionAngleIndices.Num());

	const FIntRect ExistingRect = StrokeDirtyRectsByAngle[TransactionIndex];
	const bool bHasExistingRect = ExistingRect.Width() > 0 && ExistingRect.Height() > 0;
	StrokeDirtyRectsByAngle[TransactionIndex] = bHasExistingRect ? UnionRects(ExistingRect, ClampedRect) : ClampedRect;

	if (!bHasStrokeDirtyRect)
	{
		StrokeDirtyRect = ClampedRect;
		bHasStrokeDirtyRect = true;
		return;
	}

	StrokeDirtyRect.Min.X = FMath::Min(StrokeDirtyRect.Min.X, ClampedRect.Min.X);
	StrokeDirtyRect.Min.Y = FMath::Min(StrokeDirtyRect.Min.Y, ClampedRect.Min.Y);
	StrokeDirtyRect.Max.X = FMath::Max(StrokeDirtyRect.Max.X, ClampedRect.Max.X);
	StrokeDirtyRect.Max.Y = FMath::Max(StrokeDirtyRect.Max.Y, ClampedRect.Max.Y);
}

void UQuickSDFPaintTool::BeginStrokeTransaction()
{
	if (bStrokeTransactionActive) return;
	
	StrokeDirtyRectsByAngle.Reset();
	StrokeBeforeRenderTargetsByAngle.Reset();
	StrokeDirtyRect = FIntRect();
	bHasStrokeDirtyRect = false;
	const TArray<int32> CandidateAngleIndices = GetPaintTargetAngleIndices();
	StrokeTransactionAngleIndices.Reset();
	UQuickSDFToolSubsystem* Subsystem = GEditor ? GEditor->GetEditorSubsystem<UQuickSDFToolSubsystem>() : nullptr;
	UQuickSDFAsset* ActiveAsset = Subsystem ? Subsystem->GetActiveSDFAsset() : nullptr;
	EnsureMaskGuids(ActiveAsset);

	for (int32 AngleIndex : CandidateAngleIndices)
	{
		UQuickSDFAsset* Asset = ActiveAsset;
		if (!Asset || !Asset->GetActiveAngleDataList().IsValidIndex(AngleIndex))
		{
			continue;
		}

		UTextureRenderTarget2D* PaintRenderTarget = Asset->GetActiveAngleDataList()[AngleIndex].PaintRenderTarget;
		if (!PaintRenderTarget)
		{
			continue;
		}

		UTextureRenderTarget2D* StrokeSnapshot = NewObject<UTextureRenderTarget2D>(this);
		if (!StrokeSnapshot)
		{
			continue;
		}
		StrokeSnapshot->RenderTargetFormat = PaintRenderTarget->RenderTargetFormat;
		StrokeSnapshot->ClearColor = PaintRenderTarget->ClearColor;
		StrokeSnapshot->SRGB = PaintRenderTarget->SRGB;
		StrokeSnapshot->InitAutoFormat(PaintRenderTarget->SizeX, PaintRenderTarget->SizeY);
		StrokeSnapshot->UpdateResourceImmediate(true);
		if (!CopyRenderTargetToRenderTarget(PaintRenderTarget, StrokeSnapshot))
		{
			continue;
		}

		StrokeTransactionAngleIndices.Add(AngleIndex);
		StrokeBeforeRenderTargetsByAngle.Add(StrokeSnapshot);
		if (StrokeTransactionAngleIndices.Num() == 1)
		{
			StrokeTransactionAngleIndex = AngleIndex;
		}
	}

	StrokeDirtyRectsByAngle.SetNum(StrokeTransactionAngleIndices.Num());
	bStrokeTransactionActive = StrokeTransactionAngleIndices.Num() > 0;
}

bool UQuickSDFPaintTool::ApplyMonotonicGuardToStroke(UQuickSDFAsset* Asset)
{
	if (!Properties || !Properties->bEnableMonotonicGuard || !Asset || StrokeTransactionAngleIndices.Num() < 1)
	{
		return false;
	}

	FIntRect GuardRect;
	bool bHasGuardRect = false;
	for (const FIntRect& DirtyRect : StrokeDirtyRectsByAngle)
	{
		if (DirtyRect.Width() <= 0 || DirtyRect.Height() <= 0)
		{
			continue;
		}

		GuardRect = bHasGuardRect ? UnionRects(GuardRect, DirtyRect) : DirtyRect;
		bHasGuardRect = true;
	}

	if (!bHasGuardRect)
	{
		return false;
	}

	TArray<int32> GuardAngleIndices = CollectProcessableMaskIndices(*Asset, Properties->bSymmetryMode);
	for (int32 AngleIndex : StrokeTransactionAngleIndices)
	{
		GuardAngleIndices.AddUnique(AngleIndex);
	}

	TArray<FQuickSDFGuardStrokeEntry> Entries = MakeSortedGuardStrokeEntries(Asset, GuardAngleIndices, StrokeTransactionAngleIndices, StrokeBeforeRenderTargetsByAngle);
	if (Entries.Num() < 2)
	{
		return false;
	}

	UTextureRenderTarget2D* ReferenceRenderTarget = Entries[0].PaintRenderTarget;
	if (!ReferenceRenderTarget)
	{
		return false;
	}

	const FIntRect ClampedRect(
		FMath::Clamp(GuardRect.Min.X, 0, ReferenceRenderTarget->SizeX),
		FMath::Clamp(GuardRect.Min.Y, 0, ReferenceRenderTarget->SizeY),
		FMath::Clamp(GuardRect.Max.X, 0, ReferenceRenderTarget->SizeX),
		FMath::Clamp(GuardRect.Max.Y, 0, ReferenceRenderTarget->SizeY));
	if (ClampedRect.Width() <= 0 || ClampedRect.Height() <= 0)
	{
		return false;
	}

	for (FQuickSDFGuardStrokeEntry& Entry : Entries)
	{
		if (!Entry.PaintRenderTarget || Entry.PaintRenderTarget->SizeX != ReferenceRenderTarget->SizeX || Entry.PaintRenderTarget->SizeY != ReferenceRenderTarget->SizeY)
		{
			return false;
		}

		if (!CaptureRenderTargetPixelsInRect(Entry.PaintRenderTarget, ClampedRect, Entry.AfterPixels))
		{
			return false;
		}

		if (Entry.bChangedByStroke)
		{
			if (!CaptureRenderTargetPixelsInRect(Entry.BeforeRenderTarget, ClampedRect, Entry.BeforePixels))
			{
				return false;
			}
		}
		else
		{
			Entry.BeforePixels = Entry.AfterPixels;
		}
	}

	TArray<float> Angles;
	TArray<bool> BeforeStates;
	TArray<bool> AfterStates;
	TArray<bool> ProjectedAfterStates;
	TArray<bool> CandidateStates;
	TArray<bool> RestoreEntries;
	Angles.Reserve(Entries.Num());
	BeforeStates.SetNum(Entries.Num());
	AfterStates.SetNum(Entries.Num());
	ProjectedAfterStates.SetNum(Entries.Num());
	CandidateStates.SetNum(Entries.Num());
	RestoreEntries.SetNum(Entries.Num());
	for (const FQuickSDFGuardStrokeEntry& Entry : Entries)
	{
		Angles.Add(Entry.Angle);
	}

	bool bClippedAny = false;
	const int32 PixelCount = ClampedRect.Width() * ClampedRect.Height();
	for (int32 PixelIndex = 0; PixelIndex < PixelCount; ++PixelIndex)
	{
		bool bPixelChanged = false;
		for (int32 EntryIndex = 0; EntryIndex < Entries.Num(); ++EntryIndex)
		{
			const FQuickSDFGuardStrokeEntry& Entry = Entries[EntryIndex];
			BeforeStates[EntryIndex] = QuickSDFMonotonicGuard::IsWhite(Entry.BeforePixels[PixelIndex]);
			AfterStates[EntryIndex] = QuickSDFMonotonicGuard::IsWhite(Entry.AfterPixels[PixelIndex]);
			ProjectedAfterStates[EntryIndex] = AfterStates[EntryIndex];
			if (Entry.bChangedByStroke && Entry.BeforePixels[PixelIndex] != Entry.AfterPixels[PixelIndex])
			{
				ProjectedAfterStates[EntryIndex] = QuickSDFMonotonicGuard::GetProjectedStrokeState(Entry.BeforePixels[PixelIndex], Entry.AfterPixels[PixelIndex]);
				bPixelChanged = true;
			}
		}

		if (!bPixelChanged)
		{
			continue;
		}

		const int32 BeforeViolations = QuickSDFMonotonicGuard::CountViolations(BeforeStates, Angles, Properties->ClipDirection);
		const int32 AfterViolations = QuickSDFMonotonicGuard::CountViolations(ProjectedAfterStates, Angles, Properties->ClipDirection);
		if (AfterViolations <= BeforeViolations)
		{
			continue;
		}

		CandidateStates = ProjectedAfterStates;
		for (bool& bRestoreEntry : RestoreEntries)
		{
			bRestoreEntry = false;
		}

		int32 CurrentViolations = AfterViolations;
		while (CurrentViolations > BeforeViolations)
		{
			int32 BestEntryIndex = INDEX_NONE;
			int32 BestViolationCount = CurrentViolations;
			for (int32 EntryIndex = 0; EntryIndex < Entries.Num(); ++EntryIndex)
			{
				if (!Entries[EntryIndex].bChangedByStroke || RestoreEntries[EntryIndex] || CandidateStates[EntryIndex] == BeforeStates[EntryIndex])
				{
					continue;
				}

				const bool bSavedState = CandidateStates[EntryIndex];
				CandidateStates[EntryIndex] = BeforeStates[EntryIndex];
				const int32 TestViolations = QuickSDFMonotonicGuard::CountViolations(CandidateStates, Angles, Properties->ClipDirection);
				CandidateStates[EntryIndex] = bSavedState;
				if (TestViolations < BestViolationCount)
				{
					BestEntryIndex = EntryIndex;
					BestViolationCount = TestViolations;
					if (BestViolationCount <= BeforeViolations)
					{
						break;
					}
				}
			}

			if (BestEntryIndex == INDEX_NONE)
			{
				for (int32 EntryIndex = 0; EntryIndex < Entries.Num(); ++EntryIndex)
				{
					if (Entries[EntryIndex].bChangedByStroke && CandidateStates[EntryIndex] != BeforeStates[EntryIndex])
					{
						RestoreEntries[EntryIndex] = true;
						CandidateStates[EntryIndex] = BeforeStates[EntryIndex];
					}
				}
				break;
			}

			RestoreEntries[BestEntryIndex] = true;
			CandidateStates[BestEntryIndex] = BeforeStates[BestEntryIndex];
			CurrentViolations = BestViolationCount;
		}

		for (int32 EntryIndex = 0; EntryIndex < Entries.Num(); ++EntryIndex)
		{
			if (RestoreEntries[EntryIndex])
			{
				FQuickSDFGuardStrokeEntry& Entry = Entries[EntryIndex];
				Entry.AfterPixels[PixelIndex] = Entry.BeforePixels[PixelIndex];
				Entry.bNeedsRestore = true;
				bClippedAny = true;
			}
		}
	}

	if (!bClippedAny)
	{
		return false;
	}

	bool bRestoredAny = false;
	for (FQuickSDFGuardStrokeEntry& Entry : Entries)
	{
		if (Entry.bNeedsRestore && RestoreRenderTargetPixelsInRect(Entry.PaintRenderTarget, ClampedRect, Entry.AfterPixels))
		{
			bRestoredAny = true;
		}
	}

	if (bRestoredAny)
	{
		RefreshPreviewMaterial();
	}
	return bRestoredAny;
}

int32 UQuickSDFPaintTool::ValidateMonotonicGuardForAsset(UQuickSDFAsset* Asset, int32* OutTransitionViolations) const
{
	if (OutTransitionViolations)
	{
		*OutTransitionViolations = 0;
	}
	if (!Properties || !Asset)
	{
		return 0;
	}

	TArray<int32> ProcessableIndices = CollectProcessableMaskIndices(*Asset, Properties->bSymmetryMode);
	if (ProcessableIndices.Num() < 2)
	{
		return 0;
	}

	struct FValidationEntry
	{
		float Angle = 0.0f;
		UTextureRenderTarget2D* RenderTarget = nullptr;
		TArray<FColor> Pixels;
	};

	TArray<FValidationEntry> Entries;
	Entries.Reserve(ProcessableIndices.Num());
	for (int32 AngleIndex : ProcessableIndices)
	{
		if (!Asset->GetActiveAngleDataList().IsValidIndex(AngleIndex))
		{
			continue;
		}

		const FQuickSDFAngleData& AngleData = Asset->GetActiveAngleDataList()[AngleIndex];
		if (!AngleData.PaintRenderTarget)
		{
			continue;
		}

		FValidationEntry& Entry = Entries.AddDefaulted_GetRef();
		Entry.Angle = AngleData.Angle;
		Entry.RenderTarget = AngleData.PaintRenderTarget;
	}

	if (Entries.Num() < 2)
	{
		return 0;
	}

	Entries.Sort([](const FValidationEntry& A, const FValidationEntry& B)
	{
		return A.Angle < B.Angle;
	});

	UTextureRenderTarget2D* ReferenceRenderTarget = Entries[0].RenderTarget;
	if (!ReferenceRenderTarget)
	{
		return 0;
	}

	for (const FValidationEntry& Entry : Entries)
	{
		if (!Entry.RenderTarget ||
			Entry.RenderTarget->SizeX != ReferenceRenderTarget->SizeX ||
			Entry.RenderTarget->SizeY != ReferenceRenderTarget->SizeY)
		{
			return 0;
		}
	}

	TArray<float> Angles;
	TArray<bool> States;
	Angles.Reserve(Entries.Num());
	States.SetNum(Entries.Num());
	for (const FValidationEntry& Entry : Entries)
	{
		Angles.Add(Entry.Angle);
	}

	constexpr int32 TileSize = 256;
	int32 ViolationPixels = 0;
	int32 ViolationTransitions = 0;
	for (int32 MinY = 0; MinY < ReferenceRenderTarget->SizeY; MinY += TileSize)
	{
		for (int32 MinX = 0; MinX < ReferenceRenderTarget->SizeX; MinX += TileSize)
		{
			const FIntRect TileRect(
				MinX,
				MinY,
				FMath::Min(MinX + TileSize, ReferenceRenderTarget->SizeX),
				FMath::Min(MinY + TileSize, ReferenceRenderTarget->SizeY));
			const int32 PixelCount = TileRect.Width() * TileRect.Height();
			if (PixelCount <= 0)
			{
				continue;
			}

			bool bCapturedAll = true;
			for (FValidationEntry& Entry : Entries)
			{
				Entry.Pixels.Reset();
				if (!CaptureRenderTargetPixelsInRect(Entry.RenderTarget, TileRect, Entry.Pixels) || Entry.Pixels.Num() != PixelCount)
				{
					bCapturedAll = false;
					break;
				}
			}
			if (!bCapturedAll)
			{
				continue;
			}

			for (int32 PixelIndex = 0; PixelIndex < PixelCount; ++PixelIndex)
			{
				for (int32 EntryIndex = 0; EntryIndex < Entries.Num(); ++EntryIndex)
				{
					States[EntryIndex] = QuickSDFMonotonicGuard::IsWhite(Entries[EntryIndex].Pixels[PixelIndex]);
				}

				const int32 Violations = QuickSDFMonotonicGuard::CountViolations(States, Angles, Properties->ClipDirection);
				if (Violations > 0)
				{
					++ViolationPixels;
					ViolationTransitions += Violations;
				}
			}
		}
	}

	if (OutTransitionViolations)
	{
		*OutTransitionViolations = ViolationTransitions;
	}
	return ViolationPixels;
}

void UQuickSDFPaintTool::WarnIfMonotonicGuardViolations(const FText& Context)
{
	if (!Properties || !Properties->bEnableMonotonicGuard)
	{
		return;
	}

	UQuickSDFToolSubsystem* Subsystem = GEditor ? GEditor->GetEditorSubsystem<UQuickSDFToolSubsystem>() : nullptr;
	UQuickSDFAsset* Asset = Subsystem ? Subsystem->GetActiveSDFAsset() : nullptr;
	if (!Asset)
	{
		return;
	}

	int32 TransitionViolations = 0;
	const int32 PixelViolations = ValidateMonotonicGuardForAsset(Asset, &TransitionViolations);
	if (PixelViolations <= 0)
	{
		return;
	}

	GetToolManager()->DisplayMessage(
		FText::Format(
			LOCTEXT("MonotonicGuardWarningFormat", "Monotonic Guard found {0} UV pixels with {1} invalid transitions {2}. Masks were not changed."),
			FText::AsNumber(PixelViolations),
			FText::AsNumber(TransitionViolations),
			Context),
		EToolMessageLevel::UserWarning);
}

void UQuickSDFPaintTool::ValidateMonotonicGuard()
{
	UQuickSDFToolSubsystem* Subsystem = GEditor ? GEditor->GetEditorSubsystem<UQuickSDFToolSubsystem>() : nullptr;
	UQuickSDFAsset* Asset = Subsystem ? Subsystem->GetActiveSDFAsset() : nullptr;
	if (!Asset)
	{
		return;
	}

	int32 TransitionViolations = 0;
	const int32 PixelViolations = ValidateMonotonicGuardForAsset(Asset, &TransitionViolations);
	if (PixelViolations > 0)
	{
		GetToolManager()->DisplayMessage(
			FText::Format(
				LOCTEXT("MonotonicGuardManualWarning", "Monotonic Guard found {0} UV pixels with {1} invalid transitions. Masks were not changed."),
				FText::AsNumber(PixelViolations),
				FText::AsNumber(TransitionViolations)),
			EToolMessageLevel::UserWarning);
		return;
	}

	GetToolManager()->DisplayMessage(
		LOCTEXT("MonotonicGuardManualValid", "Monotonic Guard validation found no invalid transitions."),
		EToolMessageLevel::UserNotification);
}

void UQuickSDFPaintTool::EndStrokeTransaction()
{
	if (!bStrokeTransactionActive) return;
	
	UQuickSDFToolSubsystem* Subsystem = GEditor->GetEditorSubsystem<UQuickSDFToolSubsystem>();
	if (Properties && Subsystem && Subsystem->GetActiveSDFAsset())
	{
		UQuickSDFAsset* Asset = Subsystem->GetActiveSDFAsset();
		EnsureMaskGuids(Asset);
		ApplyMonotonicGuardToStroke(Asset);
		TUniquePtr<FQuickSDFRenderTargetsChange> Change = MakeUnique<FQuickSDFRenderTargetsChange>();

		for (int32 Index = 0; Index < StrokeTransactionAngleIndices.Num() && Index < StrokeBeforeRenderTargetsByAngle.Num(); ++Index)
		{
			const int32 AngleIndex = StrokeTransactionAngleIndices[Index];
			if (!Asset->GetActiveAngleDataList().IsValidIndex(AngleIndex))
			{
				continue;
			}

			UTextureRenderTarget2D* PaintRenderTarget = Asset->GetActiveAngleDataList()[AngleIndex].PaintRenderTarget;
			if (!PaintRenderTarget)
			{
				continue;
			}

			UTextureRenderTarget2D* BeforeRenderTarget = StrokeBeforeRenderTargetsByAngle[Index];
			if (!BeforeRenderTarget)
			{
				continue;
			}

			const FIntRect DirtyRect = StrokeDirtyRectsByAngle.IsValidIndex(Index) ? StrokeDirtyRectsByAngle[Index] : FIntRect();
			if (DirtyRect.Width() > 0 && DirtyRect.Height() > 0)
			{
				const FIntRect ClampedRect(
					FMath::Clamp(DirtyRect.Min.X, 0, PaintRenderTarget->SizeX),
					FMath::Clamp(DirtyRect.Min.Y, 0, PaintRenderTarget->SizeY),
					FMath::Clamp(DirtyRect.Max.X, 0, PaintRenderTarget->SizeX),
					FMath::Clamp(DirtyRect.Max.Y, 0, PaintRenderTarget->SizeY));

				TArray<FColor> BeforePixels;
				TArray<FColor> AfterPixels;
				if (ClampedRect.Width() > 0 && ClampedRect.Height() > 0 &&
					CaptureRenderTargetPixelsInRect(BeforeRenderTarget, ClampedRect, BeforePixels) &&
					CaptureRenderTargetPixelsInRect(PaintRenderTarget, ClampedRect, AfterPixels) &&
					AfterPixels != BeforePixels)
				{
					Change->AngleIndices.Add(AngleIndex);
					Change->AngleGuids.Add(Asset->GetActiveAngleDataList()[AngleIndex].MaskGuid);
					Change->PixelRects.Add(ClampedRect);
					Change->BeforePixelsByAngle.Add(MoveTemp(BeforePixels));
					Change->AfterPixelsByAngle.Add(MoveTemp(AfterPixels));
				}
				continue;
			}
		}

		if (Change->AngleIndices.Num() > 0)
		{
			GetToolManager()->EmitObjectChange(this, MoveTemp(Change), LOCTEXT("QuickSDFPaintStrokeChange", "Quick SDF Paint Stroke"));
			MarkMasksChanged();
		}
	}
	
	bStrokeTransactionActive = false;
	StrokeTransactionAngleIndex = INDEX_NONE;
	StrokeTransactionAngleIndices.Reset();
	StrokeDirtyRectsByAngle.Reset();
	StrokeBeforeRenderTargetsByAngle.Reset();
	StrokeDirtyRect = FIntRect();
	bHasStrokeDirtyRect = false;
}

UTextureRenderTarget2D* UQuickSDFPaintTool::GetActiveRenderTarget() const
{
	if (!Properties) return nullptr;

	UQuickSDFToolSubsystem* Subsystem = GEditor->GetEditorSubsystem<UQuickSDFToolSubsystem>();
	if (!Subsystem || !Subsystem->GetActiveSDFAsset()) return nullptr;

	UQuickSDFAsset* Asset = Subsystem->GetActiveSDFAsset();
	if (Asset->GetActiveAngleDataList().Num() == 0) return nullptr;

	const int32 AngleIdx = FMath::Clamp(Properties->EditAngleIndex, 0, Asset->GetActiveAngleDataList().Num() - 1);
	return Asset->GetActiveAngleDataList()[AngleIdx].PaintRenderTarget;
}

TArray<int32> UQuickSDFPaintTool::GetPaintTargetAngleIndices() const
{
	TArray<int32> TargetIndices;
	if (!Properties)
	{
		return TargetIndices;
	}

	UQuickSDFToolSubsystem* Subsystem = GEditor->GetEditorSubsystem<UQuickSDFToolSubsystem>();
	UQuickSDFAsset* Asset = Subsystem ? Subsystem->GetActiveSDFAsset() : nullptr;
	if (!Asset || Asset->GetActiveAngleDataList().Num() == 0)
	{
		return TargetIndices;
	}

	const int32 CurrentAngleIndex = FMath::Clamp(Properties->EditAngleIndex, 0, Asset->GetActiveAngleDataList().Num() - 1);
	const EQuickSDFPaintTargetMode TargetMode =
		(Properties->bPaintAllAngles && Properties->PaintTargetMode == EQuickSDFPaintTargetMode::CurrentOnly)
			? EQuickSDFPaintTargetMode::All
			: Properties->PaintTargetMode;

	if (TargetMode == EQuickSDFPaintTargetMode::CurrentOnly)
	{
		if (Asset->GetActiveAngleDataList()[CurrentAngleIndex].PaintRenderTarget)
		{
			TargetIndices.Add(CurrentAngleIndex);
		}
		return TargetIndices;
	}

	TArray<int32> SortedIndices;
	for (int32 Index = 0; Index < Asset->GetActiveAngleDataList().Num(); ++Index)
	{
		if (Asset->GetActiveAngleDataList()[Index].PaintRenderTarget)
		{
			SortedIndices.Add(Index);
		}
	}
	SortedIndices.Sort([Asset](int32 A, int32 B)
	{
		return Asset->GetActiveAngleDataList()[A].Angle < Asset->GetActiveAngleDataList()[B].Angle;
	});

	if (TargetMode == EQuickSDFPaintTargetMode::All)
	{
		return SortedIndices;
	}

	const int32 CurrentSortedIndex = SortedIndices.IndexOfByKey(CurrentAngleIndex);
	if (CurrentSortedIndex == INDEX_NONE)
	{
		return TargetIndices;
	}

	const int32 StartIndex = TargetMode == EQuickSDFPaintTargetMode::BeforeCurrent ? 0 : CurrentSortedIndex;
	const int32 EndIndex = TargetMode == EQuickSDFPaintTargetMode::BeforeCurrent ? CurrentSortedIndex : SortedIndices.Num() - 1;
	for (int32 Index = StartIndex; Index <= EndIndex; ++Index)
	{
		TargetIndices.Add(SortedIndices[Index]);
	}

	return TargetIndices;
}

#undef LOCTEXT_NAMESPACE
