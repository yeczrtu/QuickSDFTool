#pragma once

#include "CoreMinimal.h"
#include "InteractiveToolChange.h"
#include "QuickSDFAsset.h"
#include "QuickSDFPaintTool.h"
#include "QuickSDFToolProperties.h"
#include "SDFProcessor.h"

namespace QuickSDFPaintToolPrivate
{
inline constexpr double QuickSDFStrokeSpacingFactor = 0.12;
inline constexpr int32 QuickSDFBrushMaskResolution = 128;
inline constexpr int32 QuickSDFPreviewActionIncreaseBrush = static_cast<int32>(EStandardToolActions::BaseClientDefinedActionID) + 1;
inline constexpr int32 QuickSDFActionOpenToggleMenu = static_cast<int32>(EStandardToolActions::BaseClientDefinedActionID) + 20;
inline constexpr int32 QuickSDFActionToggleBase = static_cast<int32>(EStandardToolActions::BaseClientDefinedActionID) + 30;
inline constexpr float QuickSDFMinResizeSensitivity = 0.01f;
inline constexpr int32 QuickSDFBipolarDetectionStride = 500;
inline constexpr int32 QuickSDFDefaultAngleCount = 8;
inline constexpr int32 QuickSDFUVOverlaySupersample = 4;

struct FQuickSDFUVEdgeKey
{
	FIntPoint A;
	FIntPoint B;

	bool operator==(const FQuickSDFUVEdgeKey& Other) const
	{
		return A == Other.A && B == Other.B;
	}
};

struct FQuickSDFUVOverlayEdge
{
	FVector2f A;
	FVector2f B;
};

inline uint32 GetTypeHash(const FQuickSDFUVEdgeKey& Key)
{
	return HashCombine(
		HashCombine(::GetTypeHash(Key.A.X), ::GetTypeHash(Key.A.Y)),
		HashCombine(::GetTypeHash(Key.B.X), ::GetTypeHash(Key.B.Y)));
}

inline FIntPoint QuantizeUVForOverlay(const FVector2f& UV)
{
	constexpr float QuantizeScale = 8192.0f;
	return FIntPoint(
		FMath::RoundToInt(UV.X * QuantizeScale),
		FMath::RoundToInt(UV.Y * QuantizeScale));
}

inline FQuickSDFUVEdgeKey MakeUVEdgeKey(const FVector2f& A, const FVector2f& B)
{
	FIntPoint QA = QuantizeUVForOverlay(A);
	FIntPoint QB = QuantizeUVForOverlay(B);
	if (QB.X < QA.X || (QB.X == QA.X && QB.Y < QA.Y))
	{
		Swap(QA, QB);
	}
	return { QA, QB };
}

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
	TArray<TArray<FColor>> BeforePixelsByMask;
	TArray<FGuid> AfterGuids;
	TArray<float> AfterAngles;
	TArray<UTexture2D*> AfterTextures;
	TArray<TArray<FColor>> AfterPixelsByMask;

	virtual void Apply(UObject* Object) override;
	virtual void Revert(UObject* Object) override;
	virtual FString ToString() const override { return TEXT("FQuickSDFMaskStateChange"); }
};

int32 GetQuickSDFPresetSize(EQuickSDFQualityPreset Preset);
bool ShouldProcessMaskAngle(float RawAngle, bool bSymmetryMode);
TArray<int32> CollectProcessableMaskIndices(const UQuickSDFAsset& Asset, bool bSymmetryMode);
bool TryBuildMaskData(
	const UQuickSDFPaintTool& Tool,
	UTextureRenderTarget2D* RenderTarget,
	float RawAngle,
	float MaxAngle,
	int32 OrigW,
	int32 OrigH,
	int32 Upscale,
	FMaskData& OutData);
void SortMaskData(TArray<FMaskData>& MaskData);
bool NeedsBipolarOutput(const TArray<FMaskData>& MaskData, int32 PixelCount);
bool TryExtractAngleFromName(const FString& Name, float& OutAngle);
TArray<UTexture2D*> CollectSelectedTextureAssets();
bool HasImportedSourceMasks(const UQuickSDFAsset* Asset);
bool HasNonWhitePaintMasks(const UQuickSDFPaintTool& Tool, const UQuickSDFAsset* Asset);
bool IsPersistentQuickSDFAsset(const UQuickSDFAsset* Asset);
TArray<FColor> MakeSolidPixels(int32 Width, int32 Height, const FLinearColor& FillColor);
bool EnsureMaskGuids(UQuickSDFAsset* Asset);
int32 FindAngleIndexByGuid(const UQuickSDFAsset* Asset, const FGuid& MaskGuid);
void CaptureMaskState(
	UQuickSDFPaintTool& Tool,
	UQuickSDFAsset* Asset,
	TArray<FGuid>& OutGuids,
	TArray<float>& OutAngles,
	TArray<UTexture2D*>& OutTextures,
	TArray<TArray<FColor>>& OutPixelsByMask);
void RestoreMaskStateOnNextTick(
	UQuickSDFPaintTool* Tool,
	const TArray<FGuid>& MaskGuids,
	const TArray<float>& Angles,
	const TArray<UTexture2D*>& Textures,
	const TArray<TArray<FColor>>& PixelsByMask);
}
