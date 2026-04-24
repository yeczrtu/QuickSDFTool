#include "SDFProcessor.h"

TArray<uint8> FSDFProcessor::ConvertToGrayscale(const TArray<FColor>& Src)
{
	TArray<uint8> Dst;
	Dst.SetNumUninitialized(Src.Num());
	for(int32 i = 0; i < Src.Num(); ++i)
	{
		Dst[i] = (uint8)(((int32)Src[i].R + (int32)Src[i].G + (int32)Src[i].B) / 3);
	}
	return Dst;
}

TArray<uint8> FSDFProcessor::UpscaleImage(const TArray<uint8>& Src, int32 SrcW, int32 SrcH, int32 Upscale)
{
	if (Upscale <= 1) return Src;

	int32 DstW = SrcW * Upscale;
	int32 DstH = SrcH * Upscale;
	TArray<uint8> Dst;
	Dst.SetNumUninitialized(DstW * DstH);

	for (int32 y = 0; y < DstH; ++y)
	{
		float srcY = (float)y / Upscale;
		int32 y0 = FMath::Clamp(FMath::FloorToInt(srcY), 0, SrcH - 1);
		int32 y1 = FMath::Clamp(y0 + 1, 0, SrcH - 1);
		float fy = srcY - y0;

		for (int32 x = 0; x < DstW; ++x)
		{
			float srcX = (float)x / Upscale;
			int32 x0 = FMath::Clamp(FMath::FloorToInt(srcX), 0, SrcW - 1);
			int32 x1 = FMath::Clamp(x0 + 1, 0, SrcW - 1);
			float fx = srcX - x0;

			float p00 = Src[y0 * SrcW + x0];
			float p10 = Src[y0 * SrcW + x1];
			float p01 = Src[y1 * SrcW + x0];
			float p11 = Src[y1 * SrcW + x1];

			float val = FMath::Lerp(
				FMath::Lerp(p00, p10, fx),
				FMath::Lerp(p01, p11, fx),
				fy);

			Dst[y * DstW + x] = (uint8)FMath::Clamp(FMath::RoundToInt(val), 0, 255);
		}
	}
	return Dst;
}

TArray<double> FSDFProcessor::GenerateSDF(const TArray<uint8>& BinaryImg, int32 W, int32 H)
{
	TArray<double> GridIn, GridOut;
	GridIn.SetNumUninitialized(W * H);
	GridOut.SetNumUninitialized(W * H);
	
	double maxDistSq = (double)(W * W + H * H + 100.0);

	bool bHasBlack = false;
	bool bHasWhite = false;

	for(int32 i = 0; i < W * H; ++i)
	{
		bool bIsWhite = BinaryImg[i] >= 127;
		if(bIsWhite) bHasWhite = true;
		else bHasBlack = true;

		GridIn[i]  = (!bIsWhite) ? 0.0 : maxDistSq;
		GridOut[i] = (bIsWhite)  ? 0.0 : maxDistSq;
	}

	Compute2DDT(GridIn, W, H);
	Compute2DDT(GridOut, W, H);

	TArray<double> SDF;
	SDF.SetNumUninitialized(W * H);
	for(int32 i = 0; i < W * H; ++i)
	{
		if (!bHasBlack) SDF[i] = maxDistSq;
		else if (!bHasWhite) SDF[i] = -maxDistSq;
		else SDF[i] = GridIn[i] - GridOut[i];
	}
	return SDF;
}

void FSDFProcessor::CombineSDFs(const TArray<FMaskData>& Masks, TArray<double>& OutCombined, int32 W, int32 H)
{
	OutCombined.SetNumZeroed(W * H);
	int32 NumMasks = Masks.Num();
	if(NumMasks == 0) return;

	double t_min = Masks[0].TargetT;
	double t_max = Masks.Last().TargetT;

	TArray<bool> Handled;
	Handled.SetNumZeroed(W * H);

	// 階調間の補間
	for (int32 i = 0; i < NumMasks - 1; ++i)
	{
		const FMaskData& M1 = Masks[i];
		const FMaskData& M2 = Masks[i + 1];
		for (int32 p = 0; p < W * H; ++p)
		{
			if (M1.SDF[p] > 0.0 && M2.SDF[p] <= 0.0)
			{
				double d1 = M1.SDF[p];
				double d2 = -M2.SDF[p];
				double ratio = d1 / (d1 + d2 + 1e-10);
				OutCombined[p] = M1.TargetT + (M2.TargetT - M1.TargetT) * ratio;
				Handled[p] = true;
			}
		}
	}

	for (int32 p = 0; p < W * H; ++p)
	{
		if (Masks[0].SDF[p] <= 0.0)
		{
			OutCombined[p] = t_min + Masks[0].SDF[p] * 0.05;
			Handled[p] = true;
		}
		else if (Masks.Last().SDF[p] > 0.0)
		{
			OutCombined[p] = t_max + Masks.Last().SDF[p] * 0.05;
			Handled[p] = true;
		}

		// ペイントの包含関係が崩れていてどの条件にも入らなかったピクセルのフォールバック処理
		if (!Handled[p])
		{
			double fallbackT = t_min;
			for (int32 i = NumMasks - 1; i >= 0; --i)
			{
				if (Masks[i].SDF[p] > 0.0)
				{
					fallbackT = Masks[i].TargetT;
					break;
				}
			}
			OutCombined[p] = fallbackT;
		}

		double val = OutCombined[p];
		double k = 0.08; // 丸め幅（この範囲内でグラデーションが滑らかにフェードアウトする）
    	
		double h0 = FMath::Max(k - FMath::Abs(val - 0.0), 0.0) / k;
		val = FMath::Max(val, 0.0) + h0 * h0 * k * 0.25;
    	
		double h1 = FMath::Max(k - FMath::Abs(val - 1.0), 0.0) / k;
		val = FMath::Min(val, 1.0) - h1 * h1 * k * 0.25;

		OutCombined[p] = val;
	}
}

TArray<uint16> FSDFProcessor::DownscaleAndConvert(const TArray<double>& CombinedField, int32 HighW, int32 HighH,
	int32 Factor)
{
	int32 OrigW = HighW / FMath::Max(1, Factor);
	int32 OrigH = HighH / FMath::Max(1, Factor);
	TArray<uint16> Out;
	Out.SetNumUninitialized(OrigW * OrigH);
	
	int32 Radius = FMath::Max(1, FMath::CeilToInt(Factor * 1.5f)); 

	for (int32 y = 0; y < OrigH; ++y)
	{
		for (int32 x = 0; x < OrigW; ++x)
		{
			double sum = 0.0;
			double weightSum = 0.0;
        	
			double cx = (x + 0.5) * Factor;
			double cy = (y + 0.5) * Factor;

			for(int32 dy = -Radius; dy <= Radius; ++dy)
			{
				int32 hy = FMath::Clamp(FMath::RoundToInt(cy) + dy, 0, HighH - 1);
				double distY = FMath::Abs(cy - (hy + 0.5));
				double wy = FMath::Max(0.0, 1.0 - (distY / (Radius + 0.5)));

				for(int32 dx = -Radius; dx <= Radius; ++dx)
				{
					int32 hx = FMath::Clamp(FMath::RoundToInt(cx) + dx, 0, HighW - 1);
					double distX = FMath::Abs(cx - (hx + 0.5));
					double wx = FMath::Max(0.0, 1.0 - (distX / (Radius + 0.5)));
                    
					double w = wx * wy;
					double val = CombinedField[hy * HighW + hx];
					if (FMath::IsNaN(val)) val = 0.0;
                    
					sum += val * w;
					weightSum += w;
				}
			}
            
			double avg = sum / FMath::Max(weightSum, 1e-6);
			avg = FMath::Clamp(avg, 0.0, 1.0);
			Out[y * OrigW + x] = (uint16)(avg * 65535.0);
		}
	}
	return Out;
}

// 1D 距離変換 (Felzenszwalb & Huttenlocher 法)
void FSDFProcessor::Compute1DDT(const double* f, double* d, int32 n, TArray<int32>& v, TArray<double>& z)
{
	int32 k = 0;
	v[0] = 0;
	z[0] = -1e12;
	z[1] = 1e12;
	for (int32 q = 1; q < n; q++)
	{
		double denom = 2.0 * q - 2.0 * v[k];
		if (denom <= 0.0) denom = 1e-6;

		double s = ((f[q] + (double)q * q) - (f[v[k]] + (double)v[k] * v[k])) / denom;
		while (s <= z[k])
		{
			k--;
			if (k < 0) { k = 0; break; }
			denom = 2.0 * q - 2.0 * v[k];
			if (denom <= 0.0) denom = 1e-6;
			s = ((f[q] + (double)q * q) - (f[v[k]] + (double)v[k] * v[k])) / denom;
		}
		k++;
		v[k] = q;
		z[k] = s;
		z[k + 1] = 1e12;
	}
	k = 0;
	for (int32 q = 0; q < n; q++)
	{
		while (z[k + 1] < (double)q)
		{
			k++;
			if (k >= n) { k = n - 1; break; }
		}
		double dist = (double)(q - v[k]);
		d[q] = dist * dist + f[v[k]];
	}
}

void FSDFProcessor::Compute2DDT(TArray<double>& grid, int32 width, int32 height)
{
	int32 maxDim = FMath::Max(width, height);
	TArray<double> f, d;
	f.SetNum(maxDim);
	d.SetNum(maxDim);
	TArray<int32> v; v.SetNum(maxDim);
	TArray<double> z; z.SetNum(maxDim + 1);

	for (int32 y = 0; y < height; y++)
	{
		for (int32 x = 0; x < width; x++) f[x] = grid[y * width + x];
		Compute1DDT(f.GetData(), d.GetData(), width, v, z);
		for (int32 x = 0; x < width; x++) grid[y * width + x] = d[x];
	}
    
	for (int32 x = 0; x < width; x++)
	{
		for (int32 y = 0; y < height; y++) f[y] = grid[y * width + x];
		Compute1DDT(f.GetData(), d.GetData(), height, v, z);
		for (int32 y = 0; y < height; y++) grid[y * width + x] = d[y];
	}
    
	for (int32 i = 0; i < width * height; ++i)
	{
		grid[i] = FMath::Sqrt(FMath::Max(0.0, grid[i]));
	}
}
