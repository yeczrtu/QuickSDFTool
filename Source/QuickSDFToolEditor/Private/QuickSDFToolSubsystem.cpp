#include "QuickSDFToolSubsystem.h"
#include "QuickSDFAsset.h"
#include "QuickSDFPaintTool.h" 
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/Texture2D.h"
#include "Engine/Canvas.h"
#include "CanvasItem.h"
#include "Kismet/KismetRenderingLibrary.h"
#include "AssetToolsModule.h"
#include "IAssetTools.h"
#include "TextureResource.h"
#include "Editor.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "ObjectTools.h"
#include "RenderingThread.h"

#define LOCTEXT_NAMESPACE "QuickSDFToolSubsystem"

namespace
{
constexpr int32 QuickSDFSubsystemDefaultAngleCount = 8;

bool IsEngineContentPath(const FString& AssetPath)
{
	return AssetPath.Equals(TEXT("/Engine"), ESearchCase::IgnoreCase) ||
		AssetPath.StartsWith(TEXT("/Engine/"), ESearchCase::IgnoreCase);
}

bool ValidateTextureAssetPath(const FString& FolderPath, const FString& TextureName, FText* OutError)
{
	FString CleanFolder = FolderPath;
	while (CleanFolder.EndsWith(TEXT("/")))
	{
		CleanFolder.LeftChopInline(1);
	}

	if (!FPackageName::IsValidLongPackageName(CleanFolder))
	{
		if (OutError)
		{
			*OutError = FText::Format(LOCTEXT("InvalidOutputFolder", "Invalid output folder: {0}\nUse a content path such as /Game/QuickSDF_GENERATED."), FText::FromString(CleanFolder));
		}
		return false;
	}

	if (IsEngineContentPath(CleanFolder))
	{
		if (OutError)
		{
			*OutError = FText::Format(LOCTEXT("EngineOutputFolderProtected", "Cannot write textures to engine content: {0}"), FText::FromString(CleanFolder));
		}
		return false;
	}

	if (TextureName.IsEmpty())
	{
		if (OutError)
		{
			*OutError = LOCTEXT("EmptyTextureName", "Texture name is empty.");
		}
		return false;
	}

	if (ObjectTools::SanitizeObjectName(TextureName) != TextureName)
	{
		if (OutError)
		{
			*OutError = FText::Format(LOCTEXT("InvalidTextureName", "Invalid texture name: {0}\nUse letters, numbers, and underscores."), FText::FromString(TextureName));
		}
		return false;
	}

	return true;
}

UTexture2D* FindOrCreateTextureAsset(const FString& FolderPath, const FString& TextureName, bool bOverwriteExisting, FText* OutError)
{
	FString CleanFolder = FolderPath;
	while (CleanFolder.EndsWith(TEXT("/")))
	{
		CleanFolder.LeftChopInline(1);
	}

	if (!ValidateTextureAssetPath(CleanFolder, TextureName, OutError))
	{
		return nullptr;
	}

	IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();
	FString FinalFolder = CleanFolder;
	FString FinalName = TextureName;

	if (!bOverwriteExisting)
	{
		const FString DesiredPackagePath = CleanFolder / TextureName;
		FString UniquePackageName;
		AssetTools.CreateUniqueAssetName(DesiredPackagePath, TEXT(""), UniquePackageName, FinalName);
		FinalFolder = FPackageName::GetLongPackagePath(UniquePackageName);
	}

	const FString PackagePath = FinalFolder / FinalName;
	if (UObject* ExistingObject = StaticLoadObject(UObject::StaticClass(), nullptr, *PackagePath))
	{
		UTexture2D* ExistingTexture = Cast<UTexture2D>(ExistingObject);
		if (!ExistingTexture)
		{
			if (OutError)
			{
				*OutError = FText::Format(LOCTEXT("ExistingAssetNotTexture", "Cannot overwrite {0}; an asset with that name exists and is not a Texture2D."), FText::FromString(PackagePath));
			}
			return nullptr;
		}

		ExistingTexture->Modify();
		return ExistingTexture;
	}

	UTexture2D* NewTexture = Cast<UTexture2D>(AssetTools.CreateAsset(FinalName, FinalFolder, UTexture2D::StaticClass(), nullptr));
	if (!NewTexture && OutError)
	{
		*OutError = FText::Format(LOCTEXT("CreateTextureFailed", "Failed to create texture asset: {0}"), FText::FromString(PackagePath));
	}
	return NewTexture;
}

void FinalizeTextureAsset(UTexture2D* Texture)
{
	if (!Texture)
	{
		return;
	}

	Texture->PostEditChange();
	Texture->UpdateResource();
	FlushRenderingCommands();
	Texture->GetPackage()->MarkPackageDirty();

	IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();
	TArray<UObject*> Assets;
	Assets.Add(Texture);
	AssetTools.SyncBrowserToAssets(Assets);
}

UQuickSDFAsset* CreateDefaultQuickSDFAsset(UObject* Outer)
{
	UQuickSDFAsset* NewAsset = NewObject<UQuickSDFAsset>(Outer);
	if (!NewAsset)
	{
		return nullptr;
	}

	NewAsset->SetFlags(RF_Transactional);
	NewAsset->Resolution = FIntPoint(1024, 1024);
	NewAsset->UVChannel = 0;
	NewAsset->AngleDataList.SetNum(QuickSDFSubsystemDefaultAngleCount);

	const float MaxAngle = 90.0f;
	for (int32 Index = 0; Index < QuickSDFSubsystemDefaultAngleCount; ++Index)
	{
		NewAsset->AngleDataList[Index].Angle = QuickSDFSubsystemDefaultAngleCount > 1
			? (static_cast<float>(Index) / static_cast<float>(QuickSDFSubsystemDefaultAngleCount - 1)) * MaxAngle
			: 0.0f;
		NewAsset->AngleDataList[Index].MaskGuid = FGuid::NewGuid();
	}

	return NewAsset;
}
}

void UQuickSDFToolSubsystem::SetTargetComponent(UMeshComponent* NewComponent)
{
	CurrentTargetComponent = NewComponent;
	if (NewComponent)
	{
		ActiveSDFAsset = GetOrCreateSDFAssetForComponent(NewComponent);
	}
}

UMeshComponent* UQuickSDFToolSubsystem::GetTargetMeshComponent() const
{
	if (!CurrentTargetComponent.IsValid()) return nullptr;
	return CurrentTargetComponent.Get();
}

UQuickSDFAsset* UQuickSDFToolSubsystem::GetOrCreateSDFAssetForComponent(UMeshComponent* Component)
{
	if (!Component)
	{
		return nullptr;
	}

	if (TObjectPtr<UQuickSDFAsset>* ExistingAsset = ComponentSDFAssets.Find(Component))
	{
		if (ExistingAsset->Get())
		{
			return ExistingAsset->Get();
		}
	}

	UQuickSDFAsset* NewAsset = CreateDefaultQuickSDFAsset(this);
	ComponentSDFAssets.Add(Component, NewAsset);
	return NewAsset;
}

void UQuickSDFToolSubsystem::SetActiveSDFAsset(UQuickSDFAsset* InAsset)
{
	ActiveSDFAsset = InAsset;
	if (InAsset)
	{
		InAsset->SetFlags(RF_Transactional);
	}
	if (CurrentTargetComponent.IsValid() && InAsset)
	{
		ComponentSDFAssets.Add(CurrentTargetComponent.Get(), InAsset);
	}
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
	FlushRenderingCommands();
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
	FlushRenderingCommands();
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

UTexture2D* UQuickSDFToolSubsystem::CreateMaskTexture(UTextureRenderTarget2D* RT, const FString& FolderPath, const FString& TextureName, bool bOverwriteExisting, FText* OutError)
{
	if (!RT)
	{
		if (OutError)
		{
			*OutError = LOCTEXT("MaskRenderTargetMissing", "Cannot export mask because the render target is missing.");
		}
		return nullptr;
	}

	TArray<FColor> Pixels;
	if (!CaptureRenderTargetPixels(RT, Pixels))
	{
		if (OutError)
		{
			*OutError = LOCTEXT("MaskCaptureFailed", "Failed to read pixels from the mask render target.");
		}
		return nullptr;
	}

	UTexture2D* Texture = FindOrCreateTextureAsset(FolderPath, TextureName, bOverwriteExisting, OutError);
	if (!Texture)
	{
		return nullptr;
	}

	Texture->Source.Init(RT->SizeX, RT->SizeY, 1, 1, TSF_BGRA8);
	void* MipData = Texture->Source.LockMip(0);
	FMemory::Memcpy(MipData, Pixels.GetData(), Pixels.Num() * sizeof(FColor));
	Texture->Source.UnlockMip(0);

	Texture->SRGB = RT->SRGB;
	Texture->CompressionSettings = TC_Default;
	Texture->MipGenSettings = TMGS_NoMipmaps;
	Texture->Filter = TF_Nearest;

	FinalizeTextureAsset(Texture);
	return Texture;
}

bool UQuickSDFToolSubsystem::OverwriteTextureWithRenderTarget(UTexture2D* Texture, UTextureRenderTarget2D* RT, FText* OutError)
{
	if (!Texture)
	{
		if (OutError)
		{
			*OutError = LOCTEXT("OverwriteTextureMissing", "Cannot overwrite source texture because the Texture2D is missing.");
		}
		return false;
	}
	if (IsEngineContentPath(Texture->GetPathName()))
	{
		if (OutError)
		{
			*OutError = FText::Format(LOCTEXT("OverwriteEngineTextureProtected", "Cannot overwrite {0}. Engine Texture2D assets are protected."), FText::FromString(Texture->GetPathName()));
		}
		return false;
	}
	if (!RT)
	{
		if (OutError)
		{
			*OutError = LOCTEXT("OverwriteRenderTargetMissing", "Cannot overwrite source texture because the mask render target is missing.");
		}
		return false;
	}

	TArray<FColor> Pixels;
	if (!CaptureRenderTargetPixels(RT, Pixels))
	{
		if (OutError)
		{
			*OutError = LOCTEXT("OverwriteCaptureFailed", "Failed to read pixels from the mask render target.");
		}
		return false;
	}

	Texture->Modify();
	Texture->Source.Init(RT->SizeX, RT->SizeY, 1, 1, TSF_BGRA8);
	void* MipData = Texture->Source.LockMip(0);
	FMemory::Memcpy(MipData, Pixels.GetData(), Pixels.Num() * sizeof(FColor));
	Texture->Source.UnlockMip(0);

	Texture->SRGB = RT->SRGB;
	Texture->CompressionSettings = TC_Default;
	Texture->MipGenSettings = TMGS_NoMipmaps;
	Texture->Filter = TF_Nearest;

	FinalizeTextureAsset(Texture);
	return true;
}

UTexture2D* UQuickSDFToolSubsystem::CreateSDFTexture(const TArray<FFloat16Color>& Pixels, int32 Width, int32 Height, const FString& FolderPath, const FString& TextureName, ESDFOutputFormat Format, bool bOverwriteExisting, FText* OutError)
{
	if (Pixels.Num() != Width * Height)
	{
		if (OutError)
		{
			*OutError = LOCTEXT("InvalidSDFPixelCount", "Cannot save SDF texture because the pixel count does not match the output resolution.");
		}
		return nullptr;
	}

	UTexture2D* NewTex = FindOrCreateTextureAsset(FolderPath, TextureName, bOverwriteExisting, OutError);

	if (!NewTex) return nullptr;

	// モノポーラ設定時は R チャンネルの値を抽出し 16bit グレースケール (G16) で作成
	if (Format == ESDFOutputFormat::Monopolar)
	{
		NewTex->Source.Init(Width, Height, 1, 1, TSF_G16);
		uint16* MipData = (uint16*)NewTex->Source.LockMip(0);
		for(int32 i = 0; i < Pixels.Num(); ++i)
		{
			MipData[i] = (uint16)(FMath::Clamp(Pixels[i].R.GetFloat(), 0.0f, 1.0f) * 65535.0f);
		}
		NewTex->Source.UnlockMip(0);
		NewTex->CompressionSettings = TC_Grayscale;
	}
	else // バイポーラ時はRGBA16F (HDR)
	{
		NewTex->Source.Init(Width, Height, 1, 1, TSF_RGBA16F);
		FFloat16Color* MipData = (FFloat16Color*)NewTex->Source.LockMip(0);
		FMemory::Memcpy(MipData, Pixels.GetData(), Pixels.Num() * sizeof(FFloat16Color));
		NewTex->Source.UnlockMip(0);
		NewTex->CompressionSettings = TC_HDR;
	}

	NewTex->SRGB = false;
	NewTex->MipGenSettings = TMGS_NoMipmaps;
	NewTex->Filter = TF_Bilinear;

	FinalizeTextureAsset(NewTex);
	return NewTex;
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
	FlushRenderingCommands();
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
	FlushRenderingCommands();
}

#undef LOCTEXT_NAMESPACE
