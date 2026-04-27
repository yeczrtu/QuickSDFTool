#include "QuickSDFToolSubsystem.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/Texture2D.h"
#include "Engine/Canvas.h"
#include "CanvasItem.h"
#include "Kismet/KismetRenderingLibrary.h"
#include "AssetToolsModule.h"
#include "IAssetTools.h"
#include "TextureResource.h"
#include "Editor.h"

void UQuickSDFToolSubsystem::SetTargetComponent(UMeshComponent* NewComponent)
{
	CurrentTargetComponent = NewComponent;
}

UMeshComponent* UQuickSDFToolSubsystem::GetTargetMeshComponent() const
{
	if (!CurrentTargetComponent.IsValid()) return nullptr;
	return CurrentTargetComponent.Get();
}

bool UQuickSDFToolSubsystem::CaptureRenderTargetPixels(class UTextureRenderTarget2D* RenderTarget,
                                                       TArray<FColor>& OutPixels) const
{
	if (!RenderTarget) return false;
	FTextureRenderTargetResource* RTResource = RenderTarget->GameThread_GetRenderTargetResource();
	if (!RTResource) return false;
	FReadSurfaceDataFlags ReadFlags(RCM_UNorm);
	ReadFlags.SetLinearToGamma(false);
	return RTResource->ReadPixels(OutPixels, ReadFlags);
}

bool UQuickSDFToolSubsystem::RestoreRenderTargetPixels(class UTextureRenderTarget2D* RenderTarget,
	const TArray<FColor>& Pixels) const
{
	if (!RenderTarget || Pixels.Num() != RenderTarget->SizeX * RenderTarget->SizeY) return false;
	UTexture2D* TempTexture = UTexture2D::CreateTransient(RenderTarget->SizeX, RenderTarget->SizeY, PF_B8G8R8A8);
	if (!TempTexture || !TempTexture->GetPlatformData() || TempTexture->GetPlatformData()->Mips.Num() == 0) return false;
	TempTexture->MipGenSettings = TMGS_NoMipmaps;
	TempTexture->Filter = TF_Nearest;
	TempTexture->SRGB = false;
	FTexture2DMipMap& Mip = TempTexture->GetPlatformData()->Mips[0];
	void* Data = Mip.BulkData.Lock(LOCK_READ_WRITE);
	FMemory::Memcpy(Data, Pixels.GetData(), Pixels.Num() * sizeof(FColor));
	Mip.BulkData.Unlock();
	TempTexture->UpdateResource();
	FTextureRenderTargetResource* RTResource = RenderTarget->GameThread_GetRenderTargetResource();
	if (!RTResource) return false;
	FCanvas Canvas(RTResource, nullptr, GEditor->GetEditorWorldContext().World(), GMaxRHIFeatureLevel);
	FCanvasTileItem TileItem(FVector2D::ZeroVector, TempTexture->GetResource(), FVector2D(RenderTarget->SizeX, RenderTarget->SizeY), FLinearColor::White);
	TileItem.BlendMode = SE_BLEND_Opaque;
	Canvas.DrawItem(TileItem);
	Canvas.Flush_GameThread(true);
	ENQUEUE_RENDER_COMMAND(RestoreQuickSDFPaintRTCommand)(
		[RTResource](FRHICommandListImmediate& RHICmdList) {
			TransitionAndCopyTexture(RHICmdList, RTResource->GetRenderTargetTexture(), RTResource->TextureRHI, {});
		});
	return true;
}

void UQuickSDFToolSubsystem::StampSamplesToRenderTarget(class UTextureRenderTarget2D* RT, class UTexture2D* BrushMask,
	const TArray<FQuickSDFStrokeSample>& Samples, float BrushPixelSize, bool bIsShadow)
{/*
	if (!RT || !BrushMask || Samples.Num() == 0) return;

	FTextureRenderTargetResource* RTResource = RT->GameThread_GetRenderTargetResource();
	if (!RTResource) return;

	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	FCanvas Canvas(RTResource, nullptr, World, GMaxRHIFeatureLevel);

	const FVector2D RTSize(RT->SizeX, RT->SizeY);
	const FLinearColor PaintColor = bIsShadow ? FLinearColor::Black : FLinearColor::White;

	for (const FQuickSDFStrokeSample& Sample : Samples)
	{
		const FVector2D PixelPos(Sample.UV.X * RTSize.X, Sample.UV.Y * RTSize.Y);
		const FVector2D StampPos = PixelPos - (BrushPixelSize * 0.5f);
		
		FCanvasTileItem BrushItem(StampPos, BrushMask->GetResource(), BrushPixelSize, PaintColor);
		BrushItem.BlendMode = SE_BLEND_Translucent;
		Canvas.DrawItem(BrushItem);
	}

	Canvas.Flush_GameThread(false);*/
}

void UQuickSDFToolSubsystem::ExportToTexture(class UTextureRenderTarget2D* RT, const FString& FolderPath, const FString& AssetName)
{
	//TODO
	/*UQuickSDFPaintTool* Tool = Cast<UQuickSDFPaintTool>(GetOuter());
	if (!Tool) return;
	
	IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();

	for (int32 i = 0; i < TransientRenderTargets.Num(); ++i)
	{
		UTextureRenderTarget2D* RT = TransientRenderTargets[i];
		if (!RT) continue;

		TArray<FColor> Pixels;
		if (!Tool->CaptureRenderTargetPixels(RT, Pixels)) continue;

		FString FolderPath = TEXT("/Game/QuickSDF_Exports"); // TODO:ファイル名をきめる処理
		FString AssetName = FString::Printf(TEXT("T_QuickSDF_Angle%d"), i);
		
		UTexture2D* NewTex = Cast<UTexture2D>(AssetTools.CreateAsset(AssetName, FolderPath, UTexture2D::StaticClass(), nullptr));
        
		if (NewTex)
		{
			NewTex->Source.Init(RT->SizeX, RT->SizeY, 1, 1, TSF_BGRA8);
			void* MipData = NewTex->Source.LockMip(0);
			FMemory::Memcpy(MipData, Pixels.GetData(), Pixels.Num() * sizeof(FColor));
			NewTex->Source.UnlockMip(0);

			NewTex->SRGB = RT->SRGB;
			NewTex->CompressionSettings = TC_Default;
			NewTex->MipGenSettings = TMGS_NoMipmaps;
			NewTex->PostEditChange();
			NewTex->GetPackage()->MarkPackageDirty();

			TArray<UObject*> AssetsToSync;
			AssetsToSync.Add(NewTex);
			AssetTools.SyncBrowserToAssets(AssetsToSync);
		}
	}*/
}

void UQuickSDFToolSubsystem::Create16BitTexture(const TArray<uint16>& Pixels, int32 Width, int32 Height,
	const FString& FolderPath, const FString& TextureName)
{
	IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();
	//TODO:ファイル名を決められるようにする。
	UTexture2D* NewTex = Cast<UTexture2D>(AssetTools.CreateAsset(TextureName, FolderPath, UTexture2D::StaticClass(), nullptr));

	if (!NewTex) return;

	NewTex->Source.Init(Width, Height, 1, 1, TSF_G16);
	uint16* MipData = (uint16*)NewTex->Source.LockMip(0);
	FMemory::Memcpy(MipData, Pixels.GetData(), Pixels.Num() * sizeof(uint16));
	NewTex->Source.UnlockMip(0);

	NewTex->CompressionSettings = TC_Grayscale;
	NewTex->SRGB = false;
	NewTex->MipGenSettings = TMGS_FromTextureGroup;
	NewTex->Filter = TF_Default;
	NewTex->AddressX = TA_Clamp;
	NewTex->AddressY = TA_Clamp;
	
	NewTex->PostEditChange();
	NewTex->GetPackage()->MarkPackageDirty();

	TArray<UObject*> Assets;
	Assets.Add(NewTex);
	AssetTools.SyncBrowserToAssets(Assets);
}

void UQuickSDFToolSubsystem::DrawTextureToRenderTarget(UTexture2D* SourceTex, UTextureRenderTarget2D* TargetRT)
{
	if (!SourceTex || !TargetRT) return;
	FTextureRenderTargetResource* RTResource = TargetRT->GameThread_GetRenderTargetResource();
	if (!RTResource) return;

	FCanvas Canvas(RTResource, nullptr, GEditor->GetEditorWorldContext().World(), GMaxRHIFeatureLevel);
	Canvas.Clear(FLinearColor::White); // 背景を白でクリア
	
	FCanvasTileItem TileItem(FVector2D::ZeroVector, SourceTex->GetResource(), FVector2D(TargetRT->SizeX, TargetRT->SizeY), FLinearColor::White);
	TileItem.BlendMode = SE_BLEND_Opaque;
	Canvas.DrawItem(TileItem);
	Canvas.Flush_GameThread(true);

	ENQUEUE_RENDER_COMMAND(UpdateQuickSDFRTCommand)(
		[RTResource](FRHICommandListImmediate& RHICmdList) {
			TransitionAndCopyTexture(RHICmdList, RTResource->GetRenderTargetTexture(), RTResource->TextureRHI, {});
		});
}

void UQuickSDFToolSubsystem::ClearRenderTarget(UTextureRenderTarget2D* TargetRT, FLinearColor ClearColor)
{
	if (!TargetRT) return;
	FTextureRenderTargetResource* RTResource = TargetRT->GameThread_GetRenderTargetResource();
	if (!RTResource) return;

	FCanvas Canvas(RTResource, nullptr, GEditor->GetEditorWorldContext().World(), GMaxRHIFeatureLevel);
	Canvas.Clear(ClearColor);
	Canvas.Flush_GameThread(true);

	ENQUEUE_RENDER_COMMAND(ClearQuickSDFRTCommand)(
		[RTResource](FRHICommandListImmediate& RHICmdList) {
			TransitionAndCopyTexture(RHICmdList, RTResource->GetRenderTargetTexture(), RTResource->TextureRHI, {});
		});
}