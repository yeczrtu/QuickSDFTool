#include "QuickSDFPaintTool.h"
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

void UQuickSDFPaintTool::RefreshPreviewMaterial()
{
	if (!PreviewMaterial)
	{
		return;
	}

	if (UTextureRenderTarget2D* ActiveRT = GetActiveRenderTarget())
	{
		PreviewMaterial->SetTextureParameterValue(TEXT("BaseColor"), ActiveRT);
		PreviewMaterial->SetScalarParameterValue(TEXT("UVChannel"), (float)Properties->UVChannel);
		PreviewMaterial->SetScalarParameterValue(TEXT("OverlayOriginalShadow"), Properties->bOverlayOriginalShadow);
	}

	if (PreviewBaseMaterial)
	{
		PreviewBaseMaterial->GetOutermost()->SetDirtyFlag(false);
	}
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
		if (!Asset->AngleDataList.IsValidIndex(AngleIndex))
		{
			continue;
		}

		if (CopyRenderTargetToRenderTarget(StrokeBeforeRenderTargetsByAngle[Index], Asset->AngleDataList[AngleIndex].PaintRenderTarget))
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
	if (!Asset->AngleDataList.IsValidIndex(AngleIndex)) return false;
	
	const bool bRestored = RestoreRenderTargetPixels(Asset->AngleDataList[AngleIndex].PaintRenderTarget, Pixels);
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
	if (!Asset->AngleDataList.IsValidIndex(AngleIndex)) return false;

	const bool bRestored = RestoreRenderTargetPixelsInRect(Asset->AngleDataList[AngleIndex].PaintRenderTarget, Rect, Pixels);
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

bool UQuickSDFPaintTool::ApplyTextureSlotChange(const FGuid& AngleGuid, int32 FallbackIndex, UTexture2D* Texture, const TArray<FColor>& Pixels)
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
	if (!Asset->AngleDataList.IsValidIndex(AngleIndex))
	{
		return false;
	}

	FQuickSDFAngleData& AngleData = Asset->AngleDataList[AngleIndex];
	if (!AngleData.PaintRenderTarget)
	{
		Asset->InitializeRenderTargets(GetToolManager()->GetContextQueriesAPI()->GetCurrentEditingWorld());
	}
	if (!AngleData.PaintRenderTarget)
	{
		return false;
	}

	AngleData.TextureMask = Texture;
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

void UQuickSDFPaintTool::RestoreMaskStateByGuid(const TArray<FGuid>& MaskGuids, const TArray<float>& Angles, const TArray<UTexture2D*>& Textures, const TArray<TArray<FColor>>& PixelsByMask)
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

	Asset->AngleDataList.SetNum(MaskGuids.Num());
	for (int32 SnapshotIndex = 0; SnapshotIndex < MaskGuids.Num(); ++SnapshotIndex)
	{
		FQuickSDFAngleData& AngleData = Asset->AngleDataList[SnapshotIndex];
		AngleData.Angle = Angles.IsValidIndex(SnapshotIndex) ? Angles[SnapshotIndex] : 0.0f;
		AngleData.MaskGuid = MaskGuids[SnapshotIndex].IsValid() ? MaskGuids[SnapshotIndex] : FGuid::NewGuid();
		AngleData.TextureMask = Textures.IsValidIndex(SnapshotIndex) ? Textures[SnapshotIndex] : nullptr;
		AngleData.PaintRenderTarget = nullptr;
	}

	Asset->InitializeRenderTargets(GetToolManager()->GetContextQueriesAPI()->GetCurrentEditingWorld());

	for (int32 SnapshotIndex = 0; SnapshotIndex < MaskGuids.Num(); ++SnapshotIndex)
	{
		if (!Asset->AngleDataList.IsValidIndex(SnapshotIndex))
		{
			continue;
		}

		FQuickSDFAngleData& AngleData = Asset->AngleDataList[SnapshotIndex];
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
	if (!Asset || !Asset->AngleDataList.IsValidIndex(AngleIndex))
	{
		return false;
	}
	EnsureMaskGuids(Asset);

	UTextureRenderTarget2D* RenderTarget = Asset->AngleDataList[AngleIndex].PaintRenderTarget;
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
		Change->AngleGuids.Add(Asset->AngleDataList[AngleIndex].MaskGuid);
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
	if (!Asset || !Asset->AngleDataList.IsValidIndex(DestinationIndex))
	{
		return false;
	}

	const float DestinationAngle = Asset->AngleDataList[DestinationIndex].Angle;
	int32 SourceIndex = INDEX_NONE;
	float BestDistance = TNumericLimits<float>::Max();
	for (int32 Index = 0; Index < Asset->AngleDataList.Num(); ++Index)
	{
		if (Index == DestinationIndex || !Asset->AngleDataList[Index].PaintRenderTarget)
		{
			continue;
		}

		const float Distance = FMath::Abs(Asset->AngleDataList[Index].Angle - DestinationAngle);
		if (Distance < BestDistance)
		{
			BestDistance = Distance;
			SourceIndex = Index;
		}
	}

	UTextureRenderTarget2D* DestinationRT = Asset->AngleDataList[DestinationIndex].PaintRenderTarget;
	if (!DestinationRT)
	{
		return false;
	}

	TArray<FColor> Pixels;
	if (SourceIndex != INDEX_NONE && CaptureRenderTargetPixels(Asset->AngleDataList[SourceIndex].PaintRenderTarget, Pixels))
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
			if (Asset->AngleDataList.IsValidIndex(AngleIndex) &&
				Asset->AngleDataList[AngleIndex].PaintRenderTarget == RenderTarget)
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
		if (!Asset || !Asset->AngleDataList.IsValidIndex(AngleIndex))
		{
			continue;
		}

		UTextureRenderTarget2D* PaintRenderTarget = Asset->AngleDataList[AngleIndex].PaintRenderTarget;
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

void UQuickSDFPaintTool::EndStrokeTransaction()
{
	if (!bStrokeTransactionActive) return;
	
	UQuickSDFToolSubsystem* Subsystem = GEditor->GetEditorSubsystem<UQuickSDFToolSubsystem>();
	if (Properties && Subsystem && Subsystem->GetActiveSDFAsset())
	{
		UQuickSDFAsset* Asset = Subsystem->GetActiveSDFAsset();
		EnsureMaskGuids(Asset);
		TUniquePtr<FQuickSDFRenderTargetsChange> Change = MakeUnique<FQuickSDFRenderTargetsChange>();

		for (int32 Index = 0; Index < StrokeTransactionAngleIndices.Num() && Index < StrokeBeforeRenderTargetsByAngle.Num(); ++Index)
		{
			const int32 AngleIndex = StrokeTransactionAngleIndices[Index];
			if (!Asset->AngleDataList.IsValidIndex(AngleIndex))
			{
				continue;
			}

			UTextureRenderTarget2D* PaintRenderTarget = Asset->AngleDataList[AngleIndex].PaintRenderTarget;
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
					Change->AngleGuids.Add(Asset->AngleDataList[AngleIndex].MaskGuid);
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
	if (Asset->AngleDataList.Num() == 0) return nullptr;

	const int32 AngleIdx = FMath::Clamp(Properties->EditAngleIndex, 0, Asset->AngleDataList.Num() - 1);
	return Asset->AngleDataList[AngleIdx].PaintRenderTarget;
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
	if (!Asset || Asset->AngleDataList.Num() == 0)
	{
		return TargetIndices;
	}

	const int32 CurrentAngleIndex = FMath::Clamp(Properties->EditAngleIndex, 0, Asset->AngleDataList.Num() - 1);
	const EQuickSDFPaintTargetMode TargetMode =
		(Properties->bPaintAllAngles && Properties->PaintTargetMode == EQuickSDFPaintTargetMode::CurrentOnly)
			? EQuickSDFPaintTargetMode::All
			: Properties->PaintTargetMode;

	if (TargetMode == EQuickSDFPaintTargetMode::CurrentOnly)
	{
		if (Asset->AngleDataList[CurrentAngleIndex].PaintRenderTarget)
		{
			TargetIndices.Add(CurrentAngleIndex);
		}
		return TargetIndices;
	}

	TArray<int32> SortedIndices;
	for (int32 Index = 0; Index < Asset->AngleDataList.Num(); ++Index)
	{
		if (Asset->AngleDataList[Index].PaintRenderTarget)
		{
			SortedIndices.Add(Index);
		}
	}
	SortedIndices.Sort([Asset](int32 A, int32 B)
	{
		return Asset->AngleDataList[A].Angle < Asset->AngleDataList[B].Angle;
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

namespace QuickSDFPaintToolPrivate
{
void FQuickSDFRenderTargetChange::Apply(UObject* Object)
{
	if (UQuickSDFPaintTool* Tool = Cast<UQuickSDFPaintTool>(Object))
	{
		if (!AngleGuid.IsValid() || !Tool->ApplyRenderTargetPixelsByGuid(AngleGuid, AfterPixels))
		{
			if (!AngleGuid.IsValid())
			{
				Tool->ApplyRenderTargetPixels(AngleIndex, AfterPixels);
			}
		}
	}
}

void FQuickSDFRenderTargetChange::Revert(UObject* Object)
{
	if (UQuickSDFPaintTool* Tool = Cast<UQuickSDFPaintTool>(Object))
	{
		if (!AngleGuid.IsValid() || !Tool->ApplyRenderTargetPixelsByGuid(AngleGuid, BeforePixels))
		{
			if (!AngleGuid.IsValid())
			{
				Tool->ApplyRenderTargetPixels(AngleIndex, BeforePixels);
			}
		}
	}
}

void FQuickSDFRenderTargetsChange::Apply(UObject* Object)
{
	if (UQuickSDFPaintTool* Tool = Cast<UQuickSDFPaintTool>(Object))
	{
		for (int32 Index = 0; Index < AngleIndices.Num() && Index < AfterPixelsByAngle.Num(); ++Index)
		{
			const FGuid AngleGuid = AngleGuids.IsValidIndex(Index) ? AngleGuids[Index] : FGuid();
			if (PixelRects.IsValidIndex(Index) && PixelRects[Index].Width() > 0 && PixelRects[Index].Height() > 0)
			{
				Tool->ApplyRenderTargetPixelsInRectByGuid(AngleGuid, AngleIndices[Index], PixelRects[Index], AfterPixelsByAngle[Index]);
				continue;
			}
			if (!AngleGuid.IsValid() || !Tool->ApplyRenderTargetPixelsByGuid(AngleGuid, AfterPixelsByAngle[Index]))
			{
				if (!AngleGuid.IsValid())
				{
					Tool->ApplyRenderTargetPixels(AngleIndices[Index], AfterPixelsByAngle[Index]);
				}
			}
		}
	}
}

void FQuickSDFRenderTargetsChange::Revert(UObject* Object)
{
	if (UQuickSDFPaintTool* Tool = Cast<UQuickSDFPaintTool>(Object))
	{
		for (int32 Index = 0; Index < AngleIndices.Num() && Index < BeforePixelsByAngle.Num(); ++Index)
		{
			const FGuid AngleGuid = AngleGuids.IsValidIndex(Index) ? AngleGuids[Index] : FGuid();
			if (PixelRects.IsValidIndex(Index) && PixelRects[Index].Width() > 0 && PixelRects[Index].Height() > 0)
			{
				Tool->ApplyRenderTargetPixelsInRectByGuid(AngleGuid, AngleIndices[Index], PixelRects[Index], BeforePixelsByAngle[Index]);
				continue;
			}
			if (!AngleGuid.IsValid() || !Tool->ApplyRenderTargetPixelsByGuid(AngleGuid, BeforePixelsByAngle[Index]))
			{
				if (!AngleGuid.IsValid())
				{
					Tool->ApplyRenderTargetPixels(AngleIndices[Index], BeforePixelsByAngle[Index]);
				}
			}
		}
	}
}

void FQuickSDFTextureSlotChange::Apply(UObject* Object)
{
	if (UQuickSDFPaintTool* Tool = Cast<UQuickSDFPaintTool>(Object))
	{
		Tool->ApplyTextureSlotChange(AngleGuid, AngleIndex, AfterTexture, AfterPixels);
	}
}

void FQuickSDFTextureSlotChange::Revert(UObject* Object)
{
	if (UQuickSDFPaintTool* Tool = Cast<UQuickSDFPaintTool>(Object))
	{
		Tool->ApplyTextureSlotChange(AngleGuid, AngleIndex, BeforeTexture, BeforePixels);
	}
}

void FQuickSDFMaskStateChange::Apply(UObject* Object)
{
	if (UQuickSDFPaintTool* Tool = Cast<UQuickSDFPaintTool>(Object))
	{
		RestoreMaskStateOnNextTick(Tool, AfterGuids, AfterAngles, AfterTextures, AfterPixelsByMask);
	}
}

void FQuickSDFMaskStateChange::Revert(UObject* Object)
{
	if (UQuickSDFPaintTool* Tool = Cast<UQuickSDFPaintTool>(Object))
	{
		RestoreMaskStateOnNextTick(Tool, BeforeGuids, BeforeAngles, BeforeTextures, BeforePixelsByMask);
	}
}
}

#undef LOCTEXT_NAMESPACE
