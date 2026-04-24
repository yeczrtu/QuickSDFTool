#pragma once
#include "CoreMinimal.h"
#include "EditorSubsystem.h"
#include "QuickSDFToolTypes.h"
#include "QuickSDFToolSubsystem.generated.h"

UCLASS()
class UQuickSDFToolSubsystem : public UEditorSubsystem
{
	GENERATED_BODY()

public:
	bool CaptureRenderTargetPixels(class UTextureRenderTarget2D* RenderTarget, TArray<FColor>& OutPixels) const;
	bool RestoreRenderTargetPixels(class UTextureRenderTarget2D* RenderTarget, const TArray<FColor>& Pixels) const;
	
	void StampSamplesToRenderTarget(class UTextureRenderTarget2D* RT, class UTexture2D* BrushMask, const TArray<FQuickSDFStrokeSample>& Samples, float BrushPixelSize, bool bIsShadow);
	
	void ExportToTexture(class UTextureRenderTarget2D* RT, const FString& FolderPath, const FString& AssetName);
	void Create16BitTexture(const TArray<uint16>& Pixels, int32 Width, int32 Height, const FString& FolderPath, const FString& TextureName);
};