#pragma once
#include "CoreMinimal.h"
#include "QuickSDFToolTypes.h"

struct FMaskData
{
	TArray<double> SDF;
	float TargetT;
	bool bIsOpposite;
};

enum class EQuickSDFLilToonLeftChannelSource : uint8
{
	InternalY,
	InternalW,
	MirroredX
};

class FSDFProcessor
{
public:
	static TArray<uint8> ConvertToGrayscale(const TArray<FColor>& Src);
	static TArray<uint8> UpscaleImage(const TArray<uint8>& Src, int32 SrcW, int32 SrcH, int32 Upscale);
	static TArray<double> GenerateSDF(const TArray<uint8>& BinaryImg, int32 W, int32 H);
	static void CombineSDFs(const TArray<FMaskData>& Masks, TArray<FVector4f>& OutCombined, int32 W, int32 H, ESDFOutputFormat Format, bool bSymmetry);
	static TArray<FFloat16Color> DownscaleAndConvert(const TArray<FVector4f>& CombinedField, int32 HighW, int32 HighH, int32 Factor);
	static TArray<FFloat16Color> DownscaleAndConvertToLilToon(const TArray<FVector4f>& CombinedField, int32 HighW, int32 HighH, int32 Factor, EQuickSDFLilToonLeftChannelSource LeftChannelSource);

private:
	static void Compute1DDT(const double* f, double* d, int32 n, TArray<int32>& v, TArray<double>& z);
	static void Compute2DDT(TArray<double>& grid, int32 width, int32 height);
};
