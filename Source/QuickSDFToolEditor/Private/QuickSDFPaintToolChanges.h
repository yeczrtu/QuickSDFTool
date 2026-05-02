#pragma once

#include "CoreMinimal.h"
#include "InteractiveToolChange.h"

class UTexture2D;

namespace QuickSDFPaintToolPrivate
{
class FQuickSDFRenderTargetChange : public FToolCommandChange
{
public:
	int32 AngleIndex = INDEX_NONE;
	FGuid AngleGuid;
	TArray<FColor> BeforePixels;
	TArray<FColor> AfterPixels;

	virtual void Apply(UObject* Object) override;
	virtual void Revert(UObject* Object) override;
	virtual FString ToString() const override { return TEXT("FQuickSDFRenderTargetChange"); }
};

class FQuickSDFRenderTargetsChange : public FToolCommandChange
{
public:
	TArray<int32> AngleIndices;
	TArray<FGuid> AngleGuids;
	TArray<FIntRect> PixelRects;
	TArray<TArray<FColor>> BeforePixelsByAngle;
	TArray<TArray<FColor>> AfterPixelsByAngle;

	virtual void Apply(UObject* Object) override;
	virtual void Revert(UObject* Object) override;
	virtual FString ToString() const override { return TEXT("FQuickSDFRenderTargetsChange"); }
};

class FQuickSDFTextureSlotChange : public FToolCommandChange
{
public:
	int32 AngleIndex = INDEX_NONE;
	FGuid AngleGuid;
	UTexture2D* BeforeTexture = nullptr;
	UTexture2D* AfterTexture = nullptr;
	bool bBeforeAllowSourceTextureOverwrite = false;
	bool bAfterAllowSourceTextureOverwrite = false;
	TArray<FColor> BeforePixels;
	TArray<FColor> AfterPixels;

	virtual void Apply(UObject* Object) override;
	virtual void Revert(UObject* Object) override;
	virtual FString ToString() const override { return TEXT("FQuickSDFTextureSlotChange"); }
};

class FQuickSDFMaskStateChange : public FToolCommandChange
{
public:
	TArray<FGuid> BeforeGuids;
	TArray<float> BeforeAngles;
	TArray<UTexture2D*> BeforeTextures;
	TArray<bool> BeforeAllowSourceTextureOverwrites;
	TArray<TArray<FColor>> BeforePixelsByMask;
	TArray<FGuid> AfterGuids;
	TArray<float> AfterAngles;
	TArray<UTexture2D*> AfterTextures;
	TArray<bool> AfterAllowSourceTextureOverwrites;
	TArray<TArray<FColor>> AfterPixelsByMask;

	virtual void Apply(UObject* Object) override;
	virtual void Revert(UObject* Object) override;
	virtual FString ToString() const override { return TEXT("FQuickSDFMaskStateChange"); }
};
}
