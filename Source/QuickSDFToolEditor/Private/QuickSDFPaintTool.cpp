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

#if WITH_EDITOR
#include "Editor.h"
#endif

#define LOCTEXT_NAMESPACE "QuickSDFPaintTool"

namespace
{
constexpr double QuickSDFStrokeSpacingFactor = 0.12;
constexpr double QuickSDFMinSampleDistanceSq = 1.0e-6;
constexpr int32 QuickSDFBrushMaskResolution = 64;
constexpr int32 QuickSDFPreviewActionIncreaseBrush = static_cast<int32>(EStandardToolActions::BaseClientDefinedActionID) + 1;
constexpr float QuickSDFMinResizeSensitivity = 0.01f;
constexpr double QuickSDFSubPixelSpacing = 0.35;
constexpr double QuickSDFStrokeSmoothingMinAlpha = 0.08;
constexpr double QuickSDFStrokeSmoothingMaxAlpha = 0.35;

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
	for (int32 i = 0; i < TransientRenderTargets.Num(); ++i)
	{
		UTextureRenderTarget2D* RT = TransientRenderTargets[i];
		if (!RT)
		{
			continue;
		}

		FString PackageName = FString::Printf(TEXT("/Game/QuickSDF_Export_%d"), i);
		UPackage* Package = CreatePackage(*PackageName);
		if (Package)
		{
			FString TextureName = FString::Printf(TEXT("T_QuickSDF_Angle%d"), i);
			UTexture2D* NewTex = RT->ConstructTexture2D(Package, TextureName, EObjectFlags::RF_Public | EObjectFlags::RF_Standalone, CTF_Default, nullptr);
			if (NewTex)
			{
				FAssetRegistryModule::AssetCreated(NewTex);
				NewTex->MarkPackageDirty();
			}
		}
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
	BrushMaskTexture->Filter = TF_Nearest;
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
			const bool bInside = FVector2f::Distance(Pos, Center) <= Radius;
			Pixels[Y * QuickSDFBrushMaskResolution + X] = bInside ? FColor::White : FColor(255, 255, 255, 0);
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
	}
}

FQuickSDFStrokeSample UQuickSDFPaintTool::SmoothStrokeSample(const FQuickSDFStrokeSample& RawSample)
{
	if (!bHasFilteredStrokeSample)
	{
		FilteredStrokeSample = RawSample;
		bHasFilteredStrokeSample = true;
		return RawSample;
	}

	const FQuickSDFStrokeSample& PrevSample = FilteredStrokeSample;
	const double Distance = FVector3d::Distance(PrevSample.WorldPos, RawSample.WorldPos);
	const double Radius = FMath::Max(BrushProperties ? static_cast<double>(BrushProperties->BrushRadius) : 1.0, 1.0);
	const double NormalizedDistance = FMath::Clamp(Distance / Radius, 0.0, 1.0);
	const float Alpha = static_cast<float>(FMath::Lerp(QuickSDFStrokeSmoothingMinAlpha, QuickSDFStrokeSmoothingMaxAlpha, NormalizedDistance));

	FQuickSDFStrokeSample SmoothedSample;
	SmoothedSample.WorldPos = FMath::Lerp(PrevSample.WorldPos, RawSample.WorldPos, Alpha);
	SmoothedSample.UV = FMath::Lerp(PrevSample.UV, RawSample.UV, Alpha);
	FilteredStrokeSample = SmoothedSample;
	return SmoothedSample;
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
		LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial")),
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
	if (!RenderTarget)
	{
		return false;
	}

	FTextureRenderTargetResource* RTResource = RenderTarget->GameThread_GetRenderTargetResource();
	if (!RTResource)
	{
		return false;
	}

	FReadSurfaceDataFlags ReadFlags(RCM_UNorm);
	ReadFlags.SetLinearToGamma(false);
	return RTResource->ReadPixels(OutPixels, ReadFlags);
}

bool UQuickSDFPaintTool::RestoreRenderTargetPixels(UTextureRenderTarget2D* RenderTarget, const TArray<FColor>& Pixels) const
{
	if (!RenderTarget || Pixels.Num() != RenderTarget->SizeX * RenderTarget->SizeY)
	{
		return false;
	}

	UTexture2D* TempTexture = UTexture2D::CreateTransient(RenderTarget->SizeX, RenderTarget->SizeY, PF_B8G8R8A8);
	if (!TempTexture || !TempTexture->GetPlatformData() || TempTexture->GetPlatformData()->Mips.Num() == 0)
	{
		return false;
	}

	TempTexture->MipGenSettings = TMGS_NoMipmaps;
	TempTexture->Filter = TF_Nearest;
	TempTexture->SRGB = false;

	FTexture2DMipMap& Mip = TempTexture->GetPlatformData()->Mips[0];
	void* Data = Mip.BulkData.Lock(LOCK_READ_WRITE);
	FMemory::Memcpy(Data, Pixels.GetData(), Pixels.Num() * sizeof(FColor));
	Mip.BulkData.Unlock();
	TempTexture->UpdateResource();

	FTextureRenderTargetResource* RTResource = RenderTarget->GameThread_GetRenderTargetResource();
	if (!RTResource)
	{
		return false;
	}

	FCanvas Canvas(RTResource, nullptr, GetToolManager()->GetContextQueriesAPI()->GetCurrentEditingWorld(), GMaxRHIFeatureLevel);
	FCanvasTileItem TileItem(FVector2D::ZeroVector, TempTexture->GetResource(), FVector2D(RenderTarget->SizeX, RenderTarget->SizeY), FLinearColor::White);
	TileItem.BlendMode = SE_BLEND_Opaque;
	Canvas.DrawItem(TileItem);
	Canvas.Flush_GameThread(true);

	ENQUEUE_RENDER_COMMAND(RestoreQuickSDFPaintRTCommand)(
		[RTResource](FRHICommandListImmediate& RHICmdList)
		{
			TransitionAndCopyTexture(RHICmdList, RTResource->GetRenderTargetTexture(), RTResource->TextureRHI, {});
		});

	return true;
}

bool UQuickSDFPaintTool::ApplyRenderTargetPixels(int32 AngleIndex, const TArray<FColor>& Pixels)
{
	if (!Properties || !Properties->TransientRenderTargets.IsValidIndex(AngleIndex))
	{
		return false;
	}

	const bool bRestored = RestoreRenderTargetPixels(Properties->TransientRenderTargets[AngleIndex], Pixels);
	if (bRestored)
	{
		RefreshPreviewMaterial();
	}
	return bRestored;
}

void UQuickSDFPaintTool::BeginStrokeTransaction()
{
	UTextureRenderTarget2D* RT = GetActiveRenderTarget();
	if (!RT || bStrokeTransactionActive)
	{
		return;
	}

	StrokeBeforePixels.Reset();
	if (CaptureRenderTargetPixels(RT, StrokeBeforePixels))
	{
		bStrokeTransactionActive = true;
		StrokeTransactionAngleIndex = FMath::Clamp(Properties->EditAngleIndex, 0, Properties->TransientRenderTargets.Num() - 1);
	}
}

void UQuickSDFPaintTool::EndStrokeTransaction()
{
	if (!bStrokeTransactionActive)
	{
		return;
	}

	TArray<FColor> AfterPixels;
	if (Properties && Properties->TransientRenderTargets.IsValidIndex(StrokeTransactionAngleIndex))
	{
		if (CaptureRenderTargetPixels(Properties->TransientRenderTargets[StrokeTransactionAngleIndex], AfterPixels) &&
			AfterPixels.Num() == StrokeBeforePixels.Num() &&
			AfterPixels != StrokeBeforePixels)
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
	if (!Properties)
	{
		return nullptr;
	}

	const int32 AngleIdx = FMath::Clamp(Properties->EditAngleIndex, 0, Properties->TransientRenderTargets.Num() - 1);
	return Properties->TransientRenderTargets.IsValidIndex(AngleIdx) ? Properties->TransientRenderTargets[AngleIdx] : nullptr;
}

FVector2D UQuickSDFPaintTool::GetPreviewOrigin() const
{
	return PreviewCanvasOrigin;
}

FVector2D UQuickSDFPaintTool::GetPreviewSize() const
{
	return PreviewCanvasSize;
}

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
	if (!RenderTarget)
	{
		return FVector2D(16.0, 16.0);
	}

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

	if (!BrushProperties)
	{
		return 1.0;
	}

	return FMath::Max(static_cast<double>(BrushProperties->BrushRadius) * QuickSDFStrokeSpacingFactor, 0.1);
}

bool UQuickSDFPaintTool::IsPaintingShadow() const
{
	return !GetShiftToggle();
}

void FQuickSDFRenderTargetChange::Apply(UObject* Object)
{
	if (UQuickSDFPaintTool* Tool = Cast<UQuickSDFPaintTool>(Object))
	{
		Tool->ApplyRenderTargetPixels(AngleIndex, AfterPixels);
	}
}

void FQuickSDFRenderTargetChange::Revert(UObject* Object)
{
	if (UQuickSDFPaintTool* Tool = Cast<UQuickSDFPaintTool>(Object))
	{
		Tool->ApplyRenderTargetPixels(AngleIndex, BeforePixels);
	}
}

void UQuickSDFPaintTool::BeginBrushResizeMode()
{
	if (!BrushProperties)
	{
		return;
	}

	if (!bBrushResizeTransactionOpen)
	{
		GetToolManager()->BeginUndoTransaction(LOCTEXT("QuickSDFBrushResizeTransaction", "Quick SDF Change Brush Radius"));
		BrushProperties->SetFlags(RF_Transactional);
		BrushProperties->Modify();
		bBrushResizeTransactionOpen = true;
	}

	bAdjustingBrushRadius = true;
	BrushResizeStartScreenPosition = LastInputScreenPosition;
	BrushResizeStartRadius = BrushProperties->BrushRadius;
}

void UQuickSDFPaintTool::UpdateBrushResizeFromCursor()
{
	if (!bAdjustingBrushRadius || !BrushProperties)
	{
		return;
	}

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
	if (!bAdjustingBrushRadius)
	{
		return;
	}

	UpdateBrushResizeFromCursor();
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
	if (bAdjustingBrushRadius)
	{
		return;
	}

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
	BeginStrokeTransaction();

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
		StrokeSamples.Add(Sample);
		StampSample(Sample);
		LastStampedSample = Sample;
		bHasLastStampedSample = true;
	}
}

void UQuickSDFPaintTool::OnUpdateDrag(const FRay& Ray)
{
	Super::OnUpdateDrag(Ray);
	if (bAdjustingBrushRadius)
	{
		return;
	}

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
	if (bAdjustingBrushRadius)
	{
		return;
	}

	if (StrokeSamples.Num() == 2)
	{
		StampInterpolatedSegment(StrokeSamples[0], StrokeSamples[0], StrokeSamples[1], StrokeSamples[1]);
	}
	else if (StrokeSamples.Num() >= 3)
	{
		const int32 LastIndex = StrokeSamples.Num() - 1;
		StampInterpolatedSegment(
			StrokeSamples[FMath::Max(0, LastIndex - 2)],
			StrokeSamples[LastIndex - 1],
			StrokeSamples[LastIndex],
			StrokeSamples[LastIndex]);
	}

	if (StrokeSamples.Num() > 0 && bHasLastStampedSample)
	{
		const FQuickSDFStrokeSample& TailSample = StrokeSamples.Last();
		if (DistanceSinceLastStamp > KINDA_SMALL_NUMBER &&
			FVector3d::DistSquared(LastStampedSample.WorldPos, TailSample.WorldPos) > QuickSDFMinSampleDistanceSq)
		{
			StampSample(TailSample);
		}
	}

	EndStrokeTransaction();
	ResetStrokeState();
}

bool UQuickSDFPaintTool::TryMakeStrokeSample(const FRay& Ray, FQuickSDFStrokeSample& OutSample)
{
	if (!TargetMeshSpatial.IsValid() || !TargetMesh.IsValid() || !Properties || !CurrentComponent)
	{
		return false;
	}

	const FTransform Transform = CurrentComponent->GetComponentTransform();
	const FRay LocalRay(Transform.InverseTransformPosition(Ray.Origin), Transform.InverseTransformVector(Ray.Direction));

	double HitDistance = 100000.0;
	int32 HitTID = -1;
	FVector3d BaryCoords(0.0, 0.0, 0.0);
	const bool bHit = TargetMeshSpatial->FindNearestHitTriangle(LocalRay, HitDistance, HitTID, BaryCoords);

	if (!bHit || HitTID < 0)
	{
		return false;
	}

	const UE::Geometry::FDynamicMeshUVOverlay* UVOverlay = TargetMesh->Attributes()->GetUVLayer(Properties->UVChannel);
	if (!UVOverlay || !UVOverlay->IsSetTriangle(HitTID))
	{
		return false;
	}

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
	if (!RT || !IsInPreviewBounds(ScreenPosition))
	{
		return false;
	}

	OutSample.UV = ScreenToPreviewUV(ScreenPosition);
	OutSample.WorldPos = FVector3d(OutSample.UV.X * RT->SizeX, OutSample.UV.Y * RT->SizeY, 0.0);
	return true;
}

void UQuickSDFPaintTool::StampSample(const FQuickSDFStrokeSample& Sample)
{
	UTextureRenderTarget2D* RT = GetActiveRenderTarget();
	if (!RT || !BrushMaskTexture)
	{
		return;
	}

	FTextureRenderTargetResource* RTResource = RT->GameThread_GetRenderTargetResource();
	if (!RTResource)
	{
		return;
	}

	const FVector2D RTSize(RT->SizeX, RT->SizeY);
	const FVector2D PixelSize = GetBrushPixelSize(RT);
	const FVector2D PixelPos(Sample.UV.X * RTSize.X, Sample.UV.Y * RTSize.Y);
	const FVector2D StampPos = PixelPos - (PixelSize * 0.5f);
	const FLinearColor PaintColor = IsPaintingShadow() ? FLinearColor::Black : FLinearColor::White;

	FCanvas Canvas(RTResource, nullptr, GetToolManager()->GetContextQueriesAPI()->GetCurrentEditingWorld(), GMaxRHIFeatureLevel);
	FCanvasTileItem BrushItem(StampPos, BrushMaskTexture->GetResource(), PixelSize, PaintColor);
	BrushItem.BlendMode = SE_BLEND_Translucent;

	Canvas.DrawItem(BrushItem);
	Canvas.Flush_GameThread(true);

	ENQUEUE_RENDER_COMMAND(UpdateSDFPaintRTCommand)(
		[RTResource](FRHICommandListImmediate& RHICmdList)
		{
			TransitionAndCopyTexture(RHICmdList, RTResource->GetRenderTargetTexture(), RTResource->TextureRHI, {});
		});

	RefreshPreviewMaterial();
}

void UQuickSDFPaintTool::AppendStrokeSample(const FQuickSDFStrokeSample& Sample)
{
	if (StrokeSamples.Num() > 0 &&
		FVector3d::DistSquared(StrokeSamples.Last().WorldPos, Sample.WorldPos) <= QuickSDFMinSampleDistanceSq)
	{
		return;
	}

	StrokeSamples.Add(Sample);
	const int32 NumSamples = StrokeSamples.Num();
	if (NumSamples < 3)
	{
		return;
	}

	if (NumSamples == 3)
	{
		StampInterpolatedSegment(StrokeSamples[0], StrokeSamples[0], StrokeSamples[1], StrokeSamples[2]);
		return;
	}

	StampInterpolatedSegment(
		StrokeSamples[NumSamples - 4],
		StrokeSamples[NumSamples - 3],
		StrokeSamples[NumSamples - 2],
		StrokeSamples[NumSamples - 1]);

	if (StrokeSamples.Num() > 4)
	{
		StrokeSamples.RemoveAt(0, StrokeSamples.Num() - 4, EAllowShrinking::No);
	}
}

void UQuickSDFPaintTool::StampInterpolatedSegment(
	const FQuickSDFStrokeSample& P0,
	const FQuickSDFStrokeSample& P1,
	const FQuickSDFStrokeSample& P2,
	const FQuickSDFStrokeSample& P3)
{
	UTextureRenderTarget2D* RT = GetActiveRenderTarget();
	if (!RT)
	{
		return;
	}

	const double SegmentLength = FVector3d::Distance(P1.WorldPos, P2.WorldPos);
	if (SegmentLength <= KINDA_SMALL_NUMBER)
	{
		return;
	}

	const double Spacing = GetCurrentStrokeSpacing(RT);
	const int32 NumSteps = FMath::Max(24, FMath::CeilToInt(SegmentLength / FMath::Max(Spacing * QuickSDFSubPixelSpacing, 0.125)) * 8);

	auto SafeParam = [](double Value)
	{
		return Value > KINDA_SMALL_NUMBER ? Value : KINDA_SMALL_NUMBER;
	};

	auto CentripetalDistance = [&](const FQuickSDFStrokeSample& A, const FQuickSDFStrokeSample& B)
	{
		return FMath::Pow(FVector3d::Distance(A.WorldPos, B.WorldPos), 0.5);
	};

	auto EvaluateCatmullRom = [&](const FQuickSDFStrokeSample& InP0, const FQuickSDFStrokeSample& InP1, const FQuickSDFStrokeSample& InP2, const FQuickSDFStrokeSample& InP3, double T)
	{
		const double T0 = 0.0;
		const double T1 = T0 + SafeParam(CentripetalDistance(InP0, InP1));
		const double T2 = T1 + SafeParam(CentripetalDistance(InP1, InP2));
		const double T3 = T2 + SafeParam(CentripetalDistance(InP2, InP3));
		const double EvalT = FMath::Lerp(T1, T2, T);

		auto BlendSample = [](const FQuickSDFStrokeSample& A, const FQuickSDFStrokeSample& B, double AlphaValue)
		{
			FQuickSDFStrokeSample Result;
			Result.WorldPos = FMath::Lerp(A.WorldPos, B.WorldPos, AlphaValue);
			Result.UV = FMath::Lerp(A.UV, B.UV, static_cast<float>(AlphaValue));
			return Result;
		};

		const FQuickSDFStrokeSample A1 = BlendSample(InP0, InP1, (EvalT - T0) / SafeParam(T1 - T0));
		const FQuickSDFStrokeSample A2 = BlendSample(InP1, InP2, (EvalT - T1) / SafeParam(T2 - T1));
		const FQuickSDFStrokeSample A3 = BlendSample(InP2, InP3, (EvalT - T2) / SafeParam(T3 - T2));
		const FQuickSDFStrokeSample B1 = BlendSample(A1, A2, (EvalT - T0) / SafeParam(T2 - T0));
		const FQuickSDFStrokeSample B2 = BlendSample(A2, A3, (EvalT - T1) / SafeParam(T3 - T1));
		return BlendSample(B1, B2, (EvalT - T1) / SafeParam(T2 - T1));
	};

	auto LerpStrokeSample = [](const FQuickSDFStrokeSample& A, const FQuickSDFStrokeSample& B, double T)
	{
		FQuickSDFStrokeSample Result;
		Result.WorldPos = FMath::Lerp(A.WorldPos, B.WorldPos, T);
		Result.UV = FMath::Lerp(A.UV, B.UV, static_cast<float>(T));
		return Result;
	};

	FQuickSDFStrokeSample PrevSample = EvaluateCatmullRom(P0, P1, P2, P3, 0.0);
	for (int32 Step = 1; Step <= NumSteps; ++Step)
	{
		const double T = static_cast<double>(Step) / static_cast<double>(NumSteps);
		FQuickSDFStrokeSample CurrentSample = EvaluateCatmullRom(P0, P1, P2, P3, T);
		double RemainingSegmentDistance = FVector3d::Distance(PrevSample.WorldPos, CurrentSample.WorldPos);

		if (RemainingSegmentDistance <= KINDA_SMALL_NUMBER)
		{
			PrevSample = CurrentSample;
			continue;
		}

		while ((DistanceSinceLastStamp + RemainingSegmentDistance) >= Spacing)
		{
			const double DistanceToNextStamp = Spacing - DistanceSinceLastStamp;
			const double Alpha = DistanceToNextStamp / RemainingSegmentDistance;
			FQuickSDFStrokeSample Stamp = LerpStrokeSample(PrevSample, CurrentSample, Alpha);
			StampSample(Stamp);
			LastStampedSample = Stamp;
			bHasLastStampedSample = true;

			PrevSample = Stamp;
			RemainingSegmentDistance = FVector3d::Distance(PrevSample.WorldPos, CurrentSample.WorldPos);
			DistanceSinceLastStamp = 0.0;

			if (RemainingSegmentDistance <= KINDA_SMALL_NUMBER)
			{
				break;
			}
		}

		DistanceSinceLastStamp += RemainingSegmentDistance;
		PrevSample = CurrentSample;
	}
}

void UQuickSDFPaintTool::ResetStrokeState()
{
	StrokeSamples.Reset();
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
		if (Property && Property->GetFName() == GET_MEMBER_NAME_CHECKED(UQuickSDFToolProperties, EditAngleIndex))
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
		PreviewCanvasOrigin = FVector2D(10.0f, 10.0f);
		PreviewCanvasSize = FVector2D(256.0f, 256.0f);
		const FVector2D PreviewOrigin = GetPreviewOrigin();
		const FVector2D PreviewSize = GetPreviewSize();
		FCanvasTileItem TileItem(PreviewOrigin, RT->GetResource(), PreviewSize, FLinearColor::White);
		TileItem.BlendMode = SE_BLEND_Opaque;
		Canvas->DrawItem(TileItem);

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
