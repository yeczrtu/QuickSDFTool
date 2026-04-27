#include "QuickSDFPaintTool.h"
#include "QuickSDFToolSubsystem.h"
#include "QuickSDFAsset.h"
#include "SDFProcessor.h"
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

void UQuickSDFToolProperties::ExportToTexture()
{
	UQuickSDFPaintTool* Tool = Cast<UQuickSDFPaintTool>(GetOuter());
	if (!Tool) return;
	
	UQuickSDFToolSubsystem* Subsystem = GEditor->GetEditorSubsystem<UQuickSDFToolSubsystem>();
	if (!Subsystem || !Subsystem->GetActiveSDFAsset()) return;

	UQuickSDFAsset* Asset = Subsystem->GetActiveSDFAsset();
	IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();

	for (int32 i = 0; i < Asset->AngleDataList.Num(); ++i)
	{
		UTextureRenderTarget2D* RT = Asset->AngleDataList[i].PaintRenderTarget;
		if (!RT) continue;

		TArray<FColor> Pixels;
		if (!Subsystem->CaptureRenderTargetPixels(RT, Pixels)) continue;

		FString FolderPath = TEXT("/Game/QuickSDF_Exports");
		FString AssetName = FString::Printf(TEXT("T_QuickSDF_Angle%d"), i);
		
		UTexture2D* NewTex = Cast<UTexture2D>(AssetTools.CreateAsset(AssetName, FolderPath, UTexture2D::StaticClass(), nullptr));
        
		if (NewTex)
		{
			NewTex->Source.Init(RT->SizeX, RT->SizeY, 1, 1, TSF_BGRA8);
			void* MipData = NewTex->Source.LockMip(0);
			FMemory::Memcpy(MipData, Pixels.GetData(), Pixels.Num() * sizeof(FColor));
			NewTex->Source.UnlockMip(0);

			NewTex->SRGB = RT->SRGB;
			NewTex->CompressionSettings = TC_Default;
			NewTex->MipGenSettings = TMGS_NoMipmaps;
			NewTex->PostEditChange();
			NewTex->GetPackage()->MarkPackageDirty();

			// エクスポートしたテクスチャをアセットに紐づけておくことで、次回ロード時に復元されます
			Asset->AngleDataList[i].TextureMask = NewTex;

			TArray<UObject*> AssetsToSync;
			AssetsToSync.Add(NewTex);
			AssetTools.SyncBrowserToAssets(AssetsToSync);
		}
	}
	Asset->Modify(); // アセットを更新状態にする
}

void UQuickSDFToolProperties::GenerateSDFThresholdMap()
{
	UQuickSDFPaintTool* Tool = Cast<UQuickSDFPaintTool>(GetOuter());
	if (Tool)
	{
		Tool->GenerateSDF();
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

}

void UQuickSDFPaintTool::GenerateSDF()
{
	if (!Properties) return;
	
	UQuickSDFToolSubsystem* Subsystem = GEditor->GetEditorSubsystem<UQuickSDFToolSubsystem>();
	if (!Subsystem || !Subsystem->GetActiveSDFAsset()) return;

	UQuickSDFAsset* Asset = Subsystem->GetActiveSDFAsset();

	TArray<FMaskData> ProcessedData;
	int32 OrigW = Asset->Resolution.X;
	int32 OrigH = Asset->Resolution.Y;
	int32 Upscale = FMath::Clamp(Properties->UpscaleFactor, 1, 8);
	int32 HighW = OrigW * Upscale;
	int32 HighH = OrigH * Upscale;

	TArray<int32> ValidIndices;
	bool bSymmetry = Properties->bSymmetryMode;
	float MaxAngle = bSymmetry ? 90.0f : 180.0f;

	for (int32 i = 0; i < Asset->AngleDataList.Num(); ++i)
	{
		if (i < Properties->TargetAngles.Num() && Asset->AngleDataList[i].PaintRenderTarget) 
		{
			if (bSymmetry && Properties->TargetAngles[i] > MaxAngle) continue;
			ValidIndices.Add(i);
		}
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

		UTextureRenderTarget2D* RT = Asset->AngleDataList[Index].PaintRenderTarget;
		TArray<FColor> Pixels;
		if (!CaptureRenderTargetPixels(RT, Pixels)) continue;

		TArray<uint8> GrayPixels = FSDFProcessor::ConvertToGrayscale(Pixels);
		TArray<uint8> UpscaledPixels = FSDFProcessor::UpscaleImage(GrayPixels, OrigW, OrigH, Upscale);
		TArray<double> SDF = FSDFProcessor::GenerateSDF(UpscaledPixels, HighW, HighH);

		FMaskData Data;
		Data.SDF = MoveTemp(SDF);
		Data.TargetT = Properties->TargetAngles[Index] / MaxAngle;
		ProcessedData.Add(MoveTemp(Data));
	}

	if (ProcessedData.Num() == 0) return;

	SlowTask.EnterProgressFrame(1.f, LOCTEXT("CombineSDF", "Combining SDFs..."));
	if (SlowTask.ShouldCancel()) return;

	TArray<double> CombinedField;
	FSDFProcessor::CombineSDFs(ProcessedData, CombinedField, HighW, HighH);

	SlowTask.EnterProgressFrame(1.f, LOCTEXT("DownscaleSDF", "Downscaling and Saving..."));
	if (SlowTask.ShouldCancel()) return;

	TArray<uint16> FinalPixels = FSDFProcessor::DownscaleAndConvert(CombinedField, HighW, HighH, Upscale);
	FString PackageName = TEXT("/Game/QuickSDF_GENERATED");
	FString TextureName = TEXT("T_QuickSDF_ThresholdMap");
	Subsystem->Create16BitTexture(FinalPixels, OrigW, OrigH, PackageName, TextureName);
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
		ChangeTargetComponent(Subsystem->GetTargetMeshComponent());

		// サブシステムにアセットがない場合は仮で新規作成
		if (!Subsystem->GetActiveSDFAsset())
		{
			UQuickSDFAsset* NewAsset = NewObject<UQuickSDFAsset>(Subsystem);
			NewAsset->Resolution = FIntPoint(1024, 1024);
			NewAsset->UVChannel = 0;
			float InitialMaxAngle = Properties->bSymmetryMode ? 90.0f : 180.0f;
			for (int32 i = 0; i < 8; ++i)
			{
				FQuickSDFAngleData Data;
				Data.Angle = (i / 7.0f) * InitialMaxAngle;
				NewAsset->AngleDataList.Add(Data);
			}
			Subsystem->SetActiveSDFAsset(NewAsset);
		}

		UQuickSDFAsset* ActiveAsset = Subsystem->GetActiveSDFAsset();
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
			}
		}
	}
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

	if (!CurrentComponent.IsValid())
	{
		return;
	}

	bool bValidMeshLoaded = false;
	TSharedPtr<UE::Geometry::FDynamicMesh3> TempMesh = MakeShared<UE::Geometry::FDynamicMesh3>();

	if (UStaticMeshComponent* SMC = Cast<UStaticMeshComponent>(CurrentComponent.Get()))
	{
		if (UStaticMesh* StaticMesh = SMC->GetStaticMesh())
		{
			if (const FMeshDescription* MeshDesc = StaticMesh->GetMeshDescription(0))
			{
				FMeshDescriptionToDynamicMesh Converter;
				Converter.Convert(MeshDesc, *TempMesh);
				bValidMeshLoaded = true;
			}
		}
	}

	if (!bValidMeshLoaded || TempMesh->TriangleCount() <= 0)
	{
		CurrentComponent.Reset();
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

	// 新しいコンポーネントのマテリアルをプレビュー用に差し替え
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
	
	Super::Shutdown(ShutdownType); // ツール終了処理
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
	if (!Properties) return false;
	
	UQuickSDFToolSubsystem* Subsystem = GEditor->GetEditorSubsystem<UQuickSDFToolSubsystem>();
	if (!Subsystem || !Subsystem->GetActiveSDFAsset()) return false;

	UQuickSDFAsset* Asset = Subsystem->GetActiveSDFAsset();
	if (!Asset->AngleDataList.IsValidIndex(AngleIndex)) return false;
	
	const bool bRestored = RestoreRenderTargetPixels(Asset->AngleDataList[AngleIndex].PaintRenderTarget, Pixels);
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
		
		UQuickSDFToolSubsystem* Subsystem = GEditor->GetEditorSubsystem<UQuickSDFToolSubsystem>();
		if (Subsystem && Subsystem->GetActiveSDFAsset() && Subsystem->GetActiveSDFAsset()->AngleDataList.Num() > 0)
		{
			StrokeTransactionAngleIndex = FMath::Clamp(Properties->EditAngleIndex, 0, Subsystem->GetActiveSDFAsset()->AngleDataList.Num() - 1);
		}
		else
		{
			StrokeTransactionAngleIndex = 0;
		}
	}
}

void UQuickSDFPaintTool::EndStrokeTransaction()
{
	if (!bStrokeTransactionActive) return;
	TArray<FColor> AfterPixels;
	
	UQuickSDFToolSubsystem* Subsystem = GEditor->GetEditorSubsystem<UQuickSDFToolSubsystem>();
	if (Properties && Subsystem && Subsystem->GetActiveSDFAsset())
	{
		UQuickSDFAsset* Asset = Subsystem->GetActiveSDFAsset();
		if (Asset->AngleDataList.IsValidIndex(StrokeTransactionAngleIndex))
		{
			if (CaptureRenderTargetPixels(Asset->AngleDataList[StrokeTransactionAngleIndex].PaintRenderTarget, AfterPixels) &&
				AfterPixels.Num() == StrokeBeforePixels.Num() && AfterPixels != StrokeBeforePixels)
			{
				TUniquePtr<FQuickSDFRenderTargetChange> Change = MakeUnique<FQuickSDFRenderTargetChange>();
				Change->AngleIndex = StrokeTransactionAngleIndex;
				Change->BeforePixels = MoveTemp(StrokeBeforePixels);
				Change->AfterPixels = MoveTemp(AfterPixels);
				GetToolManager()->EmitObjectChange(this, MoveTemp(Change), LOCTEXT("QuickSDFPaintStrokeChange", "Quick SDF Paint Stroke"));
			}
		}
	}
	
	bStrokeTransactionActive = false;
	StrokeTransactionAngleIndex = INDEX_NONE;
	StrokeBeforePixels.Reset();
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
	if (CurrentComponent.IsValid())
	{
		FCollisionQueryParams Params(SCENE_QUERY_STAT(QuickSDF), true);
		Params.bReturnFaceIndex = true;
		Params.bTraceComplex = true;

		if (CurrentComponent->LineTraceComponent(OutHit, Ray.Origin, Ray.Origin + Ray.Direction * 100000.0f, Params))
		{
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
	if (!TargetMeshSpatial.IsValid() || !TargetMesh.IsValid() || !Properties || !CurrentComponent.IsValid()) return false;
	
	const FTransform Transform = CurrentComponent->GetComponentTransform();
	const FRay LocalRay(Transform.InverseTransformPosition(Ray.Origin), Transform.InverseTransformVector(Ray.Direction));

	double HitDistance = 100000.0;
	int32 HitTID = -1;
	FVector3d BaryCoords(0.0, 0.0, 0.0);
	const bool bHit = TargetMeshSpatial->FindNearestHitTriangle(LocalRay, HitDistance, HitTID, BaryCoords);

	if (!bHit || HitTID < 0) return false;

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

	// ワールド空間での三角形の面積（の2倍）を計算
	double AreaWorld = FVector3d::CrossProduct(V1 - V0, V2 - V0).Size();
	// UV空間での三角形の面積（の2倍）を計算
	double AreaUV = FMath::Abs((UV1.X - UV0.X) * (UV2.Y - UV0.Y) - (UV2.X - UV0.X) * (UV1.Y - UV0.Y));

	// 1ワールド単位あたりのUV単位（スケール）を計算
	// 面積比の平方根をとることで線形スケールを得る
	if (AreaWorld > KINDA_SMALL_NUMBER)
	{
		OutSample.LocalUVScale = FMath::Sqrt(AreaUV / AreaWorld);
	}
	else
	{
		OutSample.LocalUVScale = 0.001f;
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

	UTextureRenderTarget2D* RT = GetActiveRenderTarget();
	if (!RT || !BrushMaskTexture) return;

	FTextureRenderTargetResource* RTResource = RT->GameThread_GetRenderTargetResource();
	if (!RTResource) return;

	const FVector2D RTSize(RT->SizeX, RT->SizeY);
	const FLinearColor PaintColor = IsPaintingShadow() ? FLinearColor::Black : FLinearColor::White;
	
	FCanvas Canvas(RTResource, nullptr, GetToolManager()->GetContextQueriesAPI()->GetCurrentEditingWorld(), GMaxRHIFeatureLevel);

	for (const FQuickSDFStrokeSample& Sample : Samples)
	{
		float BrushRadiusWorld = BrushProperties ? BrushProperties->BrushRadius : 10.0f;
		FVector2D PixelSize;
		
		if (ActiveStrokeInputMode == EQuickSDFStrokeInputMode::TexturePreview)
		{
			// プレビュー表示（2D）の場合は以前の固定計算
			PixelSize = GetBrushPixelSize(RT);
		}
		else
		{
			// 3Dメッシュ上の場合は、計算したローカルスケールを使用
			float UVRadius = BrushRadiusWorld * Sample.LocalUVScale;
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
			// 詳細パネルから別のアセットに切り替えた場合の処理
			if (Property && Property->GetFName() == GET_MEMBER_NAME_CHECKED(UQuickSDFToolProperties, TargetAsset))
			{
				if (Properties->TargetAsset != nullptr)
				{
					Subsystem->SetActiveSDFAsset(Properties->TargetAsset);
					ActiveAsset = Properties->TargetAsset;
					ActiveAsset->InitializeRenderTargets(GetToolManager()->GetContextQueriesAPI()->GetCurrentEditingWorld());
					
					// 新しいアセットの値をUIにロード
					Properties->Resolution = ActiveAsset->Resolution;
					Properties->UVChannel = ActiveAsset->UVChannel;
					Properties->NumAngles = ActiveAsset->AngleDataList.Num();
					Properties->TargetAngles.SetNum(Properties->NumAngles);
					for (int32 i = 0; i < Properties->NumAngles; ++i)
					{
						Properties->TargetAngles[i] = ActiveAsset->AngleDataList[i].Angle;
					}
					RefreshPreviewMaterial();
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
				for (int32 i = 0; i < FMath::Min(Properties->TargetTextures.Num(), ActiveAsset->AngleDataList.Num()); ++i)
				{
					// UIのテクスチャとアセットのテクスチャに差分があれば更新
					if (ActiveAsset->AngleDataList[i].TextureMask != Properties->TargetTextures[i])
					{
						ActiveAsset->AngleDataList[i].TextureMask = Properties->TargetTextures[i];

						// 画像がセットされたならキャンバスに転写、外されたなら白紙に戻す
						if (Properties->TargetTextures[i] != nullptr)
						{
							Subsystem->DrawTextureToRenderTarget(Properties->TargetTextures[i], ActiveAsset->AngleDataList[i].PaintRenderTarget);
						}
						else
						{
							Subsystem->ClearRenderTarget(ActiveAsset->AngleDataList[i].PaintRenderTarget);
						}
						
						ActiveAsset->Modify(); // 変更をマーク
					}
				}
				RefreshPreviewMaterial();
			}//TODO:後からテクスチャを追加する処理を実装する
			// 解像度やUVチャンネルの同期
			if (Property && Property->GetFName() == GET_MEMBER_NAME_CHECKED(UQuickSDFToolProperties, Resolution))
			{
				ActiveAsset->Resolution = Properties->Resolution;
			}

			if (Property && Property->GetFName() == GET_MEMBER_NAME_CHECKED(UQuickSDFToolProperties, UVChannel))
			{
				ActiveAsset->UVChannel = Properties->UVChannel;
				RefreshPreviewMaterial();
			}
		}

		if (Property && (Property->GetFName() == GET_MEMBER_NAME_CHECKED(UQuickSDFToolProperties, EditAngleIndex) ||
				 Property->GetFName() == GET_MEMBER_NAME_CHECKED(UQuickSDFToolProperties, bOverlayOriginalShadow)))
		{
			RefreshPreviewMaterial();
		}
	}
}

void UQuickSDFPaintTool::AddKeyframe()
{
	if (!Properties) return;
	UQuickSDFToolSubsystem* Subsystem = GEditor->GetEditorSubsystem<UQuickSDFToolSubsystem>();
	if (!Subsystem || !Subsystem->GetActiveSDFAsset()) return;

	UQuickSDFAsset* Asset = Subsystem->GetActiveSDFAsset();

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
	
	Asset->AngleDataList.Insert(NewData, InsertIndex);
	Properties->TargetAngles.Insert(NewAngle, InsertIndex);
	Properties->TargetTextures.Insert(nullptr, InsertIndex);
	Properties->NumAngles = Asset->AngleDataList.Num();

	Asset->InitializeRenderTargets(GetToolManager()->GetContextQueriesAPI()->GetCurrentEditingWorld());
	
	Properties->EditAngleIndex = InsertIndex;
	FProperty* Prop = Properties->GetClass()->FindPropertyByName(GET_MEMBER_NAME_CHECKED(UQuickSDFToolProperties, EditAngleIndex));
	OnPropertyModified(Properties, Prop);

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

		GetToolManager()->EndUndoTransaction();
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
    	
        if (TargetMesh.IsValid() && TargetMesh->HasAttributes() && Properties->bOverlayUV)
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