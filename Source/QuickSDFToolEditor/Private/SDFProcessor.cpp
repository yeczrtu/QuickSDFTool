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
		if (!bHasBlack) SDF[i] = -maxDistSq;
		else if (!bHasWhite) SDF[i] = maxDistSq;
		else SDF[i] = GridOut[i] - GridIn[i]; 
	}
	return SDF;
}

void FSDFProcessor::CombineSDFs(const TArray<FMaskData>& Masks, TArray<FVector4f>& OutCombined, int32 W, int32 H, ESDFOutputFormat Format, bool bSymmetry)
{
    OutCombined.SetNumZeroed(W * H);
    int32 NumMasks = Masks.Num();
    if (NumMasks == 0) return;

    // --- 1. 初期値の設定 ---
    for (int32 p = 0; p < W * H; ++p)
    {
        // R, G は 1.0 (まだ影になっていない)
        // B, A は 0.0 (まだ影が消える現象は起きていない)
        OutCombined[p] = FVector4f(1.0f, 1.0f, 1.0f, 1.0f);
    }

    TArray<uint8> HandledFlags; 
    HandledFlags.SetNumZeroed(W * H);
    auto IsHandled = [&](int32 p, int32 ch) { return (HandledFlags[p] & (1 << ch)) != 0; };
    auto SetHandled = [&](int32 p, int32 ch) { HandledFlags[p] |= (1 << ch); };

    // 90度（TargetT = 0.5）付近のマスクのインデックスを探す（アシンメトリ用）
    int32 MidIdx = 0;
    for (int32 i = 0; i < NumMasks; ++i) {
        if (Masks[i].TargetT >= 0.5f) { MidIdx = i; break; }
    }

    // --- 2. 補間処理（境界の検出） ---
    for (int32 i = 0; i < NumMasks - 1; ++i)
    {
        const FMaskData& M1 = Masks[i];
        const FMaskData& M2 = Masks[i + 1];

        if (Format == ESDFOutputFormat::Bipolar && M1.bIsOpposite != M2.bIsOpposite) continue;

        for (int32 p = 0; p < W * H; ++p)
        {
        	bool bIncreased = M1.SDF[p] > 0.0 && M2.SDF[p] <= 0.0; // 影 -> 光（影が減る方向）
        	bool bDecreased = M1.SDF[p] <= 0.0 && M2.SDF[p] > 0.0; // 光 -> 影（影が増える方向）

            if (!bIncreased && !bDecreased) continue;

            double d1 = FMath::Abs(M1.SDF[p]);
            double d2 = FMath::Abs(M2.SDF[p]);
            double ratio = d1 / (d1 + d2 + 1e-10);
            float InterpT = (float)(M1.TargetT + (M2.TargetT - M1.TargetT) * ratio);

            int32 Ch_Inc = 0; // R
            int32 Ch_Dec = 2; // B
            float LocalT = InterpT;

            if (!bSymmetry && InterpT > 0.5f)
            {
                Ch_Inc = 1; Ch_Dec = 3; // G / A
                LocalT = (InterpT - 0.5f) * 2.0f;
            }
            else if (!bSymmetry)
            {
                LocalT = InterpT * 2.0f;
            }

            if (bIncreased && !IsHandled(p, Ch_Inc)) {
            	OutCombined[p][Ch_Inc] = FMath::Clamp(1.0f - LocalT, 0.0f, 1.0f);
                SetHandled(p, Ch_Inc);
            }
            if (Format == ESDFOutputFormat::Bipolar && bDecreased && !IsHandled(p, Ch_Dec)) {
                OutCombined[p][Ch_Dec] = FMath::Clamp(LocalT, 0.0f, 1.0f);
                SetHandled(p, Ch_Dec);
            }
        }
    }

    // --- 3. 塗りつぶし処理（開始状態と終了状態の確定） ---
    for (int32 p = 0; p < W * H; ++p)
    {
        if (bSymmetry)
        {
            // シンメトリ：0~90度のみ
        	bool bStartsShadow = (Masks[0].SDF[p] > 0.0);
        	bool bEndsShadow = (Masks[NumMasks - 1].SDF[p] > 0.0);
        	// R: 影→光の遷移なしで影の中にいるなら 0.0
        	if (!IsHandled(p, 0) && (bStartsShadow || bEndsShadow)) OutCombined[p].X = 0.0f;
            if (Format == ESDFOutputFormat::Bipolar) {
            	// B: 最初から影で、かつ一度も光→影の遷移が起きなかったなら 0.0
            	if (bStartsShadow && !IsHandled(p, 2)) OutCombined[p].Z = 0.0f;
            	// あるいは、途中で影→光になったが、光→影は起きなかった場合も 0.0
            	else if (IsHandled(p, 0) && !IsHandled(p, 2)) OutCombined[p].Z = 0.0f;
            }

            if (Format == ESDFOutputFormat::Monopolar) {
                float V = OutCombined[p].X;
                OutCombined[p] = FVector4f(V, V, V, 1.0f);
            }
        }
        else
        {
            // アシンメトリ：0~90度(R/B) と 90~180度(G/A)
            
            // --- R/B (0~90度区間) ---
        	bool bStartsShadow0 = (Masks[0].SDF[p] > 0.0);
        	bool bEndsShadow0 = (Masks[FMath::Max(0, MidIdx)].SDF[p] > 0.0);
        	if (!IsHandled(p, 0) && (bStartsShadow0 || bEndsShadow0)) OutCombined[p].X = 0.0f;
            if (Format == ESDFOutputFormat::Bipolar) {
            	if (bStartsShadow0 && !IsHandled(p, 2)) OutCombined[p].Z = 0.0f;
            	else if (IsHandled(p, 0) && !IsHandled(p, 2)) OutCombined[p].Z = 0.0f;
            }

            // --- G/A (90~180度区間) ---
            // 90度時点での影の状態をチェック
        	bool bStartsShadow90 = (Masks[MidIdx].SDF[p] > 0.0);
        	bool bEndsShadow90 = (Masks[NumMasks - 1].SDF[p] > 0.0);
        	if (!IsHandled(p, 1) && (bStartsShadow90 || bEndsShadow90)) OutCombined[p].Y = 0.0f;
            if (Format == ESDFOutputFormat::Bipolar) {
            	if (bStartsShadow90 && !IsHandled(p, 3)) OutCombined[p].W = 0.0f;
            	else if (IsHandled(p, 1) && !IsHandled(p, 3)) OutCombined[p].W = 0.0f;
            }
        }

        // アンチエイリアス処理
        double k = 0.08;
        for (int32 ch = 0; ch < 4; ++ch) {
            if (!IsHandled(p, ch)) {
                continue;
            }
            float& valRef = OutCombined[p][ch];
            double val = (double)valRef;
            double h0 = FMath::Max(k - FMath::Abs(val - 0.0), 0.0) / k;
            val = FMath::Max(val, 0.0) + h0 * h0 * k * 0.25;
            double h1 = FMath::Max(k - FMath::Abs(val - 1.0), 0.0) / k;
            val = FMath::Min(val, 1.0) - h1 * h1 * k * 0.25;
            valRef = (float)val;
        }
    }
}

TArray<FFloat16Color> FSDFProcessor::DownscaleAndConvert(const TArray<FVector4f>& CombinedField, int32 HighW, int32 HighH, int32 Factor)
{
	int32 OrigW = HighW / FMath::Max(1, Factor);
	int32 OrigH = HighH / FMath::Max(1, Factor);
	TArray<FFloat16Color> Out;
	Out.SetNumUninitialized(OrigW * OrigH);
	
	int32 Radius = FMath::Max(1, FMath::CeilToInt(Factor * 1.5f)); 

	for (int32 y = 0; y < OrigH; ++y)
	{
		for (int32 x = 0; x < OrigW; ++x)
		{
			FVector4f sum(0.0f, 0.0f, 0.0f, 0.0f);
			float weightSum = 0.0f;
        	
			float cx = (x + 0.5f) * Factor;
			float cy = (y + 0.5f) * Factor;

			for(int32 dy = -Radius; dy <= Radius; ++dy)
			{
				int32 hy = FMath::Clamp(FMath::RoundToInt(cy) + dy, 0, HighH - 1);
				float distY = FMath::Abs(cy - (hy + 0.5f));
				float wy = FMath::Max(0.0f, 1.0f - (distY / (Radius + 0.5f)));

				for(int32 dx = -Radius; dx <= Radius; ++dx)
				{
					int32 hx = FMath::Clamp(FMath::RoundToInt(cx) + dx, 0, HighW - 1);
					float distX = FMath::Abs(cx - (hx + 0.5f));
					float wx = FMath::Max(0.0f, 1.0f - (distX / (Radius + 0.5f)));
                    
					float w = wx * wy;
					FVector4f val = CombinedField[hy * HighW + hx];
					
					sum += val * w;
					weightSum += w;
				}
			}
            
			sum /= FMath::Max(weightSum, 1e-6f);
			
			FFloat16Color Color;
			Color.R = FMath::Clamp(sum.X, 0.0f, 1.0f);
			Color.G = FMath::Clamp(sum.Y, 0.0f, 1.0f);
			Color.B = FMath::Clamp(sum.Z, 0.0f, 1.0f);
			Color.A = FMath::Clamp(sum.W, 0.0f, 1.0f);
			
			Out[y * OrigW + x] = Color;
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
