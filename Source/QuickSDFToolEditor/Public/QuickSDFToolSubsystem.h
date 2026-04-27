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
	UPROPERTY()
	TWeakObjectPtr<UMeshComponent> CurrentTargetComponent;

	// 現在生成されている作業用RT
	UPROPERTY()
	TArray<TObjectPtr<UTextureRenderTarget2D>> TransientRenderTargets;

	// --- ロジック (Logic) ---
	void SetTargetComponent(UMeshComponent* NewComponent);
	UMeshComponent* GetTargetMeshComponent() const;
	
	
	bool CaptureRenderTargetPixels(class UTextureRenderTarget2D* RenderTarget, TArray<FColor>& OutPixels) const;
	bool RestoreRenderTargetPixels(class UTextureRenderTarget2D* RenderTarget, const TArray<FColor>& Pixels) const;
	
	void StampSamplesToRenderTarget(class UTextureRenderTarget2D* RT, class UTexture2D* BrushMask, const TArray<FQuickSDFStrokeSample>& Samples, float BrushPixelSize, bool bIsShadow);
	
	void ExportToTexture(class UTextureRenderTarget2D* RT, const FString& FolderPath, const FString& AssetName);
	void Create16BitTexture(const TArray<uint16>& Pixels, int32 Width, int32 Height, const FString& FolderPath, const FString& TextureName);
	
	UPROPERTY()
	TObjectPtr<class UQuickSDFAsset> ActiveSDFAsset;

	void SetActiveSDFAsset(class UQuickSDFAsset* InAsset) { ActiveSDFAsset = InAsset; }
	class UQuickSDFAsset* GetActiveSDFAsset() const { return ActiveSDFAsset; }
	
	void DrawTextureToRenderTarget(class UTexture2D* SourceTex, class UTextureRenderTarget2D* TargetRT);
	
	void ClearRenderTarget(class UTextureRenderTarget2D* TargetRT, FLinearColor ClearColor = FLinearColor::White);
};