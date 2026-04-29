#include "QuickSDFPaintTool.h"
#include "QuickSDFMeshComponentAdapter.h"
#include "QuickSDFToolSubsystem.h"
#include "QuickSDFAsset.h"
#include "SDFProcessor.h"
#include "BaseGizmos/BrushStampIndicator.h"
#include "InteractiveToolManager.h"
#include "ToolBuilderUtil.h"
#include "EngineUtils.h"
#include "Engine/DirectionalLight.h"
#include "CollisionQueryParams.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/Texture2D.h"
#include "BaseBehaviors/ClickDragBehavior.h"
#include "TargetInterfaces/PrimitiveComponentBackedTarget.h"
#include "TargetInterfaces/MeshDescriptionProvider.h"
#include "DynamicMesh/MeshTransforms.h"
#include "DynamicMesh/DynamicMeshAttributeSet.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Components/PrimitiveComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/SkinnedMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
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
#include "HAL/PlatformTime.h"
#include "InteractiveToolChange.h"
#include "Misc/ScopedSlowTask.h"
#include "Misc/MessageDialog.h"
#include "DesktopPlatformModule.h"
#include "Engine/Selection.h"
#include "Framework/Application/SlateApplication.h"
#include "IDesktopPlatform.h"
#include "Misc/DefaultValueHelper.h"
#include "Containers/Ticker.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetToolsModule.h"
#include "IAssetTools.h"
#include "Misc/PackageName.h"
#include "ObjectTools.h"

#if WITH_EDITOR
#include "Editor.h"
#include "IMaterialBakingModule.h"
#include "MaterialBakingStructures.h"
#endif

#define LOCTEXT_NAMESPACE "QuickSDFPaintTool"

namespace
{
constexpr double QuickSDFStrokeSpacingFactor = 0.12;
constexpr double QuickSDFMinSampleDistanceSq = 1.0e-6;
constexpr int32 QuickSDFBrushMaskResolution = 128;
constexpr int32 QuickSDFPreviewActionIncreaseBrush = static_cast<int32>(EStandardToolActions::BaseClientDefinedActionID) + 1;
constexpr float QuickSDFMinResizeSensitivity = 0.01f;
constexpr double QuickSDFSubPixelSpacing = 0.35;
constexpr double QuickSDFStrokeSmoothingMinAlpha = 0.15;
constexpr double QuickSDFStrokeSmoothingMaxAlpha = 0.85;
constexpr int32 QuickSDFBipolarDetectionStride = 500;
constexpr int32 QuickSDFDefaultAngleCount = 8;
constexpr int32 QuickSDFUVOverlaySupersample = 4;

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

uint32 GetTypeHash(const FQuickSDFUVEdgeKey& Key)
{
	return HashCombine(
		HashCombine(::GetTypeHash(Key.A.X), ::GetTypeHash(Key.A.Y)),
		HashCombine(::GetTypeHash(Key.B.X), ::GetTypeHash(Key.B.Y)));
}

FIntPoint QuantizeUVForOverlay(const FVector2f& UV)
{
	constexpr float QuantizeScale = 8192.0f;
	return FIntPoint(
		FMath::RoundToInt(UV.X * QuantizeScale),
		FMath::RoundToInt(UV.Y * QuantizeScale));
}

FQuickSDFUVEdgeKey MakeUVEdgeKey(const FVector2f& A, const FVector2f& B)
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

bool ShouldProcessMaskAngle(float RawAngle, bool bSymmetryMode)
{
	return !bSymmetryMode || (RawAngle >= -0.01f && RawAngle <= 90.01f);
}

TArray<int32> CollectProcessableMaskIndices(const UQuickSDFAsset& Asset, bool bSymmetryMode)
{
	TArray<int32> Indices;
	for (int32 Index = 0; Index < Asset.AngleDataList.Num(); ++Index)
	{
		const FQuickSDFAngleData& AngleData = Asset.AngleDataList[Index];
		if (AngleData.PaintRenderTarget && ShouldProcessMaskAngle(AngleData.Angle, bSymmetryMode))
		{
			Indices.Add(Index);
		}
	}
	return Indices;
}

bool TryBuildMaskData(
	const UQuickSDFPaintTool& Tool,
	UTextureRenderTarget2D* RenderTarget,
	float RawAngle,
	float MaxAngle,
	int32 OrigW,
	int32 OrigH,
	int32 Upscale,
	FMaskData& OutData)
{
	TArray<FColor> Pixels;
	if (!RenderTarget || !Tool.CaptureRenderTargetPixels(RenderTarget, Pixels))
	{
		return false;
	}

	const int32 HighW = OrigW * Upscale;
	const int32 HighH = OrigH * Upscale;
	TArray<uint8> GrayPixels = FSDFProcessor::ConvertToGrayscale(Pixels);
	TArray<uint8> UpscaledPixels = FSDFProcessor::UpscaleImage(GrayPixels, OrigW, OrigH, Upscale);

	OutData.SDF = FSDFProcessor::GenerateSDF(UpscaledPixels, HighW, HighH);
	OutData.TargetT = FMath::Clamp(FMath::Abs(RawAngle) / FMath::Max(MaxAngle, KINDA_SMALL_NUMBER), 0.0f, 1.0f);
	OutData.bIsOpposite = RawAngle < -0.01f;
	return true;
}

void SortMaskData(TArray<FMaskData>& MaskData)
{
	MaskData.Sort([](const FMaskData& A, const FMaskData& B)
	{
		if (A.bIsOpposite != B.bIsOpposite)
		{
			return !A.bIsOpposite;
		}
		return A.TargetT < B.TargetT;
	});
}

bool NeedsBipolarOutput(const TArray<FMaskData>& MaskData, int32 PixelCount)
{
	for (int32 Index = 0; Index < MaskData.Num(); ++Index)
	{
		if (MaskData[Index].bIsOpposite)
		{
			return true;
		}

		if (Index >= MaskData.Num() - 1)
		{
			continue;
		}

		const FMaskData& Current = MaskData[Index];
		const FMaskData& Next = MaskData[Index + 1];
		const int32 NumComparablePixels = FMath::Min(PixelCount, FMath::Min(Current.SDF.Num(), Next.SDF.Num()));
		for (int32 PixelIndex = 0; PixelIndex < NumComparablePixels; PixelIndex += QuickSDFBipolarDetectionStride)
		{
			if (Current.SDF[PixelIndex] > 0.0 && Next.SDF[PixelIndex] <= 0.0)
			{
				return true;
			}
		}
	}
	return false;
}

int32 GetQuickSDFPresetSize(EQuickSDFQualityPreset Preset)
{
	switch (Preset)
	{
	case EQuickSDFQualityPreset::Draft512:
		return 512;
	case EQuickSDFQualityPreset::High2048:
		return 2048;
	case EQuickSDFQualityPreset::Standard1024:
	default:
		return 1024;
	}
}

bool TryExtractAngleFromName(const FString& Name, float& OutAngle)
{
	TArray<float> Candidates;
	FString Token;

	auto FlushToken = [&]()
	{
		if (!Token.IsEmpty())
		{
			float ParsedValue = 0.0f;
			if (FDefaultValueHelper::ParseFloat(Token, ParsedValue) && ParsedValue >= 0.0f && ParsedValue <= 180.0f)
			{
				Candidates.Add(ParsedValue);
			}
			Token.Reset();
		}
	};

	for (int32 CharIndex = 0; CharIndex < Name.Len(); ++CharIndex)
	{
		const TCHAR Char = Name[CharIndex];
		const bool bCanStartNegative = Char == TEXT('-') && Token.IsEmpty() && CharIndex + 1 < Name.Len() && FChar::IsDigit(Name[CharIndex + 1]);
		if (FChar::IsDigit(Char) || Char == TEXT('.') || bCanStartNegative)
		{
			Token.AppendChar(Char);
		}
		else
		{
			FlushToken();
		}
	}
	FlushToken();

	if (Candidates.Num() == 0)
	{
		return false;
	}

	OutAngle = Candidates.Last();
	return true;
}

TArray<UTexture2D*> CollectSelectedTextureAssets()
{
	TArray<UTexture2D*> SelectedTextures;
	if (!GEditor)
	{
		return SelectedTextures;
	}

	if (USelection* SelectedObjects = GEditor->GetSelectedObjects())
	{
		for (FSelectionIterator It(*SelectedObjects); It; ++It)
		{
			if (UTexture2D* Texture = Cast<UTexture2D>(*It))
			{
				SelectedTextures.Add(Texture);
			}
		}
	}

	SelectedTextures.Sort([](const UTexture2D& A, const UTexture2D& B)
	{
		return A.GetName() < B.GetName();
	});
	return SelectedTextures;
}

bool HasImportedSourceMasks(const UQuickSDFAsset* Asset)
{
	if (!Asset)
	{
		return false;
	}

	for (const FQuickSDFAngleData& AngleData : Asset->AngleDataList)
	{
		if (AngleData.TextureMask)
		{
			return true;
		}
	}

	return false;
}

bool HasNonWhitePaintMasks(const UQuickSDFPaintTool& Tool, const UQuickSDFAsset* Asset)
{
	if (!Asset)
	{
		return false;
	}

	TArray<FColor> Pixels;
	for (const FQuickSDFAngleData& AngleData : Asset->AngleDataList)
	{
		if (!AngleData.PaintRenderTarget || !Tool.CaptureRenderTargetPixels(AngleData.PaintRenderTarget, Pixels))
		{
			continue;
		}

		for (const FColor& Pixel : Pixels)
		{
			if (Pixel.R < 250 || Pixel.G < 250 || Pixel.B < 250)
			{
				return true;
			}
		}
	}

	return false;
}

bool IsPersistentQuickSDFAsset(const UQuickSDFAsset* Asset)
{
	return Asset &&
		Asset->GetOutermost() != GetTransientPackage() &&
		FPackageName::IsValidLongPackageName(Asset->GetOutermost()->GetName());
}

TArray<FColor> MakeSolidPixels(int32 Width, int32 Height, const FLinearColor& FillColor)
{
	TArray<FColor> Pixels;
	if (Width <= 0 || Height <= 0)
	{
		return Pixels;
	}

	Pixels.Init(FillColor.ToFColor(false), Width * Height);
	return Pixels;
}

bool EnsureMaskGuids(UQuickSDFAsset* Asset)
{
	if (!Asset)
	{
		return false;
	}

	bool bChanged = false;
	for (FQuickSDFAngleData& AngleData : Asset->AngleDataList)
	{
		if (!AngleData.MaskGuid.IsValid())
		{
			AngleData.MaskGuid = FGuid::NewGuid();
			bChanged = true;
		}
	}
	return bChanged;
}

int32 FindAngleIndexByGuid(const UQuickSDFAsset* Asset, const FGuid& MaskGuid)
{
	if (!Asset || !MaskGuid.IsValid())
	{
		return INDEX_NONE;
	}

	for (int32 Index = 0; Index < Asset->AngleDataList.Num(); ++Index)
	{
		if (Asset->AngleDataList[Index].MaskGuid == MaskGuid)
		{
			return Index;
		}
	}
	return INDEX_NONE;
}

void CaptureMaskState(
	UQuickSDFPaintTool& Tool,
	UQuickSDFAsset* Asset,
	TArray<FGuid>& OutGuids,
	TArray<float>& OutAngles,
	TArray<UTexture2D*>& OutTextures,
	TArray<TArray<FColor>>& OutPixelsByMask)
{
	OutGuids.Reset();
	OutAngles.Reset();
	OutTextures.Reset();
	OutPixelsByMask.Reset();
	if (!Asset)
	{
		return;
	}

	EnsureMaskGuids(Asset);
	for (const FQuickSDFAngleData& AngleData : Asset->AngleDataList)
	{
		OutGuids.Add(AngleData.MaskGuid);
		OutAngles.Add(AngleData.Angle);
		OutTextures.Add(AngleData.TextureMask);

		TArray<FColor>& Pixels = OutPixelsByMask.AddDefaulted_GetRef();
		if (AngleData.PaintRenderTarget)
		{
			Tool.CaptureRenderTargetPixels(AngleData.PaintRenderTarget, Pixels);
		}
	}
}

void RestoreMaskStateOnNextTick(
	UQuickSDFPaintTool* Tool,
	const TArray<FGuid>& MaskGuids,
	const TArray<float>& Angles,
	const TArray<UTexture2D*>& Textures,
	const TArray<TArray<FColor>>& PixelsByMask)
{
	if (!Tool)
	{
		return;
	}

	Tool->RestoreMaskStateByGuid(MaskGuids, Angles, Textures, PixelsByMask);

	TWeakObjectPtr<UQuickSDFPaintTool> WeakTool(Tool);
	TArray<FGuid> DeferredGuids = MaskGuids;
	TArray<float> DeferredAngles = Angles;
	TArray<UTexture2D*> DeferredTextures = Textures;
	TArray<TArray<FColor>> DeferredPixelsByMask = PixelsByMask;
	FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda(
		[WeakTool, DeferredGuids = MoveTemp(DeferredGuids), DeferredAngles = MoveTemp(DeferredAngles), DeferredTextures = MoveTemp(DeferredTextures), DeferredPixelsByMask = MoveTemp(DeferredPixelsByMask)](float)
		{
			if (WeakTool.IsValid())
			{
				WeakTool->RestoreMaskStateByGuid(DeferredGuids, DeferredAngles, DeferredTextures, DeferredPixelsByMask);
			}
			return false;
		}));
}

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

}

void UQuickSDFPaintTool::GenerateSDF()
{
	if (!Properties)
	{
		return;
	}

	UQuickSDFToolSubsystem* Subsystem = GEditor->GetEditorSubsystem<UQuickSDFToolSubsystem>();
	if (!Subsystem || !Subsystem->GetActiveSDFAsset())
	{
		return;
	}

	UQuickSDFAsset* Asset = Subsystem->GetActiveSDFAsset();
	const int32 OrigW = Asset->Resolution.X;
	const int32 OrigH = Asset->Resolution.Y;
	if (OrigW <= 0 || OrigH <= 0)
	{
		return;
	}

	const TArray<int32> ProcessableIndices = CollectProcessableMaskIndices(*Asset, Properties->bSymmetryMode);
	if (ProcessableIndices.Num() == 0)
	{
		return;
	}

	// --- プログレスバーの初期化 ---
	// 工程：SDF生成(ValidIndices.Num()) + 合成(1) + 保存(1)
	FScopedSlowTask SlowTask(static_cast<float>(ProcessableIndices.Num()) + 2.0f, LOCTEXT("GenerateSDF", "Generating Multi-Channel SDF..."));
	SlowTask.MakeDialog(true);

	// --- 1. SDFデータの生成と収集 ---
	TArray<FMaskData> ProcessedData;
	const int32 Upscale = FMath::Clamp(Properties->UpscaleFactor, 1, 8);
	const int32 HighW = OrigW * Upscale;
	const int32 HighH = OrigH * Upscale;
	const float MaxAngle = Properties->bSymmetryMode ? 90.0f : 180.0f;

	for (int32 Index : ProcessableIndices)
	{
		const float RawAngle = Asset->AngleDataList[Index].Angle;
		// プログレスバー更新
		SlowTask.EnterProgressFrame(1.f, FText::Format(LOCTEXT("ProcessMask", "Processing Mask {0}..."), Index));
		if (SlowTask.ShouldCancel())
		{
			return;
		}

		FMaskData Data;
		if (TryBuildMaskData(*this, Asset->AngleDataList[Index].PaintRenderTarget, RawAngle, MaxAngle, OrigW, OrigH, Upscale, Data))
		{
			ProcessedData.Add(MoveTemp(Data));
		}
	}

	if (ProcessedData.Num() == 0)
	{
		return;
	}

	SortMaskData(ProcessedData);

	// --- 2. Bipolarの自動判定 ---
	const bool bNeedsBipolar = NeedsBipolarOutput(ProcessedData, HighW * HighH);
	const ESDFOutputFormat EffectiveFormat = bNeedsBipolar ? ESDFOutputFormat::Bipolar : ESDFOutputFormat::Monopolar;
	UE_LOG(LogTemp, Warning, TEXT("QuickSDF: Auto-Detected Format: %s"), bNeedsBipolar ? TEXT("BIPOLAR") : TEXT("MONOPOLAR"));

	// --- 3. 合成処理 ---
	SlowTask.EnterProgressFrame(1.f, LOCTEXT("CombineSDF", "Combining SDF Channels..."));
	if (SlowTask.ShouldCancel())
	{
		return;
	}

	TArray<FVector4f> CombinedField;
	FSDFProcessor::CombineSDFs(ProcessedData, CombinedField, HighW, HighH, EffectiveFormat, Properties->bSymmetryMode);

	// --- 4. 保存処理 ---
	SlowTask.EnterProgressFrame(1.f, LOCTEXT("SaveSDF", "Downscaling and Saving..."));
	if (SlowTask.ShouldCancel())
	{
		return;
	}

	TArray<FFloat16Color> FinalPixels = FSDFProcessor::DownscaleAndConvert(CombinedField, HighW, HighH, Upscale);
	FText SaveError;
	UTexture2D* FinalTexture = Subsystem->CreateSDFTexture(FinalPixels, OrigW, OrigH, Properties->SDFOutputFolder, Properties->SDFTextureName, EffectiveFormat, Properties->bOverwriteExistingSDF, &SaveError);
	if (FinalTexture)
	{
		Asset->Modify();
		Asset->FinalSDFTexture = FinalTexture;
		Asset->MarkPackageDirty();
	}
	else if (!SaveError.IsEmpty())
	{
		FMessageDialog::Open(EAppMsgType::Ok, SaveError);
	}
}

void UQuickSDFPaintTool::CreateQuickThresholdMap()
{
	if (!Properties)
	{
		return;
	}

	UQuickSDFToolSubsystem* Subsystem = GEditor->GetEditorSubsystem<UQuickSDFToolSubsystem>();
	if (!Subsystem)
	{
		return;
	}

	UQuickSDFAsset* Asset = Subsystem->GetActiveSDFAsset();
	if (!Asset)
	{
		Asset = NewObject<UQuickSDFAsset>(Subsystem);
		Asset->SetFlags(RF_Transactional);
		Subsystem->SetActiveSDFAsset(Asset);
		Properties->TargetAsset = Asset;
	}

	EnsureInitialMasksReady();
	Asset->InitializeRenderTargets(GetToolManager()->GetContextQueriesAPI()->GetCurrentEditingWorld());

	const bool bHasSourceMasks = HasImportedSourceMasks(Asset) || HasNonWhitePaintMasks(*this, Asset);
	if (!CurrentComponent.IsValid() && !bHasSourceMasks)
	{
		FMessageDialog::Open(EAppMsgType::Ok, LOCTEXT("QuickCreateNoTargetOrMasks", "Select a mesh or import edited masks before creating a threshold map."));
		return;
	}

	GenerateSDF();
	RefreshPreviewMaterial();
	bUseImportedMasksForQuickCreate = false;
}

void UQuickSDFPaintTool::ImportEditedMasks()
{
	if (!Properties)
	{
		return;
	}

	TArray<UTexture2D*> Textures = CollectSelectedTextureAssets();
	if (Textures.Num() == 0)
	{
		IDesktopPlatform* DesktopPlatform = FDesktopPlatformModule::Get();
		if (!DesktopPlatform)
		{
			return;
		}

		TArray<FString> SourceFilenames;
		const void* ParentWindowHandle = FSlateApplication::Get().FindBestParentWindowHandleForDialogs(nullptr);
		const bool bSelectedFiles = DesktopPlatform->OpenFileDialog(
			ParentWindowHandle,
			TEXT("Import Edited Masks"),
			FString(),
			FString(),
			TEXT("Image files|*.png;*.jpg;*.jpeg;*.bmp;*.tga;*.exr|All files|*.*"),
			EFileDialogFlags::Multiple,
			SourceFilenames);

		if (!bSelectedFiles || SourceFilenames.Num() == 0)
		{
			return;
		}

		UQuickSDFToolSubsystem* Subsystem = GEditor->GetEditorSubsystem<UQuickSDFToolSubsystem>();
		if (!Subsystem)
		{
			return;
		}

		FText ImportError;
		if (!Subsystem->ImportMaskFilesAsTextures(SourceFilenames, Properties->ImportedMaskFolder, Textures, &ImportError))
		{
			if (!ImportError.IsEmpty())
			{
				FMessageDialog::Open(EAppMsgType::Ok, ImportError);
			}
			return;
		}
	}

	ImportEditedMasksFromTextures(Textures);
}

bool UQuickSDFPaintTool::ImportEditedMasksFromTextures(const TArray<UTexture2D*>& InTextures)
{
	if (!Properties || InTextures.Num() == 0)
	{
		return false;
	}

	UQuickSDFToolSubsystem* Subsystem = GEditor->GetEditorSubsystem<UQuickSDFToolSubsystem>();
	if (!Subsystem)
	{
		return false;
	}

	UQuickSDFAsset* Asset = Subsystem->GetActiveSDFAsset();
	if (!Asset)
	{
		Asset = NewObject<UQuickSDFAsset>(Subsystem);
		Asset->SetFlags(RF_Transactional);
		Subsystem->SetActiveSDFAsset(Asset);
		Properties->TargetAsset = Asset;
	}

	struct FImportTextureItem
	{
		UTexture2D* Texture = nullptr;
		FString Name;
		float Angle = 0.0f;
		bool bHasAngle = false;
	};

	TArray<FImportTextureItem> Items;
	Items.Reserve(InTextures.Num());
	bool bAnyExplicitAngleAboveSymmetryRange = false;
	for (UTexture2D* Texture : InTextures)
	{
		if (!Texture)
		{
			continue;
		}

		FImportTextureItem Item;
		Item.Texture = Texture;
		Item.Name = Texture->GetName();
		Item.bHasAngle = TryExtractAngleFromName(Item.Name, Item.Angle);
		bAnyExplicitAngleAboveSymmetryRange |= Item.bHasAngle && Item.Angle > 90.01f;
		Items.Add(Item);
	}

	if (Items.Num() == 0)
	{
		return false;
	}

	Items.Sort([](const FImportTextureItem& A, const FImportTextureItem& B)
	{
		if (A.bHasAngle != B.bHasAngle)
		{
			return A.bHasAngle;
		}
		if (A.bHasAngle && !FMath::IsNearlyEqual(A.Angle, B.Angle))
		{
			return A.Angle < B.Angle;
		}
		return A.Name < B.Name;
	});

	GetToolManager()->BeginUndoTransaction(LOCTEXT("ImportEditedMasks", "Import Edited Quick SDF Masks"));
	Asset->Modify();
	Properties->Modify();

	if (bAnyExplicitAngleAboveSymmetryRange)
	{
		Properties->bSymmetryMode = false;
	}

	const float MaxAngle = Properties->bSymmetryMode ? 90.0f : 180.0f;
	int32 AutoAngleIndex = 0;
	int32 AutoAngleCount = 0;
	for (const FImportTextureItem& Item : Items)
	{
		if (!Item.bHasAngle)
		{
			++AutoAngleCount;
		}
	}

	for (FImportTextureItem& Item : Items)
	{
		if (!Item.bHasAngle)
		{
			Item.Angle = AutoAngleCount > 1
				? (static_cast<float>(AutoAngleIndex) / static_cast<float>(AutoAngleCount - 1)) * MaxAngle
				: 0.0f;
			++AutoAngleIndex;
		}
		else
		{
			Item.Angle = FMath::Clamp(Item.Angle, 0.0f, MaxAngle);
		}
	}

	Items.Sort([](const FImportTextureItem& A, const FImportTextureItem& B)
	{
		if (!FMath::IsNearlyEqual(A.Angle, B.Angle))
		{
			return A.Angle < B.Angle;
		}
		return A.Name < B.Name;
	});

	const int32 FirstWidth = Items[0].Texture ? Items[0].Texture->GetSizeX() : 0;
	const int32 FirstHeight = Items[0].Texture ? Items[0].Texture->GetSizeY() : 0;
	if (FirstWidth > 0 && FirstHeight > 0)
	{
		Properties->Resolution = FIntPoint(FirstWidth, FirstHeight);
		Asset->Resolution = Properties->Resolution;
	}

	Asset->UVChannel = Properties->UVChannel;
	Asset->AngleDataList.SetNum(Items.Num());
	Properties->NumAngles = Items.Num();
	Properties->TargetAngles.SetNum(Items.Num());
	Properties->TargetTextures.SetNum(Items.Num());

	for (int32 Index = 0; Index < Items.Num(); ++Index)
	{
		Asset->AngleDataList[Index].Angle = Items[Index].Angle;
		Asset->AngleDataList[Index].MaskGuid = FGuid::NewGuid();
		Asset->AngleDataList[Index].TextureMask = Items[Index].Texture;
		Asset->AngleDataList[Index].PaintRenderTarget = nullptr;
		Properties->TargetAngles[Index] = Items[Index].Angle;
		Properties->TargetTextures[Index] = Items[Index].Texture;
	}

	Properties->EditAngleIndex = FMath::Clamp(Properties->EditAngleIndex, 0, Properties->NumAngles - 1);
	Asset->InitializeRenderTargets(GetToolManager()->GetContextQueriesAPI()->GetCurrentEditingWorld());
	for (int32 Index = 0; Index < Items.Num(); ++Index)
	{
		Subsystem->DrawTextureToRenderTarget(Items[Index].Texture, Asset->AngleDataList[Index].PaintRenderTarget);
	}

	RefreshPreviewMaterial();
	bUseImportedMasksForQuickCreate = true;
	MarkMasksChanged();
	GetToolManager()->EndUndoTransaction();
	return true;
}

void UQuickSDFPaintTool::SaveQuickSDFAsset()
{
	if (!Properties)
	{
		return;
	}

	UQuickSDFToolSubsystem* Subsystem = GEditor ? GEditor->GetEditorSubsystem<UQuickSDFToolSubsystem>() : nullptr;
	UQuickSDFAsset* ActiveAsset = Subsystem ? Subsystem->GetActiveSDFAsset() : nullptr;
	if (!Subsystem || !ActiveAsset)
	{
		return;
	}

	FString CleanFolder = Properties->QuickSDFAssetFolder;
	while (CleanFolder.EndsWith(TEXT("/")))
	{
		CleanFolder.LeftChopInline(1);
	}

	if (!FPackageName::IsValidLongPackageName(CleanFolder))
	{
		FMessageDialog::Open(EAppMsgType::Ok, FText::Format(
			LOCTEXT("InvalidQuickSDFAssetFolder", "Invalid QuickSDF asset folder: {0}\nUse a content path such as /Game/QuickSDF_Assets."),
			FText::FromString(CleanFolder)));
		return;
	}

	const FString DesiredName = ObjectTools::SanitizeObjectName(
		Properties->QuickSDFAssetName.IsEmpty() ? FString(TEXT("DA_QuickSDF")) : Properties->QuickSDFAssetName);

	const bool bWasPersistentAsset = IsPersistentQuickSDFAsset(ActiveAsset);
	UQuickSDFAsset* SavedAsset = ActiveAsset;
	if (!bWasPersistentAsset)
	{
		IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();
		FString UniquePackageName;
		FString UniqueAssetName;
		AssetTools.CreateUniqueAssetName(CleanFolder / DesiredName, TEXT(""), UniquePackageName, UniqueAssetName);

		UPackage* Package = CreatePackage(*UniquePackageName);
		SavedAsset = NewObject<UQuickSDFAsset>(
			Package,
			UQuickSDFAsset::StaticClass(),
			*UniqueAssetName,
			RF_Public | RF_Standalone | RF_Transactional);
		if (!SavedAsset)
		{
			return;
		}

		FAssetRegistryModule::AssetCreated(SavedAsset);
		Properties->QuickSDFAssetName = UniqueAssetName;
		Properties->QuickSDFAssetFolder = FPackageName::GetLongPackagePath(UniquePackageName);
	}

	SavedAsset->Modify();
	ActiveAsset->Modify();
	EnsureMaskGuids(ActiveAsset);
	const TArray<FQuickSDFAngleData> SourceAngleData = ActiveAsset->AngleDataList;

	SavedAsset->Resolution = ActiveAsset->Resolution;
	SavedAsset->UVChannel = ActiveAsset->UVChannel;
	SavedAsset->FinalSDFTexture = ActiveAsset->FinalSDFTexture;
	SavedAsset->AngleDataList.SetNum(SourceAngleData.Num());

	const FString AssetFolder = FPackageName::GetLongPackagePath(SavedAsset->GetOutermost()->GetName());
	const FString MaskFolder = AssetFolder / FString::Printf(TEXT("%s_Masks"), *SavedAsset->GetName());

	for (int32 AngleIndex = 0; AngleIndex < SourceAngleData.Num(); ++AngleIndex)
	{
		const FQuickSDFAngleData& SourceData = SourceAngleData[AngleIndex];
		FQuickSDFAngleData& SavedData = SavedAsset->AngleDataList[AngleIndex];

		SavedData.Angle = SourceData.Angle;
		SavedData.MaskGuid = SourceData.MaskGuid.IsValid() ? SourceData.MaskGuid : FGuid::NewGuid();
		if (SavedAsset != ActiveAsset)
		{
			SavedData.PaintRenderTarget = nullptr;
		}

		UTexture2D* MaskTexture = SourceData.TextureMask;
		if (Properties->bSaveMaskTexturesWithAsset && SourceData.PaintRenderTarget)
		{
			const FString MaskName = FString::Printf(TEXT("T_%s_Mask_%02d"), *SavedAsset->GetName(), AngleIndex);
			FText Error;
			const bool bOverwriteMaskTexture = bWasPersistentAsset || Properties->bOverwriteExistingMasks;
			if (UTexture2D* ExportedTexture = Subsystem->CreateMaskTexture(SourceData.PaintRenderTarget, MaskFolder, MaskName, bOverwriteMaskTexture, &Error))
			{
				MaskTexture = ExportedTexture;
				if (ActiveAsset->AngleDataList.IsValidIndex(AngleIndex))
				{
					ActiveAsset->AngleDataList[AngleIndex].TextureMask = ExportedTexture;
				}
			}
			else if (!Error.IsEmpty())
			{
				FMessageDialog::Open(EAppMsgType::Ok, Error);
			}
		}

		SavedData.TextureMask = MaskTexture;
	}

	SavedAsset->MarkPackageDirty();
	SavedAsset->GetOutermost()->MarkPackageDirty();

	Subsystem->SetActiveSDFAsset(SavedAsset);
	Properties->TargetAsset = SavedAsset;
	SavedAsset->InitializeRenderTargets(GetToolManager()->GetContextQueriesAPI()->GetCurrentEditingWorld());
	for (int32 AngleIndex = 0; AngleIndex < SourceAngleData.Num(); ++AngleIndex)
	{
		if (!SavedAsset->AngleDataList.IsValidIndex(AngleIndex) ||
			!SourceAngleData[AngleIndex].PaintRenderTarget ||
			!SavedAsset->AngleDataList[AngleIndex].PaintRenderTarget)
		{
			continue;
		}

		TArray<FColor> SourcePixels;
		if (CaptureRenderTargetPixels(SourceAngleData[AngleIndex].PaintRenderTarget, SourcePixels))
		{
			RestoreRenderTargetPixels(SavedAsset->AngleDataList[AngleIndex].PaintRenderTarget, SourcePixels);
		}
	}
	SyncPropertiesFromActiveAsset();
	RefreshPreviewMaterial();
	MarkMasksChanged();

	if (GEditor)
	{
		TArray<UObject*> AssetsToSync;
		AssetsToSync.Add(SavedAsset);
		GEditor->SyncBrowserToObjects(AssetsToSync);
	}
}

void UQuickSDFPaintTool::EnsureInitialMasksReady()
{
	if (!Properties || !CurrentComponent.IsValid())
	{
		return;
	}

	if (InitialBakeComponents.Contains(CurrentComponent))
	{
		return;
	}

	UQuickSDFToolSubsystem* Subsystem = GEditor->GetEditorSubsystem<UQuickSDFToolSubsystem>();
	UQuickSDFAsset* Asset = Subsystem ? Subsystem->GetActiveSDFAsset() : nullptr;
	if (!Subsystem || !Asset)
	{
		return;
	}

	EnsureMaskGuids(Asset);
	Asset->InitializeRenderTargets(GetToolManager()->GetContextQueriesAPI()->GetCurrentEditingWorld());
	if (HasImportedSourceMasks(Asset) || HasNonWhitePaintMasks(*this, Asset))
	{
		InitialBakeComponents.Add(CurrentComponent);
		return;
	}
	InitialBakeComponents.Add(CurrentComponent);

	GetToolManager()->BeginUndoTransaction(LOCTEXT("InitialQuickSDFBake", "Initial Quick SDF Bake"));
	Asset->Modify();
	Properties->Modify();

	const int32 PresetSize = GetQuickSDFPresetSize(EQuickSDFQualityPreset::Standard1024);
	Properties->QualityPreset = EQuickSDFQualityPreset::Standard1024;
	Properties->Resolution = FIntPoint(PresetSize, PresetSize);
	Properties->UVChannel = 0;
	Properties->bSymmetryMode = true;
	Properties->bAutoSyncLight = true;
	Properties->bOverwriteExistingSDF = false;
	Properties->NumAngles = QuickSDFDefaultAngleCount;

	Asset->Resolution = Properties->Resolution;
	Asset->UVChannel = Properties->UVChannel;
	Asset->AngleDataList.SetNum(QuickSDFDefaultAngleCount);
	Properties->TargetAngles.SetNum(QuickSDFDefaultAngleCount);
	Properties->TargetTextures.SetNum(QuickSDFDefaultAngleCount);

	const float MaxAngle = 90.0f;
	for (int32 Index = 0; Index < QuickSDFDefaultAngleCount; ++Index)
	{
		const float Angle = QuickSDFDefaultAngleCount > 1
			? (static_cast<float>(Index) / static_cast<float>(QuickSDFDefaultAngleCount - 1)) * MaxAngle
			: 0.0f;
		Asset->AngleDataList[Index].Angle = Angle;
		Asset->AngleDataList[Index].MaskGuid = FGuid::NewGuid();
		Asset->AngleDataList[Index].TextureMask = nullptr;
		Asset->AngleDataList[Index].PaintRenderTarget = nullptr;
		Properties->TargetAngles[Index] = Angle;
		Properties->TargetTextures[Index] = nullptr;
	}

	Asset->InitializeRenderTargets(GetToolManager()->GetContextQueriesAPI()->GetCurrentEditingWorld());
	FillOriginalShadingAll();
	GetToolManager()->EndUndoTransaction();
}

void UQuickSDFPaintTool::RebakeCurrentMask()
{
	if (!Properties)
	{
		return;
	}

	FillOriginalShading(Properties->EditAngleIndex);
}

void UQuickSDFPaintTool::RebakeAllMasks()
{
	FillOriginalShadingAll();
}

void UQuickSDFPaintTool::CompleteToEightMasks()
{
	if (!Properties)
	{
		return;
	}

	UQuickSDFToolSubsystem* Subsystem = GEditor->GetEditorSubsystem<UQuickSDFToolSubsystem>();
	UQuickSDFAsset* Asset = Subsystem ? Subsystem->GetActiveSDFAsset() : nullptr;
	if (!Subsystem || !Asset || Asset->AngleDataList.Num() >= QuickSDFDefaultAngleCount)
	{
		return;
	}

	TArray<float> AddedAngles;
	const float MaxAngle = Properties->bSymmetryMode ? 90.0f : 180.0f;
	TArray<float> StandardAngles;
	for (int32 Index = 0; Index < QuickSDFDefaultAngleCount; ++Index)
	{
		StandardAngles.Add(QuickSDFDefaultAngleCount > 1
			? (static_cast<float>(Index) / static_cast<float>(QuickSDFDefaultAngleCount - 1)) * MaxAngle
			: 0.0f);
	}

	EnsureMaskGuids(Asset);
	Asset->InitializeRenderTargets(GetToolManager()->GetContextQueriesAPI()->GetCurrentEditingWorld());
	TArray<FGuid> BeforeGuids;
	TArray<float> BeforeAngles;
	TArray<UTexture2D*> BeforeTextures;
	TArray<TArray<FColor>> BeforePixelsByMask;
	CaptureMaskState(*this, Asset, BeforeGuids, BeforeAngles, BeforeTextures, BeforePixelsByMask);

	GetToolManager()->BeginUndoTransaction(LOCTEXT("CompleteToEightMasks", "Complete Quick SDF Masks to 8"));
	Asset->Modify();
	Properties->Modify();

	for (float CandidateAngle : StandardAngles)
	{
		if (Asset->AngleDataList.Num() >= QuickSDFDefaultAngleCount)
		{
			break;
		}

		bool bAlreadyCovered = false;
		for (const FQuickSDFAngleData& ExistingData : Asset->AngleDataList)
		{
			if (FMath::IsNearlyEqual(ExistingData.Angle, CandidateAngle, 0.5f))
			{
				bAlreadyCovered = true;
				break;
			}
		}

		if (!bAlreadyCovered)
		{
			FQuickSDFAngleData NewData;
			NewData.Angle = CandidateAngle;
			NewData.MaskGuid = FGuid::NewGuid();
			Asset->AngleDataList.Add(NewData);
			AddedAngles.Add(CandidateAngle);
		}
	}

	while (Asset->AngleDataList.Num() < QuickSDFDefaultAngleCount)
	{
		FQuickSDFAngleData NewData;
		NewData.Angle = StandardAngles.IsValidIndex(Asset->AngleDataList.Num())
			? StandardAngles[Asset->AngleDataList.Num()]
			: MaxAngle;
		NewData.MaskGuid = FGuid::NewGuid();
		Asset->AngleDataList.Add(NewData);
		AddedAngles.Add(NewData.Angle);
	}

	Asset->AngleDataList.Sort([](const FQuickSDFAngleData& A, const FQuickSDFAngleData& B)
	{
		return A.Angle < B.Angle;
	});
	Asset->InitializeRenderTargets(GetToolManager()->GetContextQueriesAPI()->GetCurrentEditingWorld());
	SyncPropertiesFromActiveAsset();

	for (float AddedAngle : AddedAngles)
	{
		int32 AddedIndex = INDEX_NONE;
		for (int32 Index = 0; Index < Asset->AngleDataList.Num(); ++Index)
		{
			if (FMath::IsNearlyEqual(Asset->AngleDataList[Index].Angle, AddedAngle, 0.5f))
			{
				AddedIndex = Index;
				break;
			}
		}

		if (AddedIndex == INDEX_NONE)
		{
			continue;
		}

		const bool bWasSuppressingMaskPixelUndo = bSuppressMaskPixelUndo;
		bSuppressMaskPixelUndo = true;
		if (CurrentComponent.IsValid())
		{
			FillOriginalShading(AddedIndex);
		}
		else
		{
			CopyNearestMaskToAngle(AddedIndex);
		}
		bSuppressMaskPixelUndo = bWasSuppressingMaskPixelUndo;
	}

	SyncPropertiesFromActiveAsset();
	MarkMasksChanged();

	TUniquePtr<FQuickSDFMaskStateChange> Change = MakeUnique<FQuickSDFMaskStateChange>();
	Change->BeforeGuids = MoveTemp(BeforeGuids);
	Change->BeforeAngles = MoveTemp(BeforeAngles);
	Change->BeforeTextures = MoveTemp(BeforeTextures);
	Change->BeforePixelsByMask = MoveTemp(BeforePixelsByMask);
	CaptureMaskState(*this, Asset, Change->AfterGuids, Change->AfterAngles, Change->AfterTextures, Change->AfterPixelsByMask);
	GetToolManager()->EmitObjectChange(this, MoveTemp(Change), LOCTEXT("CompleteToEightMaskState", "Restore Quick SDF Complete to 8 Mask State"));

	GetToolManager()->EndUndoTransaction();
}

void UQuickSDFPaintTool::RedistributeAnglesEvenly()
{
	if (!Properties)
	{
		return;
	}

	UQuickSDFToolSubsystem* Subsystem = GEditor->GetEditorSubsystem<UQuickSDFToolSubsystem>();
	UQuickSDFAsset* Asset = Subsystem ? Subsystem->GetActiveSDFAsset() : nullptr;
	if (!Asset || Asset->AngleDataList.Num() == 0)
	{
		return;
	}

	GetToolManager()->BeginUndoTransaction(LOCTEXT("RedistributeAnglesEvenly", "Redistribute Quick SDF Angles Evenly"));
	Asset->Modify();
	Properties->Modify();
	EnsureMaskGuids(Asset);

	Asset->AngleDataList.Sort([](const FQuickSDFAngleData& A, const FQuickSDFAngleData& B)
	{
		return A.Angle < B.Angle;
	});

	const float MaxAngle = Properties->bSymmetryMode ? 90.0f : 180.0f;
	const int32 NumAngles = Asset->AngleDataList.Num();
	for (int32 Index = 0; Index < NumAngles; ++Index)
	{
		Asset->AngleDataList[Index].Angle = NumAngles > 1
			? (static_cast<float>(Index) / static_cast<float>(NumAngles - 1)) * MaxAngle
			: 0.0f;
	}

	SyncPropertiesFromActiveAsset();
	GetToolManager()->EndUndoTransaction();
}

void UQuickSDFPaintTool::FillMaskColor(bool bFillAllAngles, const FLinearColor& FillColor)
{
	if (!Properties)
	{
		return;
	}

	UQuickSDFToolSubsystem* Subsystem = GEditor->GetEditorSubsystem<UQuickSDFToolSubsystem>();
	UQuickSDFAsset* Asset = Subsystem ? Subsystem->GetActiveSDFAsset() : nullptr;
	if (!Asset)
	{
		return;
	}

	TArray<int32> TargetIndices;
	if (bFillAllAngles)
	{
		for (int32 Index = 0; Index < Asset->AngleDataList.Num(); ++Index)
		{
			TargetIndices.Add(Index);
		}
	}
	else
	{
		TargetIndices.Add(FMath::Clamp(Properties->EditAngleIndex, 0, Asset->AngleDataList.Num() - 1));
	}

	GetToolManager()->BeginUndoTransaction(FillColor.Equals(FLinearColor::Black)
		? LOCTEXT("FillMasksBlack", "Fill Quick SDF Masks Black")
		: LOCTEXT("FillMasksWhite", "Fill Quick SDF Masks White"));
	Asset->Modify();
	Properties->Modify();
	EnsureMaskGuids(Asset);

	for (int32 AngleIndex : TargetIndices)
	{
		if (!Asset->AngleDataList.IsValidIndex(AngleIndex) || !Asset->AngleDataList[AngleIndex].PaintRenderTarget)
		{
			continue;
		}

		UTextureRenderTarget2D* RenderTarget = Asset->AngleDataList[AngleIndex].PaintRenderTarget;
		const TArray<FColor> Pixels = MakeSolidPixels(RenderTarget->SizeX, RenderTarget->SizeY, FillColor);
		ApplyPixelsWithUndo(AngleIndex, Pixels, FillColor.Equals(FLinearColor::Black)
			? LOCTEXT("FillMaskBlackChange", "Fill Quick SDF Mask Black")
			: LOCTEXT("FillMaskWhiteChange", "Fill Quick SDF Mask White"));
		Asset->AngleDataList[AngleIndex].TextureMask = nullptr;
		if (Properties->TargetTextures.IsValidIndex(AngleIndex))
		{
			Properties->TargetTextures[AngleIndex] = nullptr;
		}
	}

	GetToolManager()->EndUndoTransaction();
	MarkMasksChanged();
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
	Properties->SetFlags(RF_Transactional);
	AddToolPropertySource(Properties);

	if (UQuickSDFToolSubsystem* Subsystem = GEditor->GetEditorSubsystem<UQuickSDFToolSubsystem>())
	{
		if (UMeshComponent* TargetComponent = Subsystem->GetTargetMeshComponent())
		{
			Subsystem->SetTargetComponent(TargetComponent);
		}

		// サブシステムにアセットがない場合は仮で新規作成
		if (!Subsystem->GetActiveSDFAsset())
		{
			UQuickSDFAsset* NewAsset = NewObject<UQuickSDFAsset>(Subsystem);
			NewAsset->SetFlags(RF_Transactional);
			Subsystem->SetActiveSDFAsset(NewAsset);
		}

		UQuickSDFAsset* ActiveAsset = Subsystem->GetActiveSDFAsset();
		ActiveAsset->SetFlags(RF_Transactional);
		if (ActiveAsset->AngleDataList.Num() == 0)
		{
			ActiveAsset->Resolution = FIntPoint(1024, 1024);
			ActiveAsset->UVChannel = 0;
			float InitialMaxAngle = Properties->bSymmetryMode ? 90.0f : 180.0f;
			for (int32 i = 0; i < 8; ++i)
			{
				FQuickSDFAngleData Data;
				Data.Angle = (i / 7.0f) * InitialMaxAngle;
				Data.MaskGuid = FGuid::NewGuid();
				ActiveAsset->AngleDataList.Add(Data);
			}
		}
		EnsureMaskGuids(ActiveAsset);
		ActiveAsset->InitializeRenderTargets(GetToolManager()->GetContextQueriesAPI()->GetCurrentEditingWorld());

		// アセットのデータをツールのプロパティ(UI)に同期させる
		Properties->TargetAsset = ActiveAsset;
		Properties->Resolution = ActiveAsset->Resolution;
		Properties->UVChannel = ActiveAsset->UVChannel;
		Properties->NumAngles = ActiveAsset->AngleDataList.Num();
		
		Properties->TargetAngles.SetNum(Properties->NumAngles);
		Properties->TargetTextures.SetNum(Properties->NumAngles);
		for (int32 i = 0; i < Properties->NumAngles; ++i)
		{
			Properties->TargetAngles[i] = ActiveAsset->AngleDataList[i].Angle;
			Properties->TargetTextures[i] = ActiveAsset->AngleDataList[i].TextureMask;
		}
		ChangeTargetComponent(Subsystem->GetTargetMeshComponent());
	}

	BuildBrushMaskTexture();
	ResetStrokeState();

	BrushResizeBehavior = NewObject<UQuickSDFBrushResizeInputBehavior>(this);
	BrushResizeBehavior->Initialize(this);
	AddInputBehavior(BrushResizeBehavior);
}

void UQuickSDFPaintTool::OnTick(float DeltaTime)
{
	Super::OnTick(DeltaTime);
	
	if (UQuickSDFToolSubsystem* Subsystem = GEditor->GetEditorSubsystem<UQuickSDFToolSubsystem>())
	{
		if (CurrentComponent.Get() != Subsystem->GetTargetMeshComponent())
		{
			ChangeTargetComponent(Subsystem->GetTargetMeshComponent());
		}

		// Ensure render targets are initialized (critical after Undo/Redo since RTs are Transient)
		if (UQuickSDFAsset* ActiveAsset = Subsystem->GetActiveSDFAsset())
		{
			bool bAnyMissingRT = false;
			for (const FQuickSDFAngleData& Data : ActiveAsset->AngleDataList)
			{
				if (!Data.PaintRenderTarget)
				{
					bAnyMissingRT = true;
					break;
				}
			}
			if (bAnyMissingRT)
			{
				ActiveAsset->InitializeRenderTargets(GetToolManager()->GetContextQueriesAPI()->GetCurrentEditingWorld());
				MarkMasksChanged();
			}
		}
	}

	TryActivateQuickLine();
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
		PreviewMaterial->SetScalarParameterValue(TEXT("OverlayOriginalShadow"), Properties->bOverlayOriginalShadow);
	}

	if (PreviewBaseMaterial)
	{
		PreviewBaseMaterial->GetOutermost()->SetDirtyFlag(false);
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

void UQuickSDFPaintTool::ChangeTargetComponent(UMeshComponent* NewComponent)
{
	if (CurrentComponent.Get() == NewComponent)
	{
		return;
	}

	// 以前のコンポーネントのマテリアルを復元
	if (CurrentComponent.IsValid())
	{
		if (UStaticMeshComponent* StaticMeshComponent = Cast<UStaticMeshComponent>(CurrentComponent.Get()))
		{
			StaticMeshComponent->SetMaterialPreview(INDEX_NONE);
		}
		else if (USkinnedMeshComponent* SkinnedMeshComponent = Cast<USkinnedMeshComponent>(CurrentComponent.Get()))
		{
			SkinnedMeshComponent->SetMaterialPreview(INDEX_NONE);
		}

		for (int32 i = 0; i < OriginalMaterials.Num(); ++i)
		{
			if (CurrentComponent->GetNumMaterials() > i)
			{
				CurrentComponent->SetMaterial(i, OriginalMaterials[i]);
			}
		}
	}

	if (PreviewBaseMaterial)
	{
		PreviewBaseMaterial->GetOutermost()->SetDirtyFlag(false);
	}
	PreviewMaterial = nullptr;
	PreviewBaseMaterial = nullptr;
	OriginalMaterials.Empty();
	CurrentComponent = NewComponent;
	if (UQuickSDFToolSubsystem* Subsystem = GEditor->GetEditorSubsystem<UQuickSDFToolSubsystem>())
	{
		Subsystem->SetTargetComponent(NewComponent);
		SyncPropertiesFromActiveAsset();
	}
	TargetMeshSpatial.Reset();
	TargetMesh.Reset();
	TargetTriangleMaterialSlots.Reset();
	InvalidateUVOverlayCache();
	ResetStrokeState();

	if (!CurrentComponent.IsValid())
	{
		return;
	}

	TSharedPtr<UE::Geometry::FDynamicMesh3> TempMesh = MakeShared<UE::Geometry::FDynamicMesh3>();
	TUniquePtr<FQuickSDFMeshComponentAdapter> MeshAdapter = FQuickSDFMeshComponentAdapter::Make(CurrentComponent.Get());
	const bool bValidMeshLoaded = MeshAdapter.IsValid() && MeshAdapter->BuildDynamicMesh(*TempMesh, TargetTriangleMaterialSlots);

	if (!bValidMeshLoaded || TempMesh->TriangleCount() <= 0)
	{
		CurrentComponent.Reset();
		return;
	}

	TargetMesh = TempMesh;
	TargetMeshSpatial = MakeShared<UE::Geometry::FDynamicMeshAABBTree3>();
	TargetMeshSpatial->SetMesh(TargetMesh.Get(), true);

	PreviewBaseMaterial = LoadObject<UMaterialInterface>(nullptr, TEXT("/QuickSDFTool/Materials/M_PreviewMat.M_PreviewMat"));
	PreviewMaterial = PreviewBaseMaterial
		? UMaterialInstanceDynamic::Create(PreviewBaseMaterial, GetTransientPackage())
		: nullptr;

	if (!PreviewMaterial)
	{
		return;
	}
	PreviewMaterial->SetFlags(RF_Transient);
	if (PreviewBaseMaterial)
	{
		PreviewBaseMaterial->GetOutermost()->SetDirtyFlag(false);
	}

	// 新しいコンポーネントのマテリアルをプレビュー用に差し替え
	for (int32 i = 0; i < CurrentComponent->GetNumMaterials(); ++i)
	{
		OriginalMaterials.Add(CurrentComponent->GetMaterial(i));
		CurrentComponent->SetMaterial(i, PreviewMaterial);
	}

	ApplyTargetMaterialSlotIsolation();
	RefreshPreviewMaterial();
	EnsureInitialMasksReady();
}

void UQuickSDFPaintTool::Shutdown(EToolShutdownType ShutdownType)
{
	if (bBrushResizeTransactionOpen)
	{
		GetToolManager()->EndUndoTransaction();
		bBrushResizeTransactionOpen = false;
	}
	
	ChangeTargetComponent(nullptr);
	
	Super::Shutdown(ShutdownType); // ツール終了処理
}

bool UQuickSDFPaintTool::IsTriangleInTargetMaterialSlot(int32 TriangleID) const
{
	if (!Properties || Properties->TargetMaterialSlot < 0)
	{
		return true;
	}

	const int32* TriangleMaterialSlot = TargetTriangleMaterialSlots.Find(TriangleID);
	return TriangleMaterialSlot && *TriangleMaterialSlot == Properties->TargetMaterialSlot;
}

void UQuickSDFPaintTool::ApplyTargetMaterialSlotIsolation()
{
	if (!CurrentComponent.IsValid())
	{
		return;
	}

	const int32 MaterialPreviewIndex =
		(Properties && Properties->bIsolateTargetMaterialSlot &&
			Properties->TargetMaterialSlot >= 0 && CurrentComponent->GetNumMaterials() > Properties->TargetMaterialSlot)
			? Properties->TargetMaterialSlot
			: INDEX_NONE;

	if (UStaticMeshComponent* StaticMeshComponent = Cast<UStaticMeshComponent>(CurrentComponent.Get()))
	{
		StaticMeshComponent->SetMaterialPreview(MaterialPreviewIndex);
	}
	else if (USkinnedMeshComponent* SkinnedMeshComponent = Cast<USkinnedMeshComponent>(CurrentComponent.Get()))
	{
		SkinnedMeshComponent->SetMaterialPreview(MaterialPreviewIndex);
	}
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
	UTexture2D* TempTexture = CreateTransientTextureFromPixels(Pixels, RenderTarget->SizeX, RenderTarget->SizeY);
	return RestoreRenderTargetTexture(RenderTarget, TempTexture);
}

UTexture2D* UQuickSDFPaintTool::CreateTransientTextureFromPixels(const TArray<FColor>& Pixels, int32 Width, int32 Height) const
{
	if (Pixels.Num() != Width * Height) return nullptr;
	UTexture2D* TempTexture = UTexture2D::CreateTransient(Width, Height, PF_B8G8R8A8);
	if (!TempTexture || !TempTexture->GetPlatformData() || TempTexture->GetPlatformData()->Mips.Num() == 0) return nullptr;
	TempTexture->MipGenSettings = TMGS_NoMipmaps;
	TempTexture->Filter = TF_Nearest;
	TempTexture->SRGB = false;
	FTexture2DMipMap& Mip = TempTexture->GetPlatformData()->Mips[0];
	void* Data = Mip.BulkData.Lock(LOCK_READ_WRITE);
	FMemory::Memcpy(Data, Pixels.GetData(), Pixels.Num() * sizeof(FColor));
	Mip.BulkData.Unlock();
	TempTexture->UpdateResource();
	return TempTexture;
}

bool UQuickSDFPaintTool::RestoreRenderTargetTexture(UTextureRenderTarget2D* RenderTarget, UTexture2D* Texture) const
{
	if (!RenderTarget || !Texture) return false;
	FTextureRenderTargetResource* RTResource = RenderTarget->GameThread_GetRenderTargetResource();
	if (!RTResource) return false;
	FCanvas Canvas(RTResource, nullptr, GetToolManager()->GetContextQueriesAPI()->GetCurrentEditingWorld(), GMaxRHIFeatureLevel);
	FCanvasTileItem TileItem(FVector2D::ZeroVector, Texture->GetResource(), FVector2D(RenderTarget->SizeX, RenderTarget->SizeY), FLinearColor::White);
	TileItem.BlendMode = SE_BLEND_Opaque;
	Canvas.DrawItem(TileItem);
	Canvas.Flush_GameThread(true);
	ENQUEUE_RENDER_COMMAND(RestoreQuickSDFPaintRTCommand)(
		[RTResource](FRHICommandListImmediate& RHICmdList) {
			TransitionAndCopyTexture(RHICmdList, RTResource->GetRenderTargetTexture(), RTResource->TextureRHI, {});
		});
	return true;
}

bool UQuickSDFPaintTool::RestoreStrokeStartPixels() const
{
	UQuickSDFToolSubsystem* Subsystem = GEditor->GetEditorSubsystem<UQuickSDFToolSubsystem>();
	UQuickSDFAsset* Asset = Subsystem ? Subsystem->GetActiveSDFAsset() : nullptr;
	if (!Asset)
	{
		return false;
	}

	bool bRestoredAny = false;
	for (int32 Index = 0; Index < StrokeTransactionAngleIndices.Num() && Index < StrokeBeforeTexturesByAngle.Num(); ++Index)
	{
		const int32 AngleIndex = StrokeTransactionAngleIndices[Index];
		if (Asset->AngleDataList.IsValidIndex(AngleIndex) &&
			RestoreRenderTargetTexture(Asset->AngleDataList[AngleIndex].PaintRenderTarget, StrokeBeforeTexturesByAngle[Index]))
		{
			bRestoredAny = true;
		}
	}

	if (bRestoredAny)
	{
		const_cast<UQuickSDFPaintTool*>(this)->RefreshPreviewMaterial();
	}
	return bRestoredAny;
}

bool UQuickSDFPaintTool::ApplyRenderTargetPixels(int32 AngleIndex, const TArray<FColor>& Pixels)
{
	if (!Properties) return false;
	
	UQuickSDFToolSubsystem* Subsystem = GEditor->GetEditorSubsystem<UQuickSDFToolSubsystem>();
	if (!Subsystem || !Subsystem->GetActiveSDFAsset()) return false;

	UQuickSDFAsset* Asset = Subsystem->GetActiveSDFAsset();
	if (!Asset->AngleDataList.IsValidIndex(AngleIndex)) return false;
	
	const bool bRestored = RestoreRenderTargetPixels(Asset->AngleDataList[AngleIndex].PaintRenderTarget, Pixels);
	if (bRestored)
	{
		RefreshPreviewMaterial();
		MarkMasksChanged();
	}
	return bRestored;
}

bool UQuickSDFPaintTool::ApplyRenderTargetPixelsByGuid(const FGuid& AngleGuid, const TArray<FColor>& Pixels)
{
	UQuickSDFToolSubsystem* Subsystem = GEditor ? GEditor->GetEditorSubsystem<UQuickSDFToolSubsystem>() : nullptr;
	UQuickSDFAsset* Asset = Subsystem ? Subsystem->GetActiveSDFAsset() : nullptr;
	const int32 AngleIndex = FindAngleIndexByGuid(Asset, AngleGuid);
	if (AngleIndex == INDEX_NONE)
	{
		return false;
	}

	return ApplyRenderTargetPixels(AngleIndex, Pixels);
}

bool UQuickSDFPaintTool::ApplyTextureSlotChange(const FGuid& AngleGuid, int32 FallbackIndex, UTexture2D* Texture, const TArray<FColor>& Pixels)
{
	if (!Properties)
	{
		return false;
	}

	UQuickSDFToolSubsystem* Subsystem = GEditor ? GEditor->GetEditorSubsystem<UQuickSDFToolSubsystem>() : nullptr;
	UQuickSDFAsset* Asset = Subsystem ? Subsystem->GetActiveSDFAsset() : nullptr;
	if (!Subsystem || !Asset)
	{
		return false;
	}

	const int32 GuidIndex = FindAngleIndexByGuid(Asset, AngleGuid);
	const int32 AngleIndex = GuidIndex != INDEX_NONE ? GuidIndex : (AngleGuid.IsValid() ? INDEX_NONE : FallbackIndex);
	if (!Asset->AngleDataList.IsValidIndex(AngleIndex))
	{
		return false;
	}

	FQuickSDFAngleData& AngleData = Asset->AngleDataList[AngleIndex];
	if (!AngleData.PaintRenderTarget)
	{
		Asset->InitializeRenderTargets(GetToolManager()->GetContextQueriesAPI()->GetCurrentEditingWorld());
	}
	if (!AngleData.PaintRenderTarget)
	{
		return false;
	}

	AngleData.TextureMask = Texture;
	if (Properties->TargetTextures.IsValidIndex(AngleIndex))
	{
		Properties->TargetTextures[AngleIndex] = Texture;
	}

	bool bRestored = false;
	if (Pixels.Num() == AngleData.PaintRenderTarget->SizeX * AngleData.PaintRenderTarget->SizeY)
	{
		bRestored = RestoreRenderTargetPixels(AngleData.PaintRenderTarget, Pixels);
	}
	else if (Texture)
	{
		Subsystem->DrawTextureToRenderTarget(Texture, AngleData.PaintRenderTarget);
		bRestored = true;
	}
	else
	{
		Subsystem->ClearRenderTarget(AngleData.PaintRenderTarget);
		bRestored = true;
	}

	if (bRestored)
	{
		RefreshPreviewMaterial();
		MarkMasksChanged();
	}
	return bRestored;
}

void UQuickSDFPaintTool::RestoreMaskStateByGuid(const TArray<FGuid>& MaskGuids, const TArray<float>& Angles, const TArray<UTexture2D*>& Textures, const TArray<TArray<FColor>>& PixelsByMask)
{
	if (!Properties)
	{
		return;
	}

	UQuickSDFToolSubsystem* Subsystem = GEditor ? GEditor->GetEditorSubsystem<UQuickSDFToolSubsystem>() : nullptr;
	UQuickSDFAsset* Asset = Subsystem ? Subsystem->GetActiveSDFAsset() : nullptr;
	if (!Asset)
	{
		return;
	}

	Asset->AngleDataList.SetNum(MaskGuids.Num());
	for (int32 SnapshotIndex = 0; SnapshotIndex < MaskGuids.Num(); ++SnapshotIndex)
	{
		FQuickSDFAngleData& AngleData = Asset->AngleDataList[SnapshotIndex];
		AngleData.Angle = Angles.IsValidIndex(SnapshotIndex) ? Angles[SnapshotIndex] : 0.0f;
		AngleData.MaskGuid = MaskGuids[SnapshotIndex].IsValid() ? MaskGuids[SnapshotIndex] : FGuid::NewGuid();
		AngleData.TextureMask = Textures.IsValidIndex(SnapshotIndex) ? Textures[SnapshotIndex] : nullptr;
		AngleData.PaintRenderTarget = nullptr;
	}

	Asset->InitializeRenderTargets(GetToolManager()->GetContextQueriesAPI()->GetCurrentEditingWorld());

	for (int32 SnapshotIndex = 0; SnapshotIndex < MaskGuids.Num(); ++SnapshotIndex)
	{
		if (!Asset->AngleDataList.IsValidIndex(SnapshotIndex))
		{
			continue;
		}

		FQuickSDFAngleData& AngleData = Asset->AngleDataList[SnapshotIndex];
		if (PixelsByMask.IsValidIndex(SnapshotIndex) && PixelsByMask[SnapshotIndex].Num() > 0 && AngleData.PaintRenderTarget)
		{
			RestoreRenderTargetPixels(AngleData.PaintRenderTarget, PixelsByMask[SnapshotIndex]);
		}
		else if (AngleData.TextureMask && AngleData.PaintRenderTarget && Subsystem)
		{
			Subsystem->DrawTextureToRenderTarget(AngleData.TextureMask, AngleData.PaintRenderTarget);
		}
	}

	SyncPropertiesFromActiveAsset();
	RefreshPreviewMaterial();
	MarkMasksChanged();
}

bool UQuickSDFPaintTool::ApplyPixelsWithUndo(int32 AngleIndex, const TArray<FColor>& Pixels, const FText& ChangeDescription)
{
	if (!Properties)
	{
		return false;
	}

	UQuickSDFToolSubsystem* Subsystem = GEditor->GetEditorSubsystem<UQuickSDFToolSubsystem>();
	UQuickSDFAsset* Asset = Subsystem ? Subsystem->GetActiveSDFAsset() : nullptr;
	if (!Asset || !Asset->AngleDataList.IsValidIndex(AngleIndex))
	{
		return false;
	}
	EnsureMaskGuids(Asset);

	UTextureRenderTarget2D* RenderTarget = Asset->AngleDataList[AngleIndex].PaintRenderTarget;
	TArray<FColor> BeforePixels;
	if (!RenderTarget || !CaptureRenderTargetPixels(RenderTarget, BeforePixels) || BeforePixels.Num() != Pixels.Num())
	{
		return false;
	}

	if (BeforePixels == Pixels)
	{
		return false;
	}

	if (!RestoreRenderTargetPixels(RenderTarget, Pixels))
	{
		return false;
	}

	if (!bSuppressMaskPixelUndo)
	{
		TUniquePtr<FQuickSDFRenderTargetsChange> Change = MakeUnique<FQuickSDFRenderTargetsChange>();
		Change->AngleIndices.Add(AngleIndex);
		Change->AngleGuids.Add(Asset->AngleDataList[AngleIndex].MaskGuid);
		Change->BeforePixelsByAngle.Add(MoveTemp(BeforePixels));
		Change->AfterPixelsByAngle.Add(Pixels);
		GetToolManager()->EmitObjectChange(this, MoveTemp(Change), ChangeDescription);
	}
	RefreshPreviewMaterial();
	MarkMasksChanged();
	return true;
}

bool UQuickSDFPaintTool::CopyNearestMaskToAngle(int32 DestinationIndex)
{
	UQuickSDFToolSubsystem* Subsystem = GEditor->GetEditorSubsystem<UQuickSDFToolSubsystem>();
	UQuickSDFAsset* Asset = Subsystem ? Subsystem->GetActiveSDFAsset() : nullptr;
	if (!Asset || !Asset->AngleDataList.IsValidIndex(DestinationIndex))
	{
		return false;
	}

	const float DestinationAngle = Asset->AngleDataList[DestinationIndex].Angle;
	int32 SourceIndex = INDEX_NONE;
	float BestDistance = TNumericLimits<float>::Max();
	for (int32 Index = 0; Index < Asset->AngleDataList.Num(); ++Index)
	{
		if (Index == DestinationIndex || !Asset->AngleDataList[Index].PaintRenderTarget)
		{
			continue;
		}

		const float Distance = FMath::Abs(Asset->AngleDataList[Index].Angle - DestinationAngle);
		if (Distance < BestDistance)
		{
			BestDistance = Distance;
			SourceIndex = Index;
		}
	}

	UTextureRenderTarget2D* DestinationRT = Asset->AngleDataList[DestinationIndex].PaintRenderTarget;
	if (!DestinationRT)
	{
		return false;
	}

	TArray<FColor> Pixels;
	if (SourceIndex != INDEX_NONE && CaptureRenderTargetPixels(Asset->AngleDataList[SourceIndex].PaintRenderTarget, Pixels))
	{
		return ApplyPixelsWithUndo(DestinationIndex, Pixels, LOCTEXT("CopyNearestMaskChange", "Copy Nearest Quick SDF Mask"));
	}

	return ApplyPixelsWithUndo(
		DestinationIndex,
		MakeSolidPixels(DestinationRT->SizeX, DestinationRT->SizeY, FLinearColor::White),
		LOCTEXT("FillMissingMaskWhiteChange", "Fill Missing Quick SDF Mask White"));
}

void UQuickSDFPaintTool::SyncPropertiesFromActiveAsset()
{
	if (!Properties)
	{
		return;
	}

	UQuickSDFToolSubsystem* Subsystem = GEditor->GetEditorSubsystem<UQuickSDFToolSubsystem>();
	UQuickSDFAsset* Asset = Subsystem ? Subsystem->GetActiveSDFAsset() : nullptr;
	if (!Asset)
	{
		return;
	}
	EnsureMaskGuids(Asset);

	Properties->TargetAsset = Asset;
	Properties->Resolution = Asset->Resolution;
	Properties->UVChannel = Asset->UVChannel;
	Properties->NumAngles = Asset->AngleDataList.Num();
	Properties->TargetAngles.SetNum(Properties->NumAngles);
	Properties->TargetTextures.SetNum(Properties->NumAngles);

	for (int32 Index = 0; Index < Properties->NumAngles; ++Index)
	{
		Properties->TargetAngles[Index] = Asset->AngleDataList[Index].Angle;
		Properties->TargetTextures[Index] = Asset->AngleDataList[Index].TextureMask;
	}

	Properties->EditAngleIndex = FMath::Clamp(Properties->EditAngleIndex, 0, FMath::Max(Properties->NumAngles - 1, 0));
}

void UQuickSDFPaintTool::MarkMasksChanged()
{
	++MaskRevision;
}

void UQuickSDFPaintTool::BeginStrokeTransaction()
{
	if (bStrokeTransactionActive) return;
	
	StrokeBeforePixels.Reset();
	StrokeBeforePixelsByAngle.Reset();
	StrokeBeforeTexturesByAngle.Reset();
	const TArray<int32> CandidateAngleIndices = GetPaintTargetAngleIndices();
	StrokeTransactionAngleIndices.Reset();
	UQuickSDFToolSubsystem* Subsystem = GEditor ? GEditor->GetEditorSubsystem<UQuickSDFToolSubsystem>() : nullptr;
	UQuickSDFAsset* ActiveAsset = Subsystem ? Subsystem->GetActiveSDFAsset() : nullptr;
	EnsureMaskGuids(ActiveAsset);

	for (int32 AngleIndex : CandidateAngleIndices)
	{
		UQuickSDFAsset* Asset = ActiveAsset;
		if (!Asset || !Asset->AngleDataList.IsValidIndex(AngleIndex))
		{
			continue;
		}

		TArray<FColor> BeforePixels;
		if (CaptureRenderTargetPixels(Asset->AngleDataList[AngleIndex].PaintRenderTarget, BeforePixels))
		{
			StrokeTransactionAngleIndices.Add(AngleIndex);
			StrokeBeforeTexturesByAngle.Add(CreateTransientTextureFromPixels(BeforePixels, Asset->AngleDataList[AngleIndex].PaintRenderTarget->SizeX, Asset->AngleDataList[AngleIndex].PaintRenderTarget->SizeY));
			StrokeBeforePixelsByAngle.Add(MoveTemp(BeforePixels));
			if (StrokeBeforePixelsByAngle.Num() == 1)
			{
				StrokeBeforePixels = StrokeBeforePixelsByAngle[0];
				StrokeTransactionAngleIndex = AngleIndex;
			}
		}
	}

	bStrokeTransactionActive = StrokeBeforePixelsByAngle.Num() > 0;
}

void UQuickSDFPaintTool::EndStrokeTransaction()
{
	if (!bStrokeTransactionActive) return;
	
	UQuickSDFToolSubsystem* Subsystem = GEditor->GetEditorSubsystem<UQuickSDFToolSubsystem>();
	if (Properties && Subsystem && Subsystem->GetActiveSDFAsset())
	{
		UQuickSDFAsset* Asset = Subsystem->GetActiveSDFAsset();
		EnsureMaskGuids(Asset);
		TUniquePtr<FQuickSDFRenderTargetsChange> Change = MakeUnique<FQuickSDFRenderTargetsChange>();

		for (int32 Index = 0; Index < StrokeTransactionAngleIndices.Num() && Index < StrokeBeforePixelsByAngle.Num(); ++Index)
		{
			const int32 AngleIndex = StrokeTransactionAngleIndices[Index];
			if (!Asset->AngleDataList.IsValidIndex(AngleIndex))
			{
				continue;
			}

			TArray<FColor> AfterPixels;
			if (CaptureRenderTargetPixels(Asset->AngleDataList[AngleIndex].PaintRenderTarget, AfterPixels) &&
				AfterPixels.Num() == StrokeBeforePixelsByAngle[Index].Num() &&
				AfterPixels != StrokeBeforePixelsByAngle[Index])
			{
				Change->AngleIndices.Add(AngleIndex);
				Change->AngleGuids.Add(Asset->AngleDataList[AngleIndex].MaskGuid);
				Change->BeforePixelsByAngle.Add(MoveTemp(StrokeBeforePixelsByAngle[Index]));
				Change->AfterPixelsByAngle.Add(MoveTemp(AfterPixels));
			}
		}

		if (Change->AngleIndices.Num() > 0)
		{
			GetToolManager()->EmitObjectChange(this, MoveTemp(Change), LOCTEXT("QuickSDFPaintStrokeChange", "Quick SDF Paint Stroke"));
			MarkMasksChanged();
		}
	}
	
	bStrokeTransactionActive = false;
	StrokeTransactionAngleIndex = INDEX_NONE;
	StrokeTransactionAngleIndices.Reset();
	StrokeBeforePixels.Reset();
	StrokeBeforePixelsByAngle.Reset();
	StrokeBeforeTexturesByAngle.Reset();
}

UTextureRenderTarget2D* UQuickSDFPaintTool::GetActiveRenderTarget() const
{
	if (!Properties) return nullptr;

	UQuickSDFToolSubsystem* Subsystem = GEditor->GetEditorSubsystem<UQuickSDFToolSubsystem>();
	if (!Subsystem || !Subsystem->GetActiveSDFAsset()) return nullptr;

	UQuickSDFAsset* Asset = Subsystem->GetActiveSDFAsset();
	if (Asset->AngleDataList.Num() == 0) return nullptr;

	const int32 AngleIdx = FMath::Clamp(Properties->EditAngleIndex, 0, Asset->AngleDataList.Num() - 1);
	return Asset->AngleDataList[AngleIdx].PaintRenderTarget;
}

TArray<int32> UQuickSDFPaintTool::GetPaintTargetAngleIndices() const
{
	TArray<int32> TargetIndices;
	if (!Properties)
	{
		return TargetIndices;
	}

	UQuickSDFToolSubsystem* Subsystem = GEditor->GetEditorSubsystem<UQuickSDFToolSubsystem>();
	UQuickSDFAsset* Asset = Subsystem ? Subsystem->GetActiveSDFAsset() : nullptr;
	if (!Asset || Asset->AngleDataList.Num() == 0)
	{
		return TargetIndices;
	}

	if (Properties->bPaintAllAngles)
	{
		for (int32 Index = 0; Index < Asset->AngleDataList.Num(); ++Index)
		{
			if (Asset->AngleDataList[Index].PaintRenderTarget)
			{
				TargetIndices.Add(Index);
			}
		}
	}
	else
	{
		const int32 AngleIndex = FMath::Clamp(Properties->EditAngleIndex, 0, Asset->AngleDataList.Num() - 1);
		if (Asset->AngleDataList[AngleIndex].PaintRenderTarget)
		{
			TargetIndices.Add(AngleIndex);
		}
	}

	return TargetIndices;
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

	if (CurrentComponent.IsValid() && TargetMesh.IsValid())
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

double UQuickSDFPaintTool::GetToolCurrentTime() const
{
	if (const UWorld* World = GetToolManager()->GetContextQueriesAPI()->GetCurrentEditingWorld())
	{
		return World->GetTimeSeconds();
	}
	return FPlatformTime::Seconds();
}

void UQuickSDFPaintTool::UpdateQuickLineHoldState(const FVector2D& ScreenPosition)
{
	if (!Properties || !Properties->bEnableQuickLine || bQuickLineActive)
	{
		return;
	}

	const double MoveTolerance = FMath::Max(static_cast<double>(Properties->QuickLineMoveTolerance), 1.0);
	if (FVector2D::Distance(ScreenPosition, QuickLineHoldScreenPosition) > MoveTolerance)
	{
		QuickLineHoldScreenPosition = ScreenPosition;
		QuickLineLastMoveTime = GetToolCurrentTime();
	}
}

void UQuickSDFPaintTool::TryActivateQuickLine()
{
	if (!Properties || !Properties->bEnableQuickLine || bQuickLineActive || !bStrokeTransactionActive ||
		!bHasQuickLineStartSample || !bHasQuickLineEndSample ||
		ActiveStrokeInputMode == EQuickSDFStrokeInputMode::None ||
		QuickLineSourceSamples.Num() < 2)
	{
		return;
	}

	if (GetToolCurrentTime() - QuickLineLastMoveTime < static_cast<double>(Properties->QuickLineHoldTime))
	{
		return;
	}

	bQuickLineActive = true;
	PointBuffer.Reset();
	AccumulatedDistance = 0.0;
	RedrawQuickLinePreview();
}

void UQuickSDFPaintTool::RedrawQuickLinePreview()
{
	if (!bQuickLineActive || !bHasQuickLineStartSample || !bHasQuickLineEndSample)
	{
		return;
	}

	if (RestoreStrokeStartPixels())
	{
		StampQuickLineSegment(QuickLineStartSample, QuickLineEndSample);
	}
}

void UQuickSDFPaintTool::StampQuickLineSegment(const FQuickSDFStrokeSample& StartSample, const FQuickSDFStrokeSample& EndSample)
{
	if (QuickLineSourceSamples.Num() >= 2)
	{
		TArray<FQuickSDFStrokeSample> CurveSamples;
		CurveSamples.Reserve(QuickLineSourceSamples.Num());
		for (const FQuickSDFStrokeSample& SourceSample : QuickLineSourceSamples)
		{
			CurveSamples.Add(TransformQuickLineSample(SourceSample));
		}

		UTextureRenderTarget2D* RT = GetActiveRenderTarget();
		if (!RT)
		{
			return;
		}

		const FVector2D RTSize(RT->SizeX, RT->SizeY);
		const FVector2D BrushPixelSize = GetBrushPixelSize(RT);
		const double SpacingPixels = FMath::Max(static_cast<double>(FMath::Min(BrushPixelSize.X, BrushPixelSize.Y) * QuickSDFStrokeSpacingFactor), 1.0);
		TArray<FQuickSDFStrokeSample> ResampledSamples;
		ResampledSamples.Reserve(CurveSamples.Num() * 4);
		ResampledSamples.Add(CurveSamples[0]);

		for (int32 Index = 1; Index < CurveSamples.Num(); ++Index)
		{
			const FQuickSDFStrokeSample& Prev = CurveSamples[Index - 1];
			const FQuickSDFStrokeSample& Curr = CurveSamples[Index];
			const FVector2D PrevPixel(Prev.UV.X * RTSize.X, Prev.UV.Y * RTSize.Y);
			const FVector2D CurrPixel(Curr.UV.X * RTSize.X, Curr.UV.Y * RTSize.Y);
			const double SegmentPixels = FVector2D::Distance(PrevPixel, CurrPixel);
			const int32 StepCount = FMath::Max(FMath::CeilToInt(SegmentPixels / SpacingPixels), 1);
			for (int32 Step = 1; Step <= StepCount; ++Step)
			{
				const float Alpha = static_cast<float>(Step) / static_cast<float>(StepCount);
				FQuickSDFStrokeSample Sample;
				Sample.WorldPos = FMath::Lerp(Prev.WorldPos, Curr.WorldPos, static_cast<double>(Alpha));
				Sample.UV = FMath::Lerp(Prev.UV, Curr.UV, Alpha);
				Sample.LocalUVScale = FMath::Lerp(Prev.LocalUVScale, Curr.LocalUVScale, Alpha);
				ResampledSamples.Add(Sample);
			}
		}

		StampSamples(ResampledSamples);
		return;
	}

	UTextureRenderTarget2D* RT = GetActiveRenderTarget();
	if (!RT) return;

	const double Spacing = FMath::Max(GetCurrentStrokeSpacing(RT), 1.0);
	const double SegmentLength = FVector3d::Distance(StartSample.WorldPos, EndSample.WorldPos);
	const int32 StepCount = FMath::Max(FMath::CeilToInt(SegmentLength / Spacing), 1);

	TArray<FQuickSDFStrokeSample> LineSamples;
	LineSamples.Reserve(StepCount + 1);
	for (int32 Index = 0; Index <= StepCount; ++Index)
	{
		const float Alpha = static_cast<float>(Index) / static_cast<float>(StepCount);
		FQuickSDFStrokeSample Sample;
		Sample.WorldPos = FMath::Lerp(StartSample.WorldPos, EndSample.WorldPos, static_cast<double>(Alpha));
		Sample.UV = FMath::Lerp(StartSample.UV, EndSample.UV, Alpha);
		Sample.LocalUVScale = FMath::Lerp(StartSample.LocalUVScale, EndSample.LocalUVScale, Alpha);
		LineSamples.Add(Sample);
	}

	StampSamples(LineSamples);
}

FQuickSDFStrokeSample UQuickSDFPaintTool::TransformQuickLineSample(const FQuickSDFStrokeSample& SourceSample) const
{
	if (QuickLineSourceSamples.Num() < 2)
	{
		return SourceSample;
	}

	const FQuickSDFStrokeSample& SourceStart = QuickLineSourceSamples[0];
	const FQuickSDFStrokeSample& SourceEnd = QuickLineSourceSamples.Last();
	const FVector2f SourceAxis = SourceEnd.UV - SourceStart.UV;
	const FVector2f TargetAxis = QuickLineEndSample.UV - QuickLineStartSample.UV;
	const float SourceLength = SourceAxis.Size();
	const float TargetLength = TargetAxis.Size();

	FQuickSDFStrokeSample OutSample = SourceSample;
	if (SourceLength <= KINDA_SMALL_NUMBER || TargetLength <= KINDA_SMALL_NUMBER)
	{
		const FVector2f UVOffset = QuickLineEndSample.UV - SourceEnd.UV;
		OutSample.UV = SourceSample.UV + UVOffset;
		OutSample.WorldPos = SourceSample.WorldPos + (QuickLineEndSample.WorldPos - SourceEnd.WorldPos);
		return OutSample;
	}

	const FVector2f SourceUnit = SourceAxis / SourceLength;
	const FVector2f SourcePerp(-SourceUnit.Y, SourceUnit.X);
	const FVector2f TargetUnit = TargetAxis / TargetLength;
	const FVector2f TargetPerp(-TargetUnit.Y, TargetUnit.X);
	const FVector2f SourceOffset = SourceSample.UV - SourceStart.UV;
	const float Along = FVector2f::DotProduct(SourceOffset, SourceUnit);
	const float Across = FVector2f::DotProduct(SourceOffset, SourcePerp);
	const float Scale = TargetLength / SourceLength;

	OutSample.UV = QuickLineStartSample.UV + TargetUnit * (Along * Scale) + TargetPerp * (Across * Scale);
	OutSample.WorldPos = FVector3d(OutSample.UV.X, OutSample.UV.Y, 0.0);
	return OutSample;
}

void FQuickSDFRenderTargetChange::Apply(UObject* Object)
{
	if (UQuickSDFPaintTool* Tool = Cast<UQuickSDFPaintTool>(Object))
	{
		if (!AngleGuid.IsValid() || !Tool->ApplyRenderTargetPixelsByGuid(AngleGuid, AfterPixels))
		{
			if (!AngleGuid.IsValid())
			{
				Tool->ApplyRenderTargetPixels(AngleIndex, AfterPixels);
			}
		}
	}
}

void FQuickSDFRenderTargetChange::Revert(UObject* Object)
{
	if (UQuickSDFPaintTool* Tool = Cast<UQuickSDFPaintTool>(Object))
	{
		if (!AngleGuid.IsValid() || !Tool->ApplyRenderTargetPixelsByGuid(AngleGuid, BeforePixels))
		{
			if (!AngleGuid.IsValid())
			{
				Tool->ApplyRenderTargetPixels(AngleIndex, BeforePixels);
			}
		}
	}
}

void FQuickSDFRenderTargetsChange::Apply(UObject* Object)
{
	if (UQuickSDFPaintTool* Tool = Cast<UQuickSDFPaintTool>(Object))
	{
		for (int32 Index = 0; Index < AngleIndices.Num() && Index < AfterPixelsByAngle.Num(); ++Index)
		{
			const FGuid AngleGuid = AngleGuids.IsValidIndex(Index) ? AngleGuids[Index] : FGuid();
			if (!AngleGuid.IsValid() || !Tool->ApplyRenderTargetPixelsByGuid(AngleGuid, AfterPixelsByAngle[Index]))
			{
				if (!AngleGuid.IsValid())
				{
					Tool->ApplyRenderTargetPixels(AngleIndices[Index], AfterPixelsByAngle[Index]);
				}
			}
		}
	}
}

void FQuickSDFRenderTargetsChange::Revert(UObject* Object)
{
	if (UQuickSDFPaintTool* Tool = Cast<UQuickSDFPaintTool>(Object))
	{
		for (int32 Index = 0; Index < AngleIndices.Num() && Index < BeforePixelsByAngle.Num(); ++Index)
		{
			const FGuid AngleGuid = AngleGuids.IsValidIndex(Index) ? AngleGuids[Index] : FGuid();
			if (!AngleGuid.IsValid() || !Tool->ApplyRenderTargetPixelsByGuid(AngleGuid, BeforePixelsByAngle[Index]))
			{
				if (!AngleGuid.IsValid())
				{
					Tool->ApplyRenderTargetPixels(AngleIndices[Index], BeforePixelsByAngle[Index]);
				}
			}
		}
	}
}

void FQuickSDFTextureSlotChange::Apply(UObject* Object)
{
	if (UQuickSDFPaintTool* Tool = Cast<UQuickSDFPaintTool>(Object))
	{
		Tool->ApplyTextureSlotChange(AngleGuid, AngleIndex, AfterTexture, AfterPixels);
	}
}

void FQuickSDFTextureSlotChange::Revert(UObject* Object)
{
	if (UQuickSDFPaintTool* Tool = Cast<UQuickSDFPaintTool>(Object))
	{
		Tool->ApplyTextureSlotChange(AngleGuid, AngleIndex, BeforeTexture, BeforePixels);
	}
}

void FQuickSDFMaskStateChange::Apply(UObject* Object)
{
	if (UQuickSDFPaintTool* Tool = Cast<UQuickSDFPaintTool>(Object))
	{
		RestoreMaskStateOnNextTick(Tool, AfterGuids, AfterAngles, AfterTextures, AfterPixelsByMask);
	}
}

void FQuickSDFMaskStateChange::Revert(UObject* Object)
{
	if (UQuickSDFPaintTool* Tool = Cast<UQuickSDFPaintTool>(Object))
	{
		RestoreMaskStateOnNextTick(Tool, BeforeGuids, BeforeAngles, BeforeTextures, BeforePixelsByMask);
	}
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
	if (!Properties)
	{
		PendingStrokeInputMode = EQuickSDFStrokeInputMode::None;
		return FInputRayHit();
	}
	if (Properties->bShowPreview && GetActiveRenderTarget() && IsInPreviewBounds(PressPos.ScreenPosition))
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
	if (CurrentComponent.IsValid())
	{
		FCollisionQueryParams Params(SCENE_QUERY_STAT(QuickSDF), true);
		Params.bReturnFaceIndex = true;
		Params.bTraceComplex = true;

		FHitResult ComponentHit;
		const bool bComponentHit = CurrentComponent->LineTraceComponent(
			ComponentHit,
			Ray.Origin,
			Ray.Origin + Ray.Direction * 100000.0f,
			Params);

		if ((!Properties || Properties->TargetMaterialSlot < 0) && bComponentHit)
		{
			OutHit = ComponentHit;
			return true;
		}

		if (TargetMeshSpatial.IsValid() && TargetMesh.IsValid())
		{
			const FTransform Transform = CurrentComponent->GetComponentTransform();
			const FRay LocalRay(Transform.InverseTransformPosition(Ray.Origin), Transform.InverseTransformVector(Ray.Direction));

			double HitDistance = 100000.0;
			int32 HitTID = INDEX_NONE;
			FVector3d BaryCoords(0.0, 0.0, 0.0);
			if (TargetMeshSpatial->FindNearestHitTriangle(LocalRay, HitDistance, HitTID, BaryCoords) &&
				HitTID != INDEX_NONE &&
				IsTriangleInTargetMaterialSlot(HitTID))
			{
				const FVector LocalHitPosition = (FVector)LocalRay.PointAt(HitDistance);
				const FVector LocalNormal = (FVector)TargetMesh->GetTriNormal(HitTID);
				FVector WorldNormal = Transform.TransformVectorNoScale(LocalNormal).GetSafeNormal();
				if (FVector::DotProduct(WorldNormal, Ray.Direction) > 0.0)
				{
					WorldNormal *= -1.0;
				}

				if (bComponentHit)
				{
					OutHit = ComponentHit;
					OutHit.FaceIndex = HitTID;
					OutHit.Distance = FVector::Distance(Ray.Origin, OutHit.ImpactPoint);
				}
				else
				{
					OutHit.Component = CurrentComponent.Get();
					OutHit.Location = Transform.TransformPosition(LocalHitPosition);
					OutHit.ImpactPoint = OutHit.Location;
					OutHit.Normal = WorldNormal;
					OutHit.ImpactNormal = WorldNormal;
					OutHit.FaceIndex = HitTID;
					OutHit.Distance = FVector::Distance(Ray.Origin, OutHit.Location);
					OutHit.bBlockingHit = true;
				}

				LastBrushStamp.HitResult = OutHit;
				LastBrushStamp.WorldPosition = OutHit.ImpactPoint;
				LastBrushStamp.WorldNormal = OutHit.Normal;
				UpdateBrushStampIndicator();
				return true;
			}
		}
	}

	return false;
}

FInputRayHit UQuickSDFPaintTool::BeginHoverSequenceHitTest(const FInputDeviceRay& PressPos)
{
	LastInputScreenPosition = PressPos.ScreenPosition;

	FHitResult OutHit;
	if (HitTest(PressPos.WorldRay, OutHit))
	{
		LastBrushStamp.Radius = BrushProperties ? BrushProperties->BrushRadius : LastBrushStamp.Radius;
		LastBrushStamp.WorldPosition = OutHit.ImpactPoint;
		LastBrushStamp.WorldNormal = OutHit.Normal;
		LastBrushStamp.HitResult = OutHit;
		LastBrushStamp.Falloff = BrushProperties ? BrushProperties->BrushFalloffAmount : LastBrushStamp.Falloff;
		if (BrushStampIndicator)
		{
			BrushStampIndicator->bVisible = true;
		}
		UpdateBrushStampIndicator();
		return FInputRayHit(OutHit.Distance);
	}

	if (BrushStampIndicator)
	{
		BrushStampIndicator->bVisible = false;
	}
	return FInputRayHit();
}

bool UQuickSDFPaintTool::OnUpdateHover(const FInputDeviceRay& DevicePos)
{
	LastInputScreenPosition = DevicePos.ScreenPosition;
	if (bAdjustingBrushRadius)
	{
		UpdateBrushResizeFromCursor();
		return true;
	}
	FHitResult OutHit;
	if (HitTest(DevicePos.WorldRay, OutHit))
	{
		LastBrushStamp.Radius = BrushProperties ? BrushProperties->BrushRadius : LastBrushStamp.Radius;
		LastBrushStamp.WorldPosition = OutHit.ImpactPoint;
		LastBrushStamp.WorldNormal = OutHit.Normal;
		LastBrushStamp.HitResult = OutHit;
		LastBrushStamp.Falloff = BrushProperties ? BrushProperties->BrushFalloffAmount : LastBrushStamp.Falloff;
		if (BrushStampIndicator)
		{
			BrushStampIndicator->bVisible = true;
		}
		UpdateBrushStampIndicator();
		return true;
	}

	if (BrushStampIndicator)
	{
		BrushStampIndicator->bVisible = false;
	}
	return true;
}

void UQuickSDFPaintTool::OnEndHover()
{
	if (BrushStampIndicator)
	{
		BrushStampIndicator->bVisible = false;
	}
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
		if (BrushStampIndicator)
		{
			BrushStampIndicator->bVisible = true;
		}
		UpdateBrushStampIndicator();
		BeginStrokeTransaction(); 
		PointBuffer.Add(StartSample);
		QuickLineSourceSamples.Reset();
		QuickLineSourceSamples.Add(StartSample);
		QuickLineStartSample = StartSample;
		QuickLineEndSample = StartSample;
		bHasQuickLineStartSample = true;
		bHasQuickLineEndSample = true;
		QuickLineHoldScreenPosition = LastInputScreenPosition;
		QuickLineLastMoveTime = GetToolCurrentTime();
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
		if (BrushStampIndicator)
		{
			BrushStampIndicator->bVisible = true;
		}
		UpdateBrushStampIndicator();
		Sample = SmoothStrokeSample(Sample);
		QuickLineEndSample = Sample;
		bHasQuickLineEndSample = true;
		UpdateQuickLineHoldState(LastInputScreenPosition);
		if (bQuickLineActive)
		{
			RedrawQuickLinePreview();
			return;
		}
		if (QuickLineSourceSamples.Num() == 0 ||
			FVector3d::DistSquared(QuickLineSourceSamples.Last().WorldPos, Sample.WorldPos) > 1e-8)
		{
			QuickLineSourceSamples.Add(Sample);
		}
		AppendStrokeSample(Sample);
	}
}

void UQuickSDFPaintTool::OnEndDrag(const FRay& Ray)
{
	if (bQuickLineActive)
	{
		RedrawQuickLinePreview();
		EndStrokeTransaction();
		PointBuffer.Empty();
		ResetStrokeState();
		return;
	}

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
	if (!TargetMeshSpatial.IsValid() || !TargetMesh.IsValid() || !Properties || !CurrentComponent.IsValid()) return false;
	
	const FTransform Transform = CurrentComponent->GetComponentTransform();
	const FRay LocalRay(Transform.InverseTransformPosition(Ray.Origin), Transform.InverseTransformVector(Ray.Direction));

	double HitDistance = 100000.0;
	int32 HitTID = -1;
	FVector3d BaryCoords(0.0, 0.0, 0.0);
	const bool bHit = TargetMeshSpatial->FindNearestHitTriangle(LocalRay, HitDistance, HitTID, BaryCoords);

	if (!bHit || HitTID < 0) return false;
	if (!IsTriangleInTargetMaterialSlot(HitTID)) return false;

	UE::Geometry::FIndex3i TriV = TargetMesh->GetTriangle(HitTID);

	// 2. インデックスからローカル頂点座標を取得し、ワールド座標に変換
	FVector3d V0 = Transform.TransformPosition(TargetMesh->GetVertex(TriV.A));
	FVector3d V1 = Transform.TransformPosition(TargetMesh->GetVertex(TriV.B));
	FVector3d V2 = Transform.TransformPosition(TargetMesh->GetVertex(TriV.C));

	// UV座標を取得
	const UE::Geometry::FDynamicMeshUVOverlay* UVOverlay = TargetMesh->Attributes()->GetUVLayer(Properties->UVChannel);
	if (!UVOverlay || !UVOverlay->IsSetTriangle(HitTID)) return false;

	const UE::Geometry::FIndex3i TriUVs = UVOverlay->GetTriangle(HitTID);
	const FVector2f UV0 = UVOverlay->GetElement(TriUVs.A);
	const FVector2f UV1 = UVOverlay->GetElement(TriUVs.B);
	const FVector2f UV2 = UVOverlay->GetElement(TriUVs.C);

	double ScaleSum = 0.0;
	int32 ScaleCount = 0;
	auto AccumulateUVScale = [&ScaleSum, &ScaleCount](const FVector3d& WorldA, const FVector3d& WorldB, const FVector2f& UVA, const FVector2f& UVB)
	{
		const double WorldLength = FVector3d::Distance(WorldA, WorldB);
		const double UVLength = FVector2f::Distance(UVA, UVB);
		if (WorldLength > KINDA_SMALL_NUMBER && UVLength > KINDA_SMALL_NUMBER)
		{
			ScaleSum += UVLength / WorldLength;
			++ScaleCount;
		}
	};

	AccumulateUVScale(V0, V1, UV0, UV1);
	AccumulateUVScale(V1, V2, UV1, UV2);
	AccumulateUVScale(V2, V0, UV2, UV0);

	if (ScaleCount > 0)
	{
		OutSample.LocalUVScale = static_cast<float>(ScaleSum / static_cast<double>(ScaleCount));
	}
	else
	{
		OutSample.LocalUVScale = 1.0f / FMath::Max(static_cast<float>(TargetMesh->GetBounds().MaxDim()), KINDA_SMALL_NUMBER);
	}
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

	if (Properties && Properties->bPaintAllAngles && !bStampingAllPaintTargets)
	{
		const int32 PreviousEditAngleIndex = Properties->EditAngleIndex;
		bStampingAllPaintTargets = true;
		for (int32 AngleIndex : GetPaintTargetAngleIndices())
		{
			Properties->EditAngleIndex = AngleIndex;
			StampSamples(Samples);
		}
		Properties->EditAngleIndex = PreviousEditAngleIndex;
		bStampingAllPaintTargets = false;
		RefreshPreviewMaterial();
		return;
	}

	UTextureRenderTarget2D* RT = GetActiveRenderTarget();
	if (!RT || !BrushMaskTexture) return;

	FTextureRenderTargetResource* RTResource = RT->GameThread_GetRenderTargetResource();
	if (!RTResource) return;

	const FVector2D RTSize(RT->SizeX, RT->SizeY);
	const FLinearColor PaintColor = IsPaintingShadow() ? FLinearColor::Black : FLinearColor::White;
	
	FCanvas Canvas(RTResource, nullptr, GetToolManager()->GetContextQueriesAPI()->GetCurrentEditingWorld(), GMaxRHIFeatureLevel);

	for (const FQuickSDFStrokeSample& Sample : Samples)
	{
		FVector2D PixelSize;
		
		if (ActiveStrokeInputMode == EQuickSDFStrokeInputMode::TexturePreview)
		{
			// プレビュー表示（2D）の場合は以前の固定計算
			PixelSize = GetBrushPixelSize(RT);
		}
		else
		{
			const float BrushRadiusWorld = BrushProperties ? BrushProperties->BrushRadius : 10.0f;
			const float UVRadius = BrushRadiusWorld * FMath::Max(Sample.LocalUVScale, KINDA_SMALL_NUMBER);
			PixelSize = FVector2D(UVRadius * RTSize.X * 2.0f, UVRadius * RTSize.Y * 2.0f);
		}
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
        
    	double s1 = a1 * P0.LocalUVScale + b1 * P1.LocalUVScale;
    	double s2 = a2 * P1.LocalUVScale + b2 * P2.LocalUVScale;
    	double s3 = a3 * P2.LocalUVScale + b3 * P3.LocalUVScale;
    	double sv1 = a12 * s1 + b12 * s2;
    	double sv2 = a23 * s2 + b23 * s3;
    	Out.LocalUVScale = (float)(a2 * sv1 + b2 * sv2);
    	
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
        	Stamp.LocalUVScale = FMath::Lerp(Prev.LocalUVScale, Curr.LocalUVScale, (float)Ratio);
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
	bQuickLineActive = false;
	bHasQuickLineStartSample = false;
	bHasQuickLineEndSample = false;
	QuickLineStartSample = FQuickSDFStrokeSample();
	QuickLineEndSample = FQuickSDFStrokeSample();
	QuickLineSourceSamples.Reset();
	QuickLineHoldScreenPosition = FVector2D::ZeroVector;
	QuickLineLastMoveTime = 0.0;
}

void UQuickSDFPaintTool::OnPropertyModified(UObject* PropertySet, FProperty* Property)
{
	Super::OnPropertyModified(PropertySet, Property);

	if (PropertySet == Properties)
	{
		UQuickSDFToolSubsystem* Subsystem = GEditor->GetEditorSubsystem<UQuickSDFToolSubsystem>();
		UQuickSDFAsset* ActiveAsset = Subsystem ? Subsystem->GetActiveSDFAsset() : nullptr;

		if (ActiveAsset)
		{
			if (Property && Property->GetFName() == GET_MEMBER_NAME_CHECKED(UQuickSDFToolProperties, QualityPreset))
			{
				const int32 PresetSize = GetQuickSDFPresetSize(Properties->QualityPreset);
				Properties->Resolution = FIntPoint(PresetSize, PresetSize);
			}

			// 詳細パネルから別のアセットに切り替えた場合の処理
			if (Property && Property->GetFName() == GET_MEMBER_NAME_CHECKED(UQuickSDFToolProperties, TargetAsset))
			{
				if (Properties->TargetAsset != nullptr)
				{
					Subsystem->SetActiveSDFAsset(Properties->TargetAsset);
					ActiveAsset = Properties->TargetAsset;
					EnsureMaskGuids(ActiveAsset);
					ActiveAsset->InitializeRenderTargets(GetToolManager()->GetContextQueriesAPI()->GetCurrentEditingWorld());
					
					// 新しいアセットの値をUIにロード
					Properties->Resolution = ActiveAsset->Resolution;
					Properties->UVChannel = ActiveAsset->UVChannel;
					Properties->NumAngles = ActiveAsset->AngleDataList.Num();
					Properties->TargetAngles.SetNum(Properties->NumAngles);
					Properties->TargetTextures.SetNum(Properties->NumAngles);
					for (int32 i = 0; i < Properties->NumAngles; ++i)
					{
						Properties->TargetAngles[i] = ActiveAsset->AngleDataList[i].Angle;
						Properties->TargetTextures[i] = ActiveAsset->AngleDataList[i].TextureMask;
					}
					RefreshPreviewMaterial();
					MarkMasksChanged();
				}
			}

			// アングルの「数」が変わった場合、アセットの配列サイズを同期 (Linear reset has been disabled to support custom timeline editing)
			if (Property && Property->GetFName() == GET_MEMBER_NAME_CHECKED(UQuickSDFToolProperties, NumAngles))
			{
				/*
				Properties->TargetAngles.SetNum(Properties->NumAngles);
				ActiveAsset->AngleDataList.SetNum(Properties->NumAngles);
				for (int32 i = 0; i < Properties->NumAngles; ++i)
				{
					Properties->TargetAngles[i] = ((float)i / (float)FMath::Max(1, Properties->NumAngles - 1)) * 180.0f;
					ActiveAsset->AngleDataList[i].Angle = Properties->TargetAngles[i];
				}
				ActiveAsset->InitializeRenderTargets(GetToolManager()->GetContextQueriesAPI()->GetCurrentEditingWorld());
				*/
			}

			// 手動でアングルの「角度」が変わった場合
			if (Property && Property->GetFName() == GET_MEMBER_NAME_CHECKED(UQuickSDFToolProperties, TargetAngles))
			{
				for (int32 i = 0; i < FMath::Min(Properties->TargetAngles.Num(), ActiveAsset->AngleDataList.Num()); ++i)
				{
					ActiveAsset->AngleDataList[i].Angle = Properties->TargetAngles[i];
				}
			}
			// 手動で「テクスチャスロット」に画像がアサイン（または削除）された場合
			if (Property && Property->GetFName() == GET_MEMBER_NAME_CHECKED(UQuickSDFToolProperties, TargetTextures))
			{
				EnsureMaskGuids(ActiveAsset);
				for (int32 i = 0; i < FMath::Min(Properties->TargetTextures.Num(), ActiveAsset->AngleDataList.Num()); ++i)
				{
					// UIのテクスチャとアセットのテクスチャに差分があれば更新
					if (ActiveAsset->AngleDataList[i].TextureMask != Properties->TargetTextures[i])
					{
						FQuickSDFAngleData& AngleData = ActiveAsset->AngleDataList[i];
						if (!AngleData.PaintRenderTarget)
						{
							ActiveAsset->InitializeRenderTargets(GetToolManager()->GetContextQueriesAPI()->GetCurrentEditingWorld());
						}

						TArray<FColor> BeforePixels;
						if (AngleData.PaintRenderTarget)
						{
							CaptureRenderTargetPixels(AngleData.PaintRenderTarget, BeforePixels);
						}

						UTexture2D* BeforeTexture = AngleData.TextureMask;
						UTexture2D* AfterTexture = Properties->TargetTextures[i];
						ActiveAsset->Modify();
						AngleData.TextureMask = AfterTexture;

						// 画像がセットされたならキャンバスに転写、外されたなら白紙に戻す
						if (AfterTexture != nullptr)
						{
							Subsystem->DrawTextureToRenderTarget(AfterTexture, AngleData.PaintRenderTarget);
						}
						else
						{
							Subsystem->ClearRenderTarget(AngleData.PaintRenderTarget);
						}

						TArray<FColor> AfterPixels;
						if (AngleData.PaintRenderTarget)
						{
							CaptureRenderTargetPixels(AngleData.PaintRenderTarget, AfterPixels);
						}

						TUniquePtr<FQuickSDFTextureSlotChange> Change = MakeUnique<FQuickSDFTextureSlotChange>();
						Change->AngleIndex = i;
						Change->AngleGuid = AngleData.MaskGuid;
						Change->BeforeTexture = BeforeTexture;
						Change->AfterTexture = AfterTexture;
						Change->BeforePixels = MoveTemp(BeforePixels);
						Change->AfterPixels = MoveTemp(AfterPixels);
						GetToolManager()->EmitObjectChange(this, MoveTemp(Change), LOCTEXT("AssignQuickSDFMaskTexture", "Assign Quick SDF Mask Texture"));
					}
				}
				RefreshPreviewMaterial();
				MarkMasksChanged();
			}//TODO:後からテクスチャを追加する処理を実装する
			// 解像度の同期 — FIntPoint のサブプロパティ (X, Y) 変更も検出するため、名前ではなく値の差分で判定
			if (ActiveAsset->Resolution != Properties->Resolution)
			{
				ActiveAsset->Resolution = Properties->Resolution;
				// Force re-creation of render targets at the new resolution
				for (FQuickSDFAngleData& Data : ActiveAsset->AngleDataList)
				{
					Data.PaintRenderTarget = nullptr;
				}
				ActiveAsset->InitializeRenderTargets(GetToolManager()->GetContextQueriesAPI()->GetCurrentEditingWorld());
				RefreshPreviewMaterial();
			}

			if (Property && Property->GetFName() == GET_MEMBER_NAME_CHECKED(UQuickSDFToolProperties, UVChannel))
			{
				ActiveAsset->UVChannel = Properties->UVChannel;
				InvalidateUVOverlayCache();
				RefreshPreviewMaterial();
			}
		}

		if (Property && (Property->GetFName() == GET_MEMBER_NAME_CHECKED(UQuickSDFToolProperties, EditAngleIndex) ||
				 Property->GetFName() == GET_MEMBER_NAME_CHECKED(UQuickSDFToolProperties, bOverlayOriginalShadow)))
		{
			RefreshPreviewMaterial();
		}

		if (Property && (Property->GetFName() == GET_MEMBER_NAME_CHECKED(UQuickSDFToolProperties, TargetMaterialSlot) ||
				 Property->GetFName() == GET_MEMBER_NAME_CHECKED(UQuickSDFToolProperties, bIsolateTargetMaterialSlot)))
		{
			if (Property->GetFName() == GET_MEMBER_NAME_CHECKED(UQuickSDFToolProperties, TargetMaterialSlot) &&
				Properties->TargetMaterialSlot >= 0)
			{
				Properties->bIsolateTargetMaterialSlot = true;
			}
			InvalidateUVOverlayCache();
			ApplyTargetMaterialSlotIsolation();
		}
	}
}

void UQuickSDFPaintTool::AddKeyframe()
{
	if (!Properties) return;
	UQuickSDFToolSubsystem* Subsystem = GEditor->GetEditorSubsystem<UQuickSDFToolSubsystem>();
	if (!Subsystem || !Subsystem->GetActiveSDFAsset()) return;

	UQuickSDFAsset* Asset = Subsystem->GetActiveSDFAsset();
	EnsureMaskGuids(Asset);
	Asset->InitializeRenderTargets(GetToolManager()->GetContextQueriesAPI()->GetCurrentEditingWorld());
	TArray<FGuid> BeforeGuids;
	TArray<float> BeforeAngles;
	TArray<UTexture2D*> BeforeTextures;
	TArray<TArray<FColor>> BeforePixelsByMask;
	CaptureMaskState(*this, Asset, BeforeGuids, BeforeAngles, BeforeTextures, BeforePixelsByMask);

	GetToolManager()->BeginUndoTransaction(LOCTEXT("AddKeyframe", "Add Timeline Keyframe"));
	Asset->Modify();
	Properties->Modify();

	// Insert after current EditAngleIndex
	int32 InsertIndex = Properties->EditAngleIndex + 1;
	
	float NewAngle = 0.0f;
	if (Asset->AngleDataList.Num() == 0)
	{
		NewAngle = 0.0f;
		InsertIndex = 0;
	}
	else if (InsertIndex >= Asset->AngleDataList.Num())
	{
		NewAngle = FMath::Min(Asset->AngleDataList.Last().Angle + 10.0f, 180.0f);
	}
	else
	{
		float PrevAngle = Asset->AngleDataList[InsertIndex - 1].Angle;
		float NextAngle = Asset->AngleDataList[InsertIndex].Angle;
		NewAngle = (PrevAngle + NextAngle) * 0.5f;
	}

	FQuickSDFAngleData NewData;
	NewData.Angle = NewAngle;
	NewData.MaskGuid = FGuid::NewGuid();
	
	Asset->AngleDataList.Insert(NewData, InsertIndex);
	Properties->TargetAngles.Insert(NewAngle, InsertIndex);
	Properties->TargetTextures.Insert(nullptr, InsertIndex);
	Properties->NumAngles = Asset->AngleDataList.Num();

	Asset->InitializeRenderTargets(GetToolManager()->GetContextQueriesAPI()->GetCurrentEditingWorld());
	
	Properties->EditAngleIndex = InsertIndex;
	FProperty* Prop = Properties->GetClass()->FindPropertyByName(GET_MEMBER_NAME_CHECKED(UQuickSDFToolProperties, EditAngleIndex));
	OnPropertyModified(Properties, Prop);

	if (CurrentComponent.IsValid())
	{
		const bool bWasSuppressingMaskPixelUndo = bSuppressMaskPixelUndo;
		bSuppressMaskPixelUndo = true;
		FillOriginalShading(InsertIndex);
		bSuppressMaskPixelUndo = bWasSuppressingMaskPixelUndo;
	}
	else
	{
		const bool bWasSuppressingMaskPixelUndo = bSuppressMaskPixelUndo;
		bSuppressMaskPixelUndo = true;
		CopyNearestMaskToAngle(InsertIndex);
		bSuppressMaskPixelUndo = bWasSuppressingMaskPixelUndo;
	}
	MarkMasksChanged();

	TUniquePtr<FQuickSDFMaskStateChange> Change = MakeUnique<FQuickSDFMaskStateChange>();
	Change->BeforeGuids = MoveTemp(BeforeGuids);
	Change->BeforeAngles = MoveTemp(BeforeAngles);
	Change->BeforeTextures = MoveTemp(BeforeTextures);
	Change->BeforePixelsByMask = MoveTemp(BeforePixelsByMask);
	CaptureMaskState(*this, Asset, Change->AfterGuids, Change->AfterAngles, Change->AfterTextures, Change->AfterPixelsByMask);
	GetToolManager()->EmitObjectChange(this, MoveTemp(Change), LOCTEXT("AddKeyframeMaskState", "Restore Quick SDF Added Keyframe Mask State"));

	GetToolManager()->EndUndoTransaction();
}

void UQuickSDFPaintTool::RemoveKeyframe(int32 Index)
{
	if (!Properties) return;
	UQuickSDFToolSubsystem* Subsystem = GEditor->GetEditorSubsystem<UQuickSDFToolSubsystem>();
	if (!Subsystem || !Subsystem->GetActiveSDFAsset()) return;

	UQuickSDFAsset* Asset = Subsystem->GetActiveSDFAsset();
	
	if (Asset->AngleDataList.IsValidIndex(Index) && Asset->AngleDataList.Num() > 1)
	{
		EnsureMaskGuids(Asset);
		Asset->InitializeRenderTargets(GetToolManager()->GetContextQueriesAPI()->GetCurrentEditingWorld());
		TArray<FGuid> BeforeGuids;
		TArray<float> BeforeAngles;
		TArray<UTexture2D*> BeforeTextures;
		TArray<TArray<FColor>> BeforePixelsByMask;
		CaptureMaskState(*this, Asset, BeforeGuids, BeforeAngles, BeforeTextures, BeforePixelsByMask);

		GetToolManager()->BeginUndoTransaction(LOCTEXT("RemoveKeyframe", "Remove Timeline Keyframe"));
		Asset->Modify();
		Properties->Modify();

		Asset->AngleDataList.RemoveAt(Index);
		Properties->TargetAngles.RemoveAt(Index);
		Properties->TargetTextures.RemoveAt(Index);
		Properties->NumAngles = Asset->AngleDataList.Num();
		
		Properties->EditAngleIndex = FMath::Clamp(Properties->EditAngleIndex, 0, Properties->NumAngles - 1);
		
		Asset->InitializeRenderTargets(GetToolManager()->GetContextQueriesAPI()->GetCurrentEditingWorld());

		FProperty* Prop = Properties->GetClass()->FindPropertyByName(GET_MEMBER_NAME_CHECKED(UQuickSDFToolProperties, EditAngleIndex));
		OnPropertyModified(Properties, Prop);

		TUniquePtr<FQuickSDFMaskStateChange> Change = MakeUnique<FQuickSDFMaskStateChange>();
		Change->BeforeGuids = MoveTemp(BeforeGuids);
		Change->BeforeAngles = MoveTemp(BeforeAngles);
		Change->BeforeTextures = MoveTemp(BeforeTextures);
		Change->BeforePixelsByMask = MoveTemp(BeforePixelsByMask);
		CaptureMaskState(*this, Asset, Change->AfterGuids, Change->AfterAngles, Change->AfterTextures, Change->AfterPixelsByMask);
		GetToolManager()->EmitObjectChange(this, MoveTemp(Change), LOCTEXT("RemoveKeyframeMaskState", "Restore Quick SDF Removed Keyframe Mask State"));

		GetToolManager()->EndUndoTransaction();
		MarkMasksChanged();
	}
}

void UQuickSDFPaintTool::InvalidateUVOverlayCache()
{
	bUVOverlayDirty = true;
}

UTextureRenderTarget2D* UQuickSDFPaintTool::GetUVOverlayRenderTarget()
{
	if (!Properties || !Properties->bOverlayUV || !TargetMesh.IsValid() || !TargetMesh->HasAttributes())
	{
		return nullptr;
	}

	const FVector2D PreviewSize = GetPreviewSize();
	const int32 Width = FMath::Max(1, FMath::RoundToInt(PreviewSize.X)) * QuickSDFUVOverlaySupersample;
	const int32 Height = FMath::Max(1, FMath::RoundToInt(PreviewSize.Y)) * QuickSDFUVOverlaySupersample;
	const FIntPoint DesiredSize(Width, Height);

	const bool bSettingsChanged =
		CachedUVOverlaySize != DesiredSize ||
		CachedUVOverlayUVChannel != Properties->UVChannel ||
		CachedUVOverlayMaterialSlot != Properties->TargetMaterialSlot ||
		bCachedUVOverlayIsolateTargetMaterialSlot != Properties->bIsolateTargetMaterialSlot;

	if (bUVOverlayDirty || bSettingsChanged || !UVOverlayRenderTarget)
	{
		RebuildUVOverlayRenderTarget(Width, Height);
	}

	return UVOverlayRenderTarget;
}

void UQuickSDFPaintTool::RebuildUVOverlayRenderTarget(int32 Width, int32 Height)
{
	if (!Properties || !TargetMesh.IsValid() || !TargetMesh->HasAttributes())
	{
		UVOverlayRenderTarget = nullptr;
		return;
	}

	const UE::Geometry::FDynamicMeshUVOverlay* UVOverlay = TargetMesh->Attributes()->GetUVLayer(Properties->UVChannel);
	if (!UVOverlay)
	{
		UVOverlayRenderTarget = nullptr;
		return;
	}

	if (!UVOverlayRenderTarget || UVOverlayRenderTarget->SizeX != Width || UVOverlayRenderTarget->SizeY != Height)
	{
		UVOverlayRenderTarget = NewObject<UTextureRenderTarget2D>(this);
		UVOverlayRenderTarget->RenderTargetFormat = RTF_RGBA8;
		UVOverlayRenderTarget->ClearColor = FLinearColor::Transparent;
		UVOverlayRenderTarget->Filter = TF_Bilinear;
		UVOverlayRenderTarget->InitAutoFormat(Width, Height);
		UVOverlayRenderTarget->UpdateResourceImmediate(true);
	}

	FTextureRenderTargetResource* RTResource = UVOverlayRenderTarget->GameThread_GetRenderTargetResource();
	if (!RTResource)
	{
		return;
	}

	FCanvas Canvas(RTResource, nullptr, GetToolManager()->GetContextQueriesAPI()->GetCurrentEditingWorld(), GMaxRHIFeatureLevel);
	Canvas.Clear(FLinearColor::Transparent);

	auto UVToOverlay = [Width, Height](const FVector2f& UV) -> FVector2D
	{
		return FVector2D(static_cast<double>(UV.X) * Width, static_cast<double>(UV.Y) * Height);
	};

	TMap<FQuickSDFUVEdgeKey, int32> UniqueEdgeIndices;
	TArray<FQuickSDFUVOverlayEdge> UniqueEdges;
	auto AddUniqueEdge = [&UniqueEdgeIndices, &UniqueEdges](const FVector2f& A, const FVector2f& B)
	{
		const FQuickSDFUVEdgeKey EdgeKey = MakeUVEdgeKey(A, B);
		if (!UniqueEdgeIndices.Contains(EdgeKey))
		{
			UniqueEdgeIndices.Add(EdgeKey, UniqueEdges.Num());
			UniqueEdges.Add({ A, B });
		}
	};

	for (int32 Tid : TargetMesh->TriangleIndicesItr())
	{
		if (!IsTriangleInTargetMaterialSlot(Tid) || !UVOverlay->IsSetTriangle(Tid))
		{
			continue;
		}

		const UE::Geometry::FIndex3i UVIndices = UVOverlay->GetTriangle(Tid);
		const FVector2f UV0 = UVOverlay->GetElement(UVIndices.A);
		const FVector2f UV1 = UVOverlay->GetElement(UVIndices.B);
		const FVector2f UV2 = UVOverlay->GetElement(UVIndices.C);
		AddUniqueEdge(UV0, UV1);
		AddUniqueEdge(UV1, UV2);
		AddUniqueEdge(UV2, UV0);
	}

	const FLinearColor UVLineColor(0.0f, 0.42f, 0.18f, 0.045f);
	for (int32 EdgeIndex = 0; EdgeIndex < UniqueEdges.Num(); ++EdgeIndex)
	{
		const FQuickSDFUVOverlayEdge& Edge = UniqueEdges[EdgeIndex];
		FCanvasLineItem Line(UVToOverlay(Edge.A), UVToOverlay(Edge.B));
		Line.SetColor(UVLineColor);
		Line.BlendMode = SE_BLEND_Translucent;
		Line.LineThickness = 1.0f;
		Canvas.DrawItem(Line);
	}

	Canvas.Flush_GameThread(true);
	ENQUEUE_RENDER_COMMAND(UpdateQuickSDFUVOverlayRTCommand)(
		[RTResource](FRHICommandListImmediate& RHICmdList)
		{
			TransitionAndCopyTexture(RHICmdList, RTResource->GetRenderTargetTexture(), RTResource->TextureRHI, {});
		});

	CachedUVOverlaySize = FIntPoint(Width, Height);
	CachedUVOverlayUVChannel = Properties->UVChannel;
	CachedUVOverlayMaterialSlot = Properties->TargetMaterialSlot;
	bCachedUVOverlayIsolateTargetMaterialSlot = Properties->bIsolateTargetMaterialSlot;
	bUVOverlayDirty = false;
}

void UQuickSDFPaintTool::DrawHUD(FCanvas* Canvas, IToolsContextRenderAPI* RenderAPI)
{
    Super::DrawHUD(Canvas, RenderAPI);

	if (!Properties)
	{
		return;
	}

    if (Properties->bShowPreview)
    {
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

            if (Properties->bEnableOnionSkin)
            {
                UQuickSDFToolSubsystem* Subsystem = GEditor->GetEditorSubsystem<UQuickSDFToolSubsystem>();
                if (Subsystem && Subsystem->GetActiveSDFAsset())
                {
                    UQuickSDFAsset* ActiveAsset = Subsystem->GetActiveSDFAsset();
                    
                    // Previous frame (Red)
                    if (Properties->EditAngleIndex > 0 && ActiveAsset->AngleDataList.IsValidIndex(Properties->EditAngleIndex - 1))
                    {
                        UTextureRenderTarget2D* PrevRT = ActiveAsset->AngleDataList[Properties->EditAngleIndex - 1].PaintRenderTarget;
                        if (PrevRT)
                        {
                            FCanvasTileItem PrevTile(PreviewOrigin, PrevRT->GetResource(), PreviewSize, FLinearColor(1.0f, 0.0f, 0.0f, 0.5f));
                            PrevTile.BlendMode = SE_BLEND_Additive;
                            Canvas->DrawItem(PrevTile);
                        }
                    }
                    
                    // Next frame (Green)
                    if (Properties->EditAngleIndex < ActiveAsset->AngleDataList.Num() - 1 && ActiveAsset->AngleDataList.IsValidIndex(Properties->EditAngleIndex + 1))
                    {
                        UTextureRenderTarget2D* NextRT = ActiveAsset->AngleDataList[Properties->EditAngleIndex + 1].PaintRenderTarget;
                        if (NextRT)
                        {
                            FCanvasTileItem NextTile(PreviewOrigin, NextRT->GetResource(), PreviewSize, FLinearColor(0.0f, 1.0f, 0.0f, 0.5f));
                            NextTile.BlendMode = SE_BLEND_Additive;
                            Canvas->DrawItem(NextTile);
                        }
                    }
                }
            }
            
            if (UTextureRenderTarget2D* UVOverlayRT = GetUVOverlayRenderTarget())
            {
                FCanvasTileItem UVTile(PreviewOrigin, UVOverlayRT->GetResource(), PreviewSize, FLinearColor::White);
                UVTile.BlendMode = SE_BLEND_Translucent;
                Canvas->DrawItem(UVTile);
            }

            if (false && TargetMesh.IsValid() && TargetMesh->HasAttributes() && Properties->bOverlayUV)
            {
                const UE::Geometry::FDynamicMeshUVOverlay* UVOverlay = TargetMesh->Attributes()->GetUVLayer(Properties->UVChannel);
                if (UVOverlay)
                {
                    // 線の色と不透明度を設定
                    FLinearColor UVLineColor(0.0f, 1.0f, 0.0f, 0.3f); // 半透明の緑色

                    for (int32 Tid : TargetMesh->TriangleIndicesItr())
                    {
                        if (!IsTriangleInTargetMaterialSlot(Tid))
                        {
                            continue;
                        }

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

            // 2. ボーダーの描画
            FCanvasBoxItem BorderItem(PreviewOrigin, PreviewSize);
            BorderItem.SetColor(IsInPreviewBounds(LastInputScreenPosition) ? FLinearColor::Yellow : FLinearColor::Gray);
            Canvas->DrawItem(BorderItem);
        }
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

void UQuickSDFPaintTool::FillOriginalShading(int32 AngleIndex)
{
	if (!Properties || !CurrentComponent.IsValid()) return;
	
	UQuickSDFToolSubsystem* Subsystem = GEditor->GetEditorSubsystem<UQuickSDFToolSubsystem>();
	if (!Subsystem || !Subsystem->GetActiveSDFAsset()) return;

	UQuickSDFAsset* Asset = Subsystem->GetActiveSDFAsset();
	if (!Asset->AngleDataList.IsValidIndex(AngleIndex)) return;

	UMaterialInterface* BaseMat = LoadObject<UMaterialInterface>(nullptr, TEXT("/QuickSDFTool/Materials/M_OriginalShading.M_OriginalShading"));
	if (!BaseMat) return;

	UMaterialInstanceDynamic* MID = UMaterialInstanceDynamic::Create(BaseMat, this);
	MID->SetScalarParameterValue(TEXT("Angle"), Properties->TargetAngles[AngleIndex]);

	FMaterialData MatData;
	MatData.Material = MID;
	MatData.PropertySizes.Add(MP_EmissiveColor, Properties->Resolution);
	MatData.PropertySizes.Add(MP_BaseColor, Properties->Resolution);
	MatData.BackgroundColor = FColor::Black;
	
	MatData.bPerformShrinking = false;
	MatData.bPerformBorderSmear = false;

	UE_LOG(LogTemp, Log, TEXT("Starting bake for angle %d at resolution %dx%d"), AngleIndex, Properties->Resolution.X, Properties->Resolution.Y);

	FMeshData MeshData;
	TUniquePtr<FQuickSDFMeshComponentAdapter> MeshAdapter = FQuickSDFMeshComponentAdapter::Make(CurrentComponent.Get());
	if (!MeshAdapter.IsValid())
	{
		UE_LOG(LogTemp, Warning, TEXT("FillOriginalShading: Unsupported mesh component type"));
		return;
	}

	if (UStaticMeshComponent* SMC = Cast<UStaticMeshComponent>(CurrentComponent.Get()))
	{
		MeshData.Mesh = SMC->GetStaticMesh();
		if (MeshData.Mesh)
		{
			MeshData.MeshDescription = MeshData.Mesh->GetMeshDescription(0);
		}
		
		MeshData.PrimitiveData = FPrimitiveData(SMC);
	}
	else if (USkeletalMeshComponent* SkMC = Cast<USkeletalMeshComponent>(CurrentComponent.Get()))
	{
		if (USkeletalMesh* SkeletalMesh = SkMC->GetSkeletalMeshAsset())
		{
			MeshData.MeshDescription = SkeletalMesh->HasMeshDescription(0) ? SkeletalMesh->GetMeshDescription(0) : nullptr;
		}

		MeshData.PrimitiveData = FPrimitiveData(SkMC);
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("FillOriginalShading: Unsupported mesh component type"));
		return;
	}

	if (!MeshData.MeshDescription)
	{
		UE_LOG(LogTemp, Warning, TEXT("FillOriginalShading: Target mesh has no LOD0 mesh description"));
		return;
	}

	MeshAdapter->GetMaterialSlots(MeshData.MaterialIndices, Properties->TargetMaterialSlot);
	if (MeshData.MaterialIndices.Num() == 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("FillOriginalShading: Target material slot %d is not valid for this component"), Properties->TargetMaterialSlot);
		return;
	}
	
	MeshData.TextureCoordinateIndex = Properties->UVChannel;
	MeshData.TextureCoordinateBox = FBox2D(FVector2D(0, 0), FVector2D(1, 1));

	TArray<FBakeOutput> BakeOutputs;
	IMaterialBakingModule& Module = FModuleManager::GetModuleChecked<IMaterialBakingModule>("MaterialBaking");
	
	FScopedSlowTask SlowTask(1.0f, LOCTEXT("BakingShading", "Baking Original Shading..."));
	SlowTask.MakeDialog();

	TArray<FMaterialData*> MaterialSettings;
	MaterialSettings.Add(&MatData);
	TArray<FMeshData*> MeshSettings;
	MeshSettings.Add(&MeshData);

	// ベイク実行
	Module.BakeMaterials(MaterialSettings, MeshSettings, BakeOutputs);

	if (BakeOutputs.Num() > 0)
	{
		TArray<FColor> FinalPixels;
		bool bGotPixels = false;

		// Emissive をチェック (LDR)
		if (BakeOutputs[0].PropertyData.Contains(MP_EmissiveColor) && BakeOutputs[0].PropertyData[MP_EmissiveColor].Num() > 1)
		{
			FinalPixels = BakeOutputs[0].PropertyData[MP_EmissiveColor];
			bGotPixels = true;
		}
		// BaseColor をチェック (LDR)
		else if (BakeOutputs[0].PropertyData.Contains(MP_BaseColor) && BakeOutputs[0].PropertyData[MP_BaseColor].Num() > 1)
		{
			FinalPixels = BakeOutputs[0].PropertyData[MP_BaseColor];
			bGotPixels = true;
		}

		if (bGotPixels)
		{
			UE_LOG(LogTemp, Log, TEXT("Bake successful for angle %d, pixels: %d"), AngleIndex, FinalPixels.Num());
			ApplyPixelsWithUndo(AngleIndex, FinalPixels, LOCTEXT("RebakeQuickSDFMask", "Rebake Quick SDF Mask"));
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("Bake produced no pixels (or 1x1) for angle %d"), AngleIndex);
		}
	}
}

void UQuickSDFPaintTool::FillOriginalShadingAll()
{
	if (!Properties) return;
	
	UQuickSDFToolSubsystem* Subsystem = GEditor->GetEditorSubsystem<UQuickSDFToolSubsystem>();
	if (!Subsystem || !Subsystem->GetActiveSDFAsset()) return;

	UQuickSDFAsset* Asset = Subsystem->GetActiveSDFAsset();
	
	FScopedSlowTask SlowTask(Asset->AngleDataList.Num(), LOCTEXT("BakingAllShading", "Baking All Original Shading..."));
	SlowTask.MakeDialog();

	for (int32 i = 0; i < Asset->AngleDataList.Num(); ++i)
	{
		SlowTask.EnterProgressFrame(1.0f);
		FillOriginalShading(i);
	}
}

#undef LOCTEXT_NAMESPACE
