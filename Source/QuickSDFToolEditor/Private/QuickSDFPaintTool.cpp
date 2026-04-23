#include "QuickSDFPaintTool.h"
#include "InteractiveToolManager.h"
#include "ToolBuilderUtil.h"
#include "EngineUtils.h"
#include "Engine/DirectionalLight.h"
#include "CollisionQueryParams.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/Texture2D.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "UObject/SavePackage.h"
#include "BaseBehaviors/ClickDragBehavior.h"
#include "TargetInterfaces/PrimitiveComponentBackedTarget.h"
#include "TargetInterfaces/MeshDescriptionProvider.h"
#include "DynamicMesh/MeshTransforms.h"
#include "MeshDescriptionToDynamicMesh.h"
#include "DynamicMesh/DynamicMeshAttributeSet.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Components/PrimitiveComponent.h"
#include "Kismet/KismetRenderingLibrary.h"
#include "Intersection/IntrRay3Triangle3.h"
#include "Spatial/SpatialInterfaces.h"
#include "IndexTypes.h"
#include "Engine/Canvas.h"
#include "CanvasItem.h"
#include "CanvasTypes.h"
#include "TextureResource.h"
#include "RenderResource.h"
#include "Math/UnrealMathUtility.h"
#include "InputCoreTypes.h"
#include "HAL/PlatformApplicationMisc.h"
#include "InteractiveToolChange.h"
#include "Misc/ScopedSlowTask.h"
#include "IAssetTools.h"
#include "AssetToolsModule.h"

#if WITH_EDITOR
#include "Editor.h"
#endif

#define LOCTEXT_NAMESPACE "QuickSDFPaintTool"

namespace
{
// レンダーターゲットのカラー配列を確実にグレースケール（0～255）にする
static TArray<uint8> ConvertToGrayscale(const TArray<FColor>& Src)
{
    TArray<uint8> Dst;
    Dst.SetNumUninitialized(Src.Num());
    for(int32 i = 0; i < Src.Num(); ++i)
    {
        Dst[i] = (uint8)(((int32)Src[i].R + (int32)Src[i].G + (int32)Src[i].B) / 3);
    }
    return Dst;
}

// 双線形補間によるアップスケール
static TArray<uint8> UpscaleImage(const TArray<uint8>& Src, int32 SrcW, int32 SrcH, int32 Upscale)
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

// 1D 距離変換 (Felzenszwalb & Huttenlocher 法)
static void Compute1DDT(const double* f, double* d, int32 n, TArray<int32>& v, TArray<double>& z)
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

// 2D 距離変換
static void Compute2DDT(TArray<double>& grid, int32 width, int32 height)
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

static TArray<double> GenerateSDF(const TArray<uint8>& BinaryImg, int32 W, int32 H)
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

struct FMaskData
{
    TArray<double> SDF;
    float TargetT;
};

static void CombineSDFs(const TArray<FMaskData>& Masks, TArray<double>& OutCombined, int32 W, int32 H)
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

static TArray<uint16> DownscaleAndConvert(const TArray<double>& CombinedField, int32 HighW, int32 HighH, int32 Factor)
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

	static void Create16BitTexture(const TArray<uint16>& Pixels, int32 Width, int32 Height, const FString& FolderPath, const FString& TextureName)
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

constexpr double QuickSDFStrokeSpacingFactor = 0.12;
constexpr double QuickSDFMinSampleDistanceSq = 1.0e-6;
constexpr int32 QuickSDFBrushMaskResolution = 128;
constexpr int32 QuickSDFPreviewActionIncreaseBrush = static_cast<int32>(EStandardToolActions::BaseClientDefinedActionID) + 1;
constexpr float QuickSDFMinResizeSensitivity = 0.01f;
constexpr double QuickSDFSubPixelSpacing = 0.35;
constexpr double QuickSDFStrokeSmoothingMinAlpha = 0.15;
constexpr double QuickSDFStrokeSmoothingMaxAlpha = 0.85;

class FQuickSDFRenderTargetChange : public FToolCommandChange
{
public:
	int32 AngleIndex = INDEX_NONE;
	TArray<FColor> BeforePixels;
	TArray<FColor> AfterPixels;

	virtual void Apply(UObject* Object) override;
	virtual void Revert(UObject* Object) override;
	virtual FString ToString() const override { return TEXT("FQuickSDFRenderTargetChange"); }
};
}

void UQuickSDFBrushResizeInputBehavior::Initialize(UQuickSDFPaintTool* InTool)
{
	BrushTool = InTool;
}

EInputDevices UQuickSDFBrushResizeInputBehavior::GetSupportedDevices()
{
	return EInputDevices::Keyboard;
}

bool UQuickSDFBrushResizeInputBehavior::IsPressed(const FInputDeviceState& Input)
{
	if (!Input.IsFromDevice(EInputDevices::Keyboard))
	{
		return false;
	}

	return Input.Keyboard.ActiveKey.Button == EKeys::F && Input.Keyboard.ActiveKey.bDown && FInputDeviceState::IsCtrlKeyDown(Input);
}

bool UQuickSDFBrushResizeInputBehavior::IsReleased(const FInputDeviceState& Input)
{
	return false;
}

FInputCaptureRequest UQuickSDFBrushResizeInputBehavior::WantsCapture(const FInputDeviceState& Input)
{
	return IsPressed(Input) ? FInputCaptureRequest::Begin(this, EInputCaptureSide::Any, 0.0f) : FInputCaptureRequest::Ignore();
}

FInputCaptureUpdate UQuickSDFBrushResizeInputBehavior::BeginCapture(const FInputDeviceState& Input, EInputCaptureSide Side)
{
	if (BrushTool)
	{
		BrushTool->BeginBrushResizeMode();
	}

	return FInputCaptureUpdate::End();
}

FInputCaptureUpdate UQuickSDFBrushResizeInputBehavior::UpdateCapture(const FInputDeviceState& Input, const FInputCaptureData& Data)
{
	return FInputCaptureUpdate::End();
}

void UQuickSDFBrushResizeInputBehavior::ForceEndCapture(const FInputCaptureData& Data)
{
}

void UQuickSDFToolProperties::RotateLight90Deg()
{
	static float Angle = 0.0f;
	Angle += 90.0f;
#if WITH_EDITOR
	if (GEditor)
	{
		UWorld* World = GEditor->GetEditorWorldContext().World();
		if (World)
		{
			for (TActorIterator<ADirectionalLight> It(World); It; ++It)
			{
				ADirectionalLight* DirLight = *It;
				if (DirLight)
				{
					FRotator NewRotation(-45.0f, Angle, 0.0f);
					DirLight->SetActorRotation(NewRotation);
					break;
				}
			}
		}
	}
#endif
}

void UQuickSDFToolProperties::ExportToTexture()
{
	UQuickSDFPaintTool* Tool = Cast<UQuickSDFPaintTool>(GetOuter());
	if (!Tool) return;

	// AssetToolsモジュールの取得
	IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();

	for (int32 i = 0; i < TransientRenderTargets.Num(); ++i)
	{
		UTextureRenderTarget2D* RT = TransientRenderTargets[i];
		if (!RT) continue;

		TArray<FColor> Pixels;
		if (!Tool->CaptureRenderTargetPixels(RT, Pixels)) continue;

		FString FolderPath = TEXT("/Game/QuickSDF_Exports"); // フォルダ
		FString AssetName = FString::Printf(TEXT("T_QuickSDF_Angle%d"), i);

		// 1. AssetToolsを使ってアセットを作成（これが重要！）
		UTexture2D* NewTex = Cast<UTexture2D>(AssetTools.CreateAsset(AssetName, FolderPath, UTexture2D::StaticClass(), nullptr));
        
		if (NewTex)
		{
			// 2. データの流し込み
			NewTex->Source.Init(RT->SizeX, RT->SizeY, 1, 1, TSF_BGRA8);
			void* MipData = NewTex->Source.LockMip(0);
			FMemory::Memcpy(MipData, Pixels.GetData(), Pixels.Num() * sizeof(FColor));
			NewTex->Source.UnlockMip(0);

			NewTex->SRGB = RT->SRGB;
			NewTex->CompressionSettings = TC_Default;
			NewTex->MipGenSettings = TMGS_NoMipmaps;

			// 3. 更新通知と保存準備
			NewTex->PostEditChange();
			NewTex->GetPackage()->MarkPackageDirty();
            
			// コンテンツブラウザでそのアセットを選択（ハイライト）させる（任意）
			TArray<UObject*> AssetsToSync;
			AssetsToSync.Add(NewTex);
			AssetTools.SyncBrowserToAssets(AssetsToSync);
		}
	}
}

void UQuickSDFToolProperties::GenerateSDFThresholdMap()
{
	UQuickSDFPaintTool* Tool = Cast<UQuickSDFPaintTool>(GetOuter());
	if (Tool)
	{
		Tool->GeneratePerfectSDF();
	}
}

UQuickSDFPaintTool::UQuickSDFPaintTool()
{
}

void UQuickSDFPaintTool::RegisterActions(FInteractiveToolActionSet& ActionSet)
{
	Super::RegisterActions(ActionSet);

	ActionSet.RegisterAction(
		this,
		QuickSDFPreviewActionIncreaseBrush,
		TEXT("QuickSDFIncreaseBrushRadius"),
		NSLOCTEXT("QuickSDFPaintTool", "IncreaseBrushShortcut", "Increase Brush"),
		NSLOCTEXT("QuickSDFPaintTool", "IncreaseBrushShortcutDesc", "Increase brush radius."),
		EModifierKey::Control,
		EKeys::F,
		[this]()
		{
			BeginBrushResizeMode();
		});
}

void UQuickSDFPaintTool::InitializeRenderTargets()
{
	if (!Properties)
	{
		return;
	}

	const int32 NumAngles = Properties->NumAngles;
	
	// アングルに対応する階調値の配列を同期
	if (Properties->TargetAngles.Num() != NumAngles)
	{
		Properties->TargetAngles.SetNum(NumAngles);
		for(int32 i = 0; i < NumAngles; ++i)
		{
			Properties->TargetAngles[i] = (float)i / (float)FMath::Max(1, NumAngles - 1);
		}
	}

	if (Properties->TransientRenderTargets.Num() != NumAngles)
	{
		Properties->TransientRenderTargets.Empty();
		for (int32 i = 0; i < NumAngles; ++i)
		{
			UTextureRenderTarget2D* RT = UKismetRenderingLibrary::CreateRenderTarget2D(this, Properties->Resolution.X, Properties->Resolution.Y, RTF_RGBA8);
			if (RT)
			{
				RT->ClearColor = FLinearColor::White;
				RT->UpdateResourceImmediate(true);
				Properties->TransientRenderTargets.Add(RT);
			}
		}
	}
}

void UQuickSDFPaintTool::GeneratePerfectSDF()
{
	if (!Properties) return;

	TArray<FMaskData> ProcessedData;
	int32 OrigW = Properties->Resolution.X;
	int32 OrigH = Properties->Resolution.Y;
	int32 Upscale = FMath::Clamp(Properties->UpscaleFactor, 1, 8);
	int32 HighW = OrigW * Upscale;
	int32 HighH = OrigH * Upscale;

	TArray<int32> ValidIndices;
	for (int32 i = 0; i < Properties->TransientRenderTargets.Num(); ++i)
	{
		if (i < Properties->TargetAngles.Num() && Properties->TransientRenderTargets[i]) ValidIndices.Add(i);
	}
	
	ValidIndices.Sort([this](int32 A, int32 B) {
		return Properties->TargetAngles[A] < Properties->TargetAngles[B];
	});

	FScopedSlowTask SlowTask(ValidIndices.Num() + 2, LOCTEXT("GenerateSDF", "Generating Perfect SDF..."));
	SlowTask.MakeDialog(true);

	for (int32 Index : ValidIndices)
	{
		SlowTask.EnterProgressFrame(1.f, FText::Format(LOCTEXT("ProcessMask", "Processing Mask {0}..."), Index));
		if (SlowTask.ShouldCancel()) return;

		UTextureRenderTarget2D* RT = Properties->TransientRenderTargets[Index];
		TArray<FColor> Pixels;
		if (!CaptureRenderTargetPixels(RT, Pixels)) continue;

		TArray<uint8> GrayPixels = ConvertToGrayscale(Pixels);
		TArray<uint8> UpscaledPixels = UpscaleImage(GrayPixels, OrigW, OrigH, Upscale);
		TArray<double> SDF = GenerateSDF(UpscaledPixels, HighW, HighH);

		FMaskData Data;
		Data.SDF = MoveTemp(SDF);
		Data.TargetT = Properties->TargetAngles[Index];
		ProcessedData.Add(MoveTemp(Data));
	}

	if (ProcessedData.Num() == 0) return;

	SlowTask.EnterProgressFrame(1.f, LOCTEXT("CombineSDF", "Combining SDFs..."));
	if (SlowTask.ShouldCancel()) return;

	TArray<double> CombinedField;
	CombineSDFs(ProcessedData, CombinedField, HighW, HighH);

	SlowTask.EnterProgressFrame(1.f, LOCTEXT("DownscaleSDF", "Downscaling and Saving..."));
	if (SlowTask.ShouldCancel()) return;

	TArray<uint16> FinalPixels = DownscaleAndConvert(CombinedField, HighW, HighH, Upscale);

	// テクスチャとして保存
	FString PackageName = TEXT("/Game/QuickSDF_UltraHighRes");
	FString TextureName = TEXT("T_QuickSDF_ThresholdMap");
	Create16BitTexture(FinalPixels, OrigW, OrigH, PackageName, TextureName);
}

void UQuickSDFPaintTool::BuildBrushMaskTexture()
{
	if (BrushMaskTexture)
	{
		return;
	}

	BrushMaskTexture = UTexture2D::CreateTransient(QuickSDFBrushMaskResolution, QuickSDFBrushMaskResolution, PF_B8G8R8A8);
	if (!BrushMaskTexture || !BrushMaskTexture->GetPlatformData() || BrushMaskTexture->GetPlatformData()->Mips.Num() == 0)
	{
		return;
	}

	BrushMaskTexture->MipGenSettings = TMGS_NoMipmaps;
	BrushMaskTexture->Filter = TF_Bilinear;
	BrushMaskTexture->SRGB = false;

	FTexture2DMipMap& Mip = BrushMaskTexture->GetPlatformData()->Mips[0];
	FColor* Pixels = static_cast<FColor*>(Mip.BulkData.Lock(LOCK_READ_WRITE));
	if (!Pixels)
	{
		Mip.BulkData.Unlock();
		return;
	}

	const float Radius = (QuickSDFBrushMaskResolution - 1) * 0.5f;
	const FVector2f Center(Radius, Radius);

	for (int32 Y = 0; Y < QuickSDFBrushMaskResolution; ++Y)
	{
		for (int32 X = 0; X < QuickSDFBrushMaskResolution; ++X)
		{
			const FVector2f Pos(static_cast<float>(X), static_cast<float>(Y));
			const float Dist = FVector2f::Distance(Pos, Center);
			const float Falloff = FMath::Clamp(Radius - Dist + 0.5f, 0.0f, 1.0f);
			const uint8 Alpha = static_cast<uint8>(Falloff * 255.0f);

			Pixels[Y * QuickSDFBrushMaskResolution + X] = FColor(255, 255, 255, Alpha);
		}
	}

	Mip.BulkData.Unlock();
	BrushMaskTexture->UpdateResource();
}

void UQuickSDFPaintTool::Setup()
{
	Super::Setup();

	Properties = NewObject<UQuickSDFToolProperties>(this);
	AddToolPropertySource(Properties);

	InitializeRenderTargets();
	BuildBrushMaskTexture();
	ResetStrokeState();

	BrushResizeBehavior = NewObject<UQuickSDFBrushResizeInputBehavior>(this);
	BrushResizeBehavior->Initialize(this);
	AddInputBehavior(BrushResizeBehavior);
}

void UQuickSDFPaintTool::RefreshPreviewMaterial()
{
	if (!PreviewMaterial)
	{
		return;
	}

	if (UTextureRenderTarget2D* ActiveRT = GetActiveRenderTarget())
	{
		PreviewMaterial->SetTextureParameterValue(TEXT("BaseColor"), ActiveRT);
		PreviewMaterial->SetScalarParameterValue(TEXT("UVChannel"), (float)Properties->UVChannel);
	}
}

FQuickSDFStrokeSample UQuickSDFPaintTool::SmoothStrokeSample(const FQuickSDFStrokeSample& RawSample)
{
#if 0//todo impliment one euro filter
	double MinCutoff = 1.0;//FMath::Lerp(5.0, 0.1, FMath::Clamp(StabilizerAmount, 0.0f, 1.0f));
	WorldPosFilter.MinCutoff = MinCutoff;
	UVFilter.MinCutoff = MinCutoff;

	double DeltaTime = GetToolManager()->GetContextQueriesAPI()->GetCurrentEditingWorld()->GetDeltaSeconds();
	if (DeltaTime <= 0.0) DeltaTime = 1.0/60.0;

	FQuickSDFStrokeSample FilteredSample = RawSample;

	FilteredSample.WorldPos = WorldPosFilter.Update(RawSample.WorldPos, DeltaTime);

	FVector3d UVAsVector(RawSample.UV.X, RawSample.UV.Y, 0.0);
	FVector3d FilteredUV = UVFilter.Update(UVAsVector, DeltaTime);
	FilteredSample.UV = FVector2f(FilteredUV.X, FilteredUV.Y);

	return FilteredSample;
#else
	return RawSample;
#endif
}

void UQuickSDFPaintTool::ChangeTargetComponent(UPrimitiveComponent* NewComponent)
{
	if (CurrentComponent == NewComponent)
	{
		return;
	}

	if (CurrentComponent)
	{
		for (int32 i = 0; i < OriginalMaterials.Num(); ++i)
		{
			if (OriginalMaterials[i] && CurrentComponent->GetNumMaterials() > i)
			{
				CurrentComponent->SetMaterial(i, OriginalMaterials[i]);
			}
		}
	}

	OriginalMaterials.Empty();
	CurrentComponent = NewComponent;
	TargetMeshSpatial.Reset();
	TargetMesh.Reset();
	ResetStrokeState();

	if (!CurrentComponent)
	{
		return;
	}

	bool bValidMeshLoaded = false;
	TSharedPtr<UE::Geometry::FDynamicMesh3> TempMesh = MakeShared<UE::Geometry::FDynamicMesh3>();

	if (UStaticMeshComponent* SMC = Cast<UStaticMeshComponent>(CurrentComponent))
	{
		if (UStaticMesh* StaticMesh = SMC->GetStaticMesh())
		{
			const FMeshDescription* MeshDesc = StaticMesh->GetMeshDescription(0);
			if (MeshDesc)
			{
				FMeshDescriptionToDynamicMesh Converter;
				Converter.Convert(MeshDesc, *TempMesh);
				bValidMeshLoaded = true;
			}
		}
	}

	if (!bValidMeshLoaded || TempMesh->TriangleCount() <= 0)
	{
		CurrentComponent = nullptr;
		return;
	}

	TargetMesh = TempMesh;
	TargetMeshSpatial = MakeShared<UE::Geometry::FDynamicMeshAABBTree3>();
	TargetMeshSpatial->SetMesh(TargetMesh.Get(), true);

	PreviewMaterial = UMaterialInstanceDynamic::Create(
		LoadObject<UMaterialInterface>(nullptr, TEXT("/QuickSDFTool/Materials/M_PreviewMat.M_PreviewMat")),
		this);

	if (!PreviewMaterial)
	{
		return;
	}

	for (int32 i = 0; i < CurrentComponent->GetNumMaterials(); ++i)
	{
		OriginalMaterials.Add(CurrentComponent->GetMaterial(i));
		CurrentComponent->SetMaterial(i, PreviewMaterial);
	}

	RefreshPreviewMaterial();
}

void UQuickSDFPaintTool::Shutdown(EToolShutdownType ShutdownType)
{
	if (bBrushResizeTransactionOpen)
	{
		GetToolManager()->EndUndoTransaction();
		bBrushResizeTransactionOpen = false;
	}

	ChangeTargetComponent(nullptr);
	UInteractiveTool::Shutdown(ShutdownType);
}

bool UQuickSDFPaintTool::CaptureRenderTargetPixels(UTextureRenderTarget2D* RenderTarget, TArray<FColor>& OutPixels) const
{
	if (!RenderTarget) return false;
	FTextureRenderTargetResource* RTResource = RenderTarget->GameThread_GetRenderTargetResource();
	if (!RTResource) return false;
	FReadSurfaceDataFlags ReadFlags(RCM_UNorm);
	ReadFlags.SetLinearToGamma(false);
	return RTResource->ReadPixels(OutPixels, ReadFlags);
}

bool UQuickSDFPaintTool::RestoreRenderTargetPixels(UTextureRenderTarget2D* RenderTarget, const TArray<FColor>& Pixels) const
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
	FCanvas Canvas(RTResource, nullptr, GetToolManager()->GetContextQueriesAPI()->GetCurrentEditingWorld(), GMaxRHIFeatureLevel);
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

bool UQuickSDFPaintTool::ApplyRenderTargetPixels(int32 AngleIndex, const TArray<FColor>& Pixels)
{
	if (!Properties || !Properties->TransientRenderTargets.IsValidIndex(AngleIndex)) return false;
	const bool bRestored = RestoreRenderTargetPixels(Properties->TransientRenderTargets[AngleIndex], Pixels);
	if (bRestored) RefreshPreviewMaterial();
	return bRestored;
}

void UQuickSDFPaintTool::BeginStrokeTransaction()
{
	UTextureRenderTarget2D* RT = GetActiveRenderTarget();
	if (!RT || bStrokeTransactionActive) return;
	StrokeBeforePixels.Reset();
	if (CaptureRenderTargetPixels(RT, StrokeBeforePixels))
	{
		bStrokeTransactionActive = true;
		StrokeTransactionAngleIndex = FMath::Clamp(Properties->EditAngleIndex, 0, Properties->TransientRenderTargets.Num() - 1);
	}
}

void UQuickSDFPaintTool::EndStrokeTransaction()
{
	if (!bStrokeTransactionActive) return;
	TArray<FColor> AfterPixels;
	if (Properties && Properties->TransientRenderTargets.IsValidIndex(StrokeTransactionAngleIndex))
	{
		if (CaptureRenderTargetPixels(Properties->TransientRenderTargets[StrokeTransactionAngleIndex], AfterPixels) &&
			AfterPixels.Num() == StrokeBeforePixels.Num() && AfterPixels != StrokeBeforePixels)
		{
			TUniquePtr<FQuickSDFRenderTargetChange> Change = MakeUnique<FQuickSDFRenderTargetChange>();
			Change->AngleIndex = StrokeTransactionAngleIndex;
			Change->BeforePixels = MoveTemp(StrokeBeforePixels);
			Change->AfterPixels = MoveTemp(AfterPixels);
			GetToolManager()->EmitObjectChange(this, MoveTemp(Change), LOCTEXT("QuickSDFPaintStrokeChange", "Quick SDF Paint Stroke"));
		}
	}
	bStrokeTransactionActive = false;
	StrokeTransactionAngleIndex = INDEX_NONE;
	StrokeBeforePixels.Reset();
}

UTextureRenderTarget2D* UQuickSDFPaintTool::GetActiveRenderTarget() const
{
	if (!Properties) return nullptr;
	const int32 AngleIdx = FMath::Clamp(Properties->EditAngleIndex, 0, Properties->TransientRenderTargets.Num() - 1);
	return Properties->TransientRenderTargets.IsValidIndex(AngleIdx) ? Properties->TransientRenderTargets[AngleIdx] : nullptr;
}

FVector2D UQuickSDFPaintTool::GetPreviewOrigin() const { return PreviewCanvasOrigin; }
FVector2D UQuickSDFPaintTool::GetPreviewSize() const { return PreviewCanvasSize; }

FVector2D UQuickSDFPaintTool::ConvertInputScreenToCanvasSpace(const FVector2D& ScreenPosition) const
{
	const float DPIScale = FMath::Max(FPlatformApplicationMisc::GetDPIScaleFactorAtPoint(ScreenPosition.X, ScreenPosition.Y), 1.0f);
	return ScreenPosition / DPIScale;
}

bool UQuickSDFPaintTool::IsInPreviewBounds(const FVector2D& ScreenPosition) const
{
	const FVector2D CanvasPos = ConvertInputScreenToCanvasSpace(ScreenPosition);
	const FVector2D Origin = GetPreviewOrigin();
	const FVector2D Size = GetPreviewSize();
	return CanvasPos.X >= Origin.X && CanvasPos.Y >= Origin.Y &&
		CanvasPos.X <= Origin.X + Size.X && CanvasPos.Y <= Origin.Y + Size.Y;
}

FVector2f UQuickSDFPaintTool::ScreenToPreviewUV(const FVector2D& ScreenPosition) const
{
	const FVector2D CanvasPos = ConvertInputScreenToCanvasSpace(ScreenPosition);
	const FVector2D Origin = GetPreviewOrigin();
	const FVector2D Size = GetPreviewSize();
	const float U = static_cast<float>(FMath::Clamp((CanvasPos.X - Origin.X) / Size.X, 0.0, 1.0));
	const float V = static_cast<float>(FMath::Clamp((CanvasPos.Y - Origin.Y) / Size.Y, 0.0, 1.0));
	return FVector2f(U, V);
}

FVector2D UQuickSDFPaintTool::GetBrushPixelSize(UTextureRenderTarget2D* RenderTarget) const
{
	if (!RenderTarget) return FVector2D(16.0, 16.0);
	const FVector2D RTSize(RenderTarget->SizeX, RenderTarget->SizeY);
	if (CurrentComponent && TargetMesh.IsValid())
	{
		const FTransform Transform = CurrentComponent->GetComponentTransform();
		const float MeshBoundsMax = static_cast<float>(TargetMesh->GetBounds().MaxDim());
		const float MaxScale = static_cast<float>(Transform.GetScale3D().GetMax());
		if (MeshBoundsMax > KINDA_SMALL_NUMBER && MaxScale > KINDA_SMALL_NUMBER && BrushProperties)
		{
			const float UVBrushSize = (BrushProperties->BrushRadius / MaxScale / MeshBoundsMax) * 2.0f;
			return FVector2D(UVBrushSize * RTSize.X, UVBrushSize * RTSize.Y);
		}
	}
	const float FallbackDiameter = BrushProperties ? FMath::Max(BrushProperties->BrushRadius * 2.0f, 1.0f) : 16.0f;
	return FVector2D(FallbackDiameter, FallbackDiameter);
}

double UQuickSDFPaintTool::GetCurrentStrokeSpacing(UTextureRenderTarget2D* RenderTarget) const
{
	if (ActiveStrokeInputMode == EQuickSDFStrokeInputMode::TexturePreview)
	{
		const FVector2D PixelSize = GetBrushPixelSize(RenderTarget);
		const double PixelRadius = FMath::Max(static_cast<double>(FMath::Min(PixelSize.X, PixelSize.Y) * 0.5f), 1.0);
		return FMath::Max(PixelRadius * QuickSDFStrokeSpacingFactor, 1.0);
	}
	if (!BrushProperties) return 1.0;
	return FMath::Max(static_cast<double>(BrushProperties->BrushRadius) * QuickSDFStrokeSpacingFactor, 0.1);
}

bool UQuickSDFPaintTool::IsPaintingShadow() const { return GetShiftToggle(); }

void FQuickSDFRenderTargetChange::Apply(UObject* Object)
{
	if (UQuickSDFPaintTool* Tool = Cast<UQuickSDFPaintTool>(Object)) Tool->ApplyRenderTargetPixels(AngleIndex, AfterPixels);
}

void FQuickSDFRenderTargetChange::Revert(UObject* Object)
{
	if (UQuickSDFPaintTool* Tool = Cast<UQuickSDFPaintTool>(Object)) Tool->ApplyRenderTargetPixels(AngleIndex, BeforePixels);
}

void UQuickSDFPaintTool::BeginBrushResizeMode()
{
	if (!BrushProperties) return;
	if (!bBrushResizeTransactionOpen)
	{
		GetToolManager()->BeginUndoTransaction(LOCTEXT("QuickSDFBrushResizeTransaction", "Quick SDF Change Brush Radius"));
		BrushProperties->SetFlags(RF_Transactional);
		BrushProperties->Modify();
		bBrushResizeTransactionOpen = true;
	}
	bAdjustingBrushRadius = true;
	BrushResizeStartScreenPosition = LastInputScreenPosition;
	BrushResizeStartAbsolutePosition = FSlateApplication::Get().GetCursorPos();
	BrushResizeStartScreenPosition = LastInputScreenPosition;
	BrushResizeStartRadius = BrushProperties->BrushRadius;
}

void UQuickSDFPaintTool::UpdateBrushResizeFromCursor()
{
	if (!bAdjustingBrushRadius || !BrushProperties) return;
	const FVector2D Delta = ConvertInputScreenToCanvasSpace(LastInputScreenPosition) - ConvertInputScreenToCanvasSpace(BrushResizeStartScreenPosition);
	const float NewRadius = FMath::Max(0.1f, BrushResizeStartRadius + (Delta.X * FMath::Max(BrushResizeSensitivity, QuickSDFMinResizeSensitivity)));
	const float RangeMin = BrushRelativeSizeRange.Min;
	const float RangeSize = BrushRelativeSizeRange.Max - BrushRelativeSizeRange.Min;
	if (RangeSize > KINDA_SMALL_NUMBER)
	{
		BrushProperties->BrushSize = FMath::Clamp((NewRadius - RangeMin) / RangeSize, 0.0f, 1.0f);
	}
	BrushProperties->BrushRadius = NewRadius;
	LastBrushStamp.Radius = NewRadius;
	NotifyOfPropertyChangeByTool(BrushProperties);
}

void UQuickSDFPaintTool::EndBrushResizeMode()
{
	if (!bAdjustingBrushRadius) return;
	UpdateBrushResizeFromCursor();
	FSlateApplication::Get().SetCursorPos(BrushResizeStartAbsolutePosition);
	bAdjustingBrushRadius = false;
	if (bBrushResizeTransactionOpen)
	{
		GetToolManager()->EndUndoTransaction();
		bBrushResizeTransactionOpen = false;
	}
}

FInputRayHit UQuickSDFPaintTool::CanBeginClickDragSequence(const FInputDeviceRay& PressPos)
{
	LastInputScreenPosition = PressPos.ScreenPosition;
	if (bAdjustingBrushRadius)
	{
		PendingStrokeInputMode = EQuickSDFStrokeInputMode::None;
		return FInputRayHit(0.0f);
	}
	if (GetActiveRenderTarget() && IsInPreviewBounds(PressPos.ScreenPosition))
	{
		PendingStrokeInputMode = EQuickSDFStrokeInputMode::TexturePreview;
		return FInputRayHit(0.0f);
	}
	FHitResult OutHit;
	if (HitTest(PressPos.WorldRay, OutHit))
	{
		PendingStrokeInputMode = EQuickSDFStrokeInputMode::MeshSurface;
		return FInputRayHit(OutHit.Distance);
	}
	PendingStrokeInputMode = EQuickSDFStrokeInputMode::None;
	return Super::CanBeginClickDragSequence(PressPos);
}

void UQuickSDFPaintTool::OnClickPress(const FInputDeviceRay& PressPos)
{
	LastInputScreenPosition = PressPos.ScreenPosition;
	if (bAdjustingBrushRadius)
	{
		EndBrushResizeMode();
		return;
	}
	Super::OnClickPress(PressPos);
}

void UQuickSDFPaintTool::OnClickDrag(const FInputDeviceRay& DragPos)
{
	LastInputScreenPosition = DragPos.ScreenPosition;
	if (bAdjustingBrushRadius)
	{
		UpdateBrushResizeFromCursor();
		return;
	}
	Super::OnClickDrag(DragPos);
}

void UQuickSDFPaintTool::OnClickRelease(const FInputDeviceRay& ReleasePos)
{
	LastInputScreenPosition = ReleasePos.ScreenPosition;
	if (bAdjustingBrushRadius) return;
	Super::OnClickRelease(ReleasePos);
}

bool UQuickSDFPaintTool::HitTest(const FRay& Ray, FHitResult& OutHit)
{
	UPrimitiveComponent* ComponentToTrace = nullptr;
	if (CurrentComponent)
	{
		ComponentToTrace = Cast<UPrimitiveComponent>(CurrentComponent);
	}
	else
	{
		UWorld* World = GetToolManager()->GetContextQueriesAPI()->GetCurrentEditingWorld();
		FHitResult TempHit;
		FCollisionQueryParams SearchParams(SCENE_QUERY_STAT(QuickSDFSearch), true);
		if (World->LineTraceSingleByChannel(TempHit, Ray.Origin, Ray.Origin + Ray.Direction * 100000.f, ECC_Visibility, SearchParams))
		{
			ComponentToTrace = TempHit.GetComponent();
		}
	}
	if (ComponentToTrace)
	{
		FCollisionQueryParams Params(SCENE_QUERY_STAT(QuickSDF), true);
		Params.bReturnFaceIndex = true;
		Params.bTraceComplex = true;
		const FVector End = Ray.Origin + Ray.Direction * 100000.0f;
		if (ComponentToTrace->LineTraceComponent(OutHit, Ray.Origin, End, Params))
		{
			ChangeTargetComponent(OutHit.GetComponent());
			return true;
		}
	}
	return false;
}

bool UQuickSDFPaintTool::OnUpdateHover(const FInputDeviceRay& DevicePos)
{
	LastInputScreenPosition = DevicePos.ScreenPosition;
	if (bAdjustingBrushRadius)
	{
		UpdateBrushResizeFromCursor();
		return true;
	}
	return Super::OnUpdateHover(DevicePos);
}

void UQuickSDFPaintTool::OnBeginDrag(const FRay& Ray)
{
	Super::OnBeginDrag(Ray);
	const EQuickSDFStrokeInputMode StrokeMode = PendingStrokeInputMode;
	ResetStrokeState();
	ActiveStrokeInputMode = StrokeMode;

	PointBuffer.Empty();
	AccumulatedDistance = 0.0;

	FQuickSDFStrokeSample StartSample;
	bool bHasSample = false;
	if (ActiveStrokeInputMode == EQuickSDFStrokeInputMode::TexturePreview) {
		bHasSample = TryMakePreviewStrokeSample(LastInputScreenPosition, StartSample);
	} else if (ActiveStrokeInputMode == EQuickSDFStrokeInputMode::MeshSurface) {
		bHasSample = TryMakeStrokeSample(Ray, StartSample);
	}

	if (bHasSample) {
		BeginStrokeTransaction(); 
		PointBuffer.Add(StartSample);
		//StampSample(StartSample); 
	}
}

void UQuickSDFPaintTool::OnUpdateDrag(const FRay& Ray)
{
	Super::OnUpdateDrag(Ray);
	if (bAdjustingBrushRadius) return;

	FQuickSDFStrokeSample Sample;
	bool bHasSample = false;
	
	if (ActiveStrokeInputMode == EQuickSDFStrokeInputMode::TexturePreview)
	{
		bHasSample = TryMakePreviewStrokeSample(LastInputScreenPosition, Sample);
	}
	else if (ActiveStrokeInputMode == EQuickSDFStrokeInputMode::MeshSurface)
	{
		bHasSample = TryMakeStrokeSample(Ray, Sample);
	}

	if (bHasSample)
	{
		Sample = SmoothStrokeSample(Sample);
		AppendStrokeSample(Sample);
	}
}

void UQuickSDFPaintTool::OnEndDrag(const FRay& Ray)
{
	
	if (PointBuffer.Num() >= 2) {
		FQuickSDFStrokeSample Last = PointBuffer.Last();
		
		AppendStrokeSample(Last); 
		
		int32 L = PointBuffer.Num();
		if (L >= 4) {
			StampInterpolatedSegment(PointBuffer[L-3], PointBuffer[L-2], PointBuffer[L-1], PointBuffer[L-1]);
		}
	}

	EndStrokeTransaction();
	PointBuffer.Empty();
	ResetStrokeState();
}

bool UQuickSDFPaintTool::TryMakeStrokeSample(const FRay& Ray, FQuickSDFStrokeSample& OutSample)
{
	if (!TargetMeshSpatial.IsValid() || !TargetMesh.IsValid() || !Properties || !CurrentComponent) return false;

	const FTransform Transform = CurrentComponent->GetComponentTransform();
	const FRay LocalRay(Transform.InverseTransformPosition(Ray.Origin), Transform.InverseTransformVector(Ray.Direction));

	double HitDistance = 100000.0;
	int32 HitTID = -1;
	FVector3d BaryCoords(0.0, 0.0, 0.0);
	const bool bHit = TargetMeshSpatial->FindNearestHitTriangle(LocalRay, HitDistance, HitTID, BaryCoords);

	if (!bHit || HitTID < 0) return false;

	const UE::Geometry::FDynamicMeshUVOverlay* UVOverlay = TargetMesh->Attributes()->GetUVLayer(Properties->UVChannel);
	if (!UVOverlay || !UVOverlay->IsSetTriangle(HitTID)) return false;

	const UE::Geometry::FIndex3i TriUVs = UVOverlay->GetTriangle(HitTID);
	const FVector2f UV0 = UVOverlay->GetElement(TriUVs.A);
	const FVector2f UV1 = UVOverlay->GetElement(TriUVs.B);
	const FVector2f UV2 = UVOverlay->GetElement(TriUVs.C);

	OutSample.UV = UV0 * static_cast<float>(BaryCoords.X) +
		UV1 * static_cast<float>(BaryCoords.Y) +
		UV2 * static_cast<float>(BaryCoords.Z);
	OutSample.WorldPos = Transform.TransformPosition(LocalRay.PointAt(HitDistance));
	return true;
}

bool UQuickSDFPaintTool::TryMakePreviewStrokeSample(const FVector2D& ScreenPosition, FQuickSDFStrokeSample& OutSample) const
{
	UTextureRenderTarget2D* RT = GetActiveRenderTarget();
	if (!RT || !IsInPreviewBounds(ScreenPosition)) return false;

	OutSample.UV = ScreenToPreviewUV(ScreenPosition);
	OutSample.WorldPos = FVector3d(OutSample.UV.X * RT->SizeX, OutSample.UV.Y * RT->SizeY, 0.0);
	return true;
}

void UQuickSDFPaintTool::StampSample(const FQuickSDFStrokeSample& Sample)
{
	StampSamples({ Sample });
}

void UQuickSDFPaintTool::StampSamples(const TArray<FQuickSDFStrokeSample>& Samples)
{
	if (Samples.Num() == 0) return;

	UTextureRenderTarget2D* RT = GetActiveRenderTarget();
	if (!RT || !BrushMaskTexture) return;

	FTextureRenderTargetResource* RTResource = RT->GameThread_GetRenderTargetResource();
	if (!RTResource) return;

	const FVector2D RTSize(RT->SizeX, RT->SizeY);
	const FVector2D PixelSize = GetBrushPixelSize(RT);
	const FLinearColor PaintColor = IsPaintingShadow() ? FLinearColor::Black : FLinearColor::White;
	
	FCanvas Canvas(RTResource, nullptr, GetToolManager()->GetContextQueriesAPI()->GetCurrentEditingWorld(), GMaxRHIFeatureLevel);

	for (const FQuickSDFStrokeSample& Sample : Samples)
	{
		const FVector2D PixelPos(Sample.UV.X * RTSize.X, Sample.UV.Y * RTSize.Y);
		const FVector2D StampPos = PixelPos - (PixelSize * 0.5f);
		FCanvasTileItem BrushItem(StampPos, BrushMaskTexture->GetResource(), PixelSize, PaintColor);
		BrushItem.BlendMode = SE_BLEND_Translucent;
		Canvas.DrawItem(BrushItem);
	}
	
	Canvas.Flush_GameThread(false);

	ENQUEUE_RENDER_COMMAND(UpdateSDFPaintRTCommand)(
		[RTResource](FRHICommandListImmediate& RHICmdList)
		{
			TransitionAndCopyTexture(RHICmdList, RTResource->GetRenderTargetTexture(), RTResource->TextureRHI, {});
		});

	RefreshPreviewMaterial();
}

void UQuickSDFPaintTool::AppendStrokeSample(const FQuickSDFStrokeSample& Sample)
{
	if (PointBuffer.Num() > 0)
	{
		if (FVector3d::DistSquared(PointBuffer.Last().WorldPos, Sample.WorldPos) < 1e-8)
		{
			return;
		}
	}
	
	PointBuffer.Add(Sample);

	
	if (PointBuffer.Num() >= 4) {
		int32 L = PointBuffer.Num();
		StampInterpolatedSegment(PointBuffer[L-4], PointBuffer[L-3], PointBuffer[L-2], PointBuffer[L-1]);
        
		
		if (PointBuffer.Num() > 4) {
			PointBuffer.RemoveAt(0);
		}
	}
}

void UQuickSDFPaintTool::StampInterpolatedSegment(
    const FQuickSDFStrokeSample& P0,
    const FQuickSDFStrokeSample& P1,
    const FQuickSDFStrokeSample& P2,
    const FQuickSDFStrokeSample& P3)
{
    UTextureRenderTarget2D* RT = GetActiveRenderTarget();
    if (!RT) return;

    const double Spacing = GetCurrentStrokeSpacing(RT);
    if (Spacing <= KINDA_SMALL_NUMBER) return;
    
    const double Alpha = 0.5; 
    auto GetT = [Alpha](double t, const FVector3d& p1, const FVector3d& p2) {
        double d = FVector3d::DistSquared(p1, p2);
        return FMath::Pow(d, Alpha * 0.5) + t;
    };

    double t0 = 0.0;
    double t1 = GetT(t0, P0.WorldPos, P1.WorldPos);
    double t2 = GetT(t1, P1.WorldPos, P2.WorldPos);
    double t3 = GetT(t2, P2.WorldPos, P3.WorldPos);

    if (t2 - t1 < KINDA_SMALL_NUMBER) return;

    
    auto EvaluateSpline = [&](double t) {
        FQuickSDFStrokeSample Out;
        
        double a1 = (t1 - t) / (t1 - t0); double b1 = (t - t0) / (t1 - t0);
        double a2 = (t2 - t) / (t2 - t1); double b2 = (t - t1) / (t2 - t1);
        double a3 = (t3 - t) / (t3 - t2); double b3 = (t - t2) / (t3 - t2);
        double a12 = (t2 - t) / (t2 - t0); double b12 = (t - t0) / (t2 - t0);
        double a23 = (t3 - t) / (t3 - t1); double b23 = (t - t1) / (t3 - t1);

        FVector3d A1 = a1 * P0.WorldPos + b1 * P1.WorldPos;
        FVector3d A2 = a2 * P1.WorldPos + b2 * P2.WorldPos;
        FVector3d A3 = a3 * P2.WorldPos + b3 * P3.WorldPos;
        FVector3d B1 = a12 * A1 + b12 * A2;
        FVector3d B2 = a23 * A2 + b23 * A3;
        Out.WorldPos = a2 * B1 + b2 * B2;

        
        FVector2f U1 = a1 * P0.UV + b1 * P1.UV;
        FVector2f U2 = a2 * P1.UV + b2 * P2.UV;
        FVector2f U3 = a3 * P2.UV + b3 * P3.UV;
        FVector2f V1 = a12 * U1 + b12 * U2;
        FVector2f V2 = a23 * U2 + b23 * U3;
        Out.UV = (float)a2 * V1 + (float)b2 * V2;
        
        return Out;
    };

    
    
    const double SegmentLength = FVector3d::Distance(P1.WorldPos, P2.WorldPos);
    
    const int32 SubSteps = FMath::Clamp(FMath::CeilToInt(SegmentLength / (Spacing * 0.1)), 20, 1000);
    const double dt = (t2 - t1) / (double)SubSteps;

    TArray<FQuickSDFStrokeSample> Batch;
    FQuickSDFStrokeSample Prev = EvaluateSpline(t1);

    for (int32 i = 1; i <= SubSteps; ++i) {
        double currT = t1 + i * dt;
        FQuickSDFStrokeSample Curr = EvaluateSpline(currT);
        
        double Dist = FVector3d::Distance(Prev.WorldPos, Curr.WorldPos);
        AccumulatedDistance += Dist;

        while (AccumulatedDistance >= Spacing) {
            double Ratio = 1.0 - ((AccumulatedDistance - Spacing) / FMath::Max(Dist, 0.0001));
            FQuickSDFStrokeSample Stamp;
            Stamp.WorldPos = FMath::Lerp(Prev.WorldPos, Curr.WorldPos, Ratio);
            Stamp.UV = FMath::Lerp(Prev.UV, Curr.UV, (float)Ratio);
            Batch.Add(Stamp);
            AccumulatedDistance -= Spacing;
        }
        Prev = Curr;
    }
    StampSamples(Batch);
}

void UQuickSDFPaintTool::ResetStrokeState()
{
	StrokeSamples.Reset();
	WorldPosFilter.Reset();
	UVFilter.Reset();
	bHasLastStampedSample = false;
	bHasFilteredStrokeSample = false;
	DistanceSinceLastStamp = 0.0;
	ActiveStrokeInputMode = EQuickSDFStrokeInputMode::None;
	PendingStrokeInputMode = EQuickSDFStrokeInputMode::None;
}

void UQuickSDFPaintTool::OnPropertyModified(UObject* PropertySet, FProperty* Property)
{
	Super::OnPropertyModified(PropertySet, Property);

	if (PropertySet == Properties)
	{
		if (Property && (Property->GetFName() == GET_MEMBER_NAME_CHECKED(UQuickSDFToolProperties, EditAngleIndex) ||
				 Property->GetFName() == GET_MEMBER_NAME_CHECKED(UQuickSDFToolProperties, UVChannel)))
		{
			RefreshPreviewMaterial();
		}
	}
}

void UQuickSDFPaintTool::DrawHUD(FCanvas* Canvas, IToolsContextRenderAPI* RenderAPI)
{
    Super::DrawHUD(Canvas, RenderAPI);

    UTextureRenderTarget2D* RT = GetActiveRenderTarget();
    if (RT)
    {
        // 1. プレビューテクスチャの描画
        PreviewCanvasOrigin = FVector2D(10.0f, 10.0f);
        PreviewCanvasSize = FVector2D(256.0f, 256.0f);
        const FVector2D PreviewOrigin = GetPreviewOrigin();
        const FVector2D PreviewSize = GetPreviewSize();
        
        FCanvasTileItem TileItem(PreviewOrigin, RT->GetResource(), PreviewSize, FLinearColor::White);
        TileItem.BlendMode = SE_BLEND_Opaque;
        Canvas->DrawItem(TileItem);

        // --- ここからUV重ね合わせ表示の追加 ---
        if (TargetMesh.IsValid() && TargetMesh->HasAttributes())
        {
            const UE::Geometry::FDynamicMeshUVOverlay* UVOverlay = TargetMesh->Attributes()->GetUVLayer(Properties->UVChannel);
            if (UVOverlay)
            {
                // 線の色と不透明度を設定
                FLinearColor UVLineColor(0.0f, 1.0f, 0.0f, 0.3f); // 半透明の緑色

                for (int32 Tid : TargetMesh->TriangleIndicesItr())
                {
                    if (UVOverlay->IsSetTriangle(Tid))
                    {
                        UE::Geometry::FIndex3i UVIndices = UVOverlay->GetTriangle(Tid);
                        FVector2f UV0 = UVOverlay->GetElement(UVIndices.A);
                        FVector2f UV1 = UVOverlay->GetElement(UVIndices.B);
                        FVector2f UV2 = UVOverlay->GetElement(UVIndices.C);

                        // UV(0-1) を プレビューのピクセル座標に変換するラムダ関数
                        auto UVToScreen = [&](const FVector2f& UV) -> FVector2D {
                            return FVector2D(
                                PreviewOrigin.X + (double)UV.X * PreviewSize.X,
                                PreviewOrigin.Y + (double)UV.Y * PreviewSize.Y
                            );
                        };

                        FVector2D P0 = UVToScreen(UV0);
                        FVector2D P1 = UVToScreen(UV1);
                        FVector2D P2 = UVToScreen(UV2);

                        // 三角形の3辺を描画
                        FCanvasLineItem Line0(P0, P1); Line0.SetColor(UVLineColor);
                        Canvas->DrawItem(Line0);
                        FCanvasLineItem Line1(P1, P2); Line1.SetColor(UVLineColor);
                        Canvas->DrawItem(Line1);
                        FCanvasLineItem Line2(P2, P0); Line2.SetColor(UVLineColor);
                        Canvas->DrawItem(Line2);
                    }
                }
            }
        }
        // --- ここまで ---

        // 2. ボーダーの描画
        FCanvasBoxItem BorderItem(PreviewOrigin, PreviewSize);
        BorderItem.SetColor(IsInPreviewBounds(LastInputScreenPosition) ? FLinearColor::Yellow : FLinearColor::Gray);
        Canvas->DrawItem(BorderItem);
    }


	const FString PaintModeLabel = IsPaintingShadow() ? TEXT("Shadow") : TEXT("Light");
	const FString ShortcutLabel = bAdjustingBrushRadius ? TEXT("Ctrl+F active: move mouse, click to confirm") : TEXT("Shift: toggle paint, Ctrl+F: resize brush");
	FCanvasTextItem ModeText(FVector2D(10.0f, 275.0f), FText::FromString(FString::Printf(TEXT("Paint: %s"), *PaintModeLabel)), GEngine->GetSmallFont(), FLinearColor::White);
	ModeText.EnableShadow(FLinearColor::Black);
	Canvas->DrawItem(ModeText);

	FCanvasTextItem ShortcutText(FVector2D(10.0f, 292.0f), FText::FromString(ShortcutLabel), GEngine->GetSmallFont(), FLinearColor::White);
	ShortcutText.EnableShadow(FLinearColor::Black);
	Canvas->DrawItem(ShortcutText);

	if (bAdjustingBrushRadius)
	{
		FCanvasTextItem RadiusText(FVector2D(10.0f, 309.0f), FText::FromString(FString::Printf(TEXT("Brush Radius: %.1f"), BrushProperties ? BrushProperties->BrushRadius : 0.0f)), GEngine->GetSmallFont(), FLinearColor::Yellow);
		RadiusText.EnableShadow(FLinearColor::Black);
		Canvas->DrawItem(RadiusText);
	}
}

#undef LOCTEXT_NAMESPACE