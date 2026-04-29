#pragma once
#include "CoreMinimal.h"
#include "EditorSubsystem.h"
#include "QuickSDFToolTypes.h"
#include "QuickSDFToolSubsystem.generated.h"

class UMeshComponent;
class UQuickSDFAsset;

UCLASS()
class UQuickSDFToolSubsystem : public UEditorSubsystem
{
	GENERATED_BODY()

public:
	UPROPERTY()
	TWeakObjectPtr<UMeshComponent> CurrentTargetComponent;

	// 現在生成されている作業用RT
	UPROPERTY()
	TArray<TObjectPtr<UTextureRenderTarget2D>> TransientRenderTargets;

	// --- ロジック (Logic) ---
	void SetTargetComponent(UMeshComponent* NewComponent);
	UMeshComponent* GetTargetMeshComponent() const;
	class UQuickSDFAsset* GetOrCreateSDFAssetForComponent(UMeshComponent* Component);
	
	
	bool CaptureRenderTargetPixels(class UTextureRenderTarget2D* RenderTarget, TArray<FColor>& OutPixels) const;
	bool RestoreRenderTargetPixels(class UTextureRenderTarget2D* RenderTarget, const TArray<FColor>& Pixels) const;
	
	void StampSamplesToRenderTarget(class UTextureRenderTarget2D* RT, class UTexture2D* BrushMask, const TArray<FQuickSDFStrokeSample>& Samples, float BrushPixelSize, bool bIsShadow);
	
	class UTexture2D* CreateMaskTexture(class UTextureRenderTarget2D* RT, const FString& FolderPath, const FString& TextureName, bool bOverwriteExisting, FText* OutError = nullptr);
	class UTexture2D* CreateSDFTexture(const TArray<FFloat16Color>& Pixels, int32 Width, int32 Height, const FString& FolderPath, const FString& TextureName, ESDFOutputFormat Format, bool bOverwriteExisting, FText* OutError = nullptr);
	class UTexture2D* ImportMaskFileAsTexture(const FString& SourceFilename, const FString& FolderPath, bool bOverwriteExisting, FText* OutError = nullptr);
	bool ImportMaskFilesAsTextures(const TArray<FString>& SourceFilenames, const FString& FolderPath, TArray<class UTexture2D*>& OutTextures, FText* OutError = nullptr);
	
	UPROPERTY()
	TObjectPtr<class UQuickSDFAsset> ActiveSDFAsset;

	UPROPERTY()
	TMap<TObjectPtr<UMeshComponent>, TObjectPtr<UQuickSDFAsset>> ComponentSDFAssets;

	void SetActiveSDFAsset(class UQuickSDFAsset* InAsset);
	class UQuickSDFAsset* GetActiveSDFAsset() const { return ActiveSDFAsset; }
	
	void DrawTextureToRenderTarget(class UTexture2D* SourceTex, class UTextureRenderTarget2D* TargetRT);
	
	void ClearRenderTarget(class UTextureRenderTarget2D* TargetRT, FLinearColor ClearColor = FLinearColor::White);
};
