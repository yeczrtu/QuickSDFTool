#include "QuickSDFPaintTool.h"
#include "InteractiveToolManager.h"
#include "ToolBuilderUtil.h"
#include "EngineUtils.h"
#include "Engine/DirectionalLight.h"
#include "CollisionQueryParams.h"
#include "Engine/TextureRenderTarget2D.h"
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

#if WITH_EDITOR
#include "Editor.h"
#endif

namespace
{
constexpr double QuickSDFStrokeSpacingFactor = 0.25;
constexpr double QuickSDFMinSampleDistanceSq = 1.0e-6;

double GetStrokeSpacingWorld(const UBrushBaseProperties* BrushProperties)
{
	if (!BrushProperties)
	{
		return 1.0;
	}

	return FMath::Max(static_cast<double>(BrushProperties->BrushRadius) * QuickSDFStrokeSpacingFactor, 0.1);
}

FQuickSDFStrokeSample LerpStrokeSample(const FQuickSDFStrokeSample& A, const FQuickSDFStrokeSample& B, double T)
{
	FQuickSDFStrokeSample Result;
	Result.WorldPos = FMath::Lerp(A.WorldPos, B.WorldPos, T);
	Result.UV = FMath::Lerp(A.UV, B.UV, static_cast<float>(T));
	return Result;
}

FQuickSDFStrokeSample EvaluateCatmullRom(
	const FQuickSDFStrokeSample& P0,
	const FQuickSDFStrokeSample& P1,
	const FQuickSDFStrokeSample& P2,
	const FQuickSDFStrokeSample& P3,
	double T)
{
	const double T2 = T * T;
	const double T3 = T2 * T;
	const float Tf = static_cast<float>(T);
	const float T2f = static_cast<float>(T2);
	const float T3f = static_cast<float>(T3);

	FQuickSDFStrokeSample Result;
	Result.WorldPos =
		0.5 * ((2.0 * P1.WorldPos) +
			(-P0.WorldPos + P2.WorldPos) * T +
			((2.0 * P0.WorldPos) - (5.0 * P1.WorldPos) + (4.0 * P2.WorldPos) - P3.WorldPos) * T2 +
			(-P0.WorldPos + (3.0 * P1.WorldPos) - (3.0 * P2.WorldPos) + P3.WorldPos) * T3);
	Result.UV =
		0.5f * ((2.0f * P1.UV) +
			(-P0.UV + P2.UV) * Tf +
			((2.0f * P0.UV) - (5.0f * P1.UV) + (4.0f * P2.UV) - P3.UV) * T2f +
			(-P0.UV + (3.0f * P1.UV) - (3.0f * P2.UV) + P3.UV) * T3f);
	return Result;
}
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

void UQuickSDFPaintTool::Setup()
{
	Super::Setup();

	Properties = NewObject<UQuickSDFToolProperties>(this);
	AddToolPropertySource(Properties);

	InitializeRenderTargets();
	ResetStrokeState();
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

	const int32 AngleIdx = FMath::Clamp(Properties->EditAngleIndex, 0, Properties->TransientRenderTargets.Num() - 1);
	if (Properties->TransientRenderTargets.IsValidIndex(AngleIdx))
	{
		PreviewMaterial->SetTextureParameterValue(TEXT("BaseColor"), Properties->TransientRenderTargets[AngleIdx]);
	}
}

void UQuickSDFPaintTool::Shutdown(EToolShutdownType ShutdownType)
{
	ChangeTargetComponent(nullptr);
	UInteractiveTool::Shutdown(ShutdownType);
}

FInputRayHit UQuickSDFPaintTool::CanBeginClickDragSequence(const FInputDeviceRay& PressPos)
{
	FHitResult OutHit;
	if (HitTest(PressPos.WorldRay, OutHit))
	{
		return FInputRayHit(OutHit.Distance);
	}

	return Super::CanBeginClickDragSequence(PressPos);
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

void UQuickSDFPaintTool::OnBeginDrag(const FRay& Ray)
{
	Super::OnBeginDrag(Ray);
	ResetStrokeState();

	FQuickSDFStrokeSample Sample;
	if (TryMakeStrokeSample(Ray, Sample))
	{
		StrokeSamples.Add(Sample);
		StampSample(Sample);
		LastStampedSample = Sample;
		bHasLastStampedSample = true;
	}
}

void UQuickSDFPaintTool::OnUpdateDrag(const FRay& Ray)
{
	Super::OnUpdateDrag(Ray);

	FQuickSDFStrokeSample Sample;
	if (TryMakeStrokeSample(Ray, Sample))
	{
		AppendStrokeSample(Sample);
	}
}

void UQuickSDFPaintTool::OnEndDrag(const FRay& Ray)
{
	if (StrokeSamples.Num() > 0 && bHasLastStampedSample)
	{
		const FQuickSDFStrokeSample& TailSample = StrokeSamples.Last();
		if (DistanceSinceLastStamp > KINDA_SMALL_NUMBER &&
			FVector3d::DistSquared(LastStampedSample.WorldPos, TailSample.WorldPos) > QuickSDFMinSampleDistanceSq)
		{
			StampSample(TailSample);
		}
	}

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

void UQuickSDFPaintTool::StampSample(const FQuickSDFStrokeSample& Sample)
{
	if (!Properties || !CurrentComponent || !TargetMesh.IsValid())
	{
		return;
	}

	const int32 AngleIdx = FMath::Clamp(Properties->EditAngleIndex, 0, Properties->TransientRenderTargets.Num() - 1);
	if (!Properties->TransientRenderTargets.IsValidIndex(AngleIdx))
	{
		return;
	}

	UTextureRenderTarget2D* RT = Properties->TransientRenderTargets[AngleIdx];
	if (!RT)
	{
		return;
	}

	FTextureRenderTargetResource* RTResource = RT->GameThread_GetRenderTargetResource();
	if (!RTResource)
	{
		return;
	}

	const FTransform Transform = CurrentComponent->GetComponentTransform();
	FCanvas Canvas(RTResource, nullptr, CurrentComponent->GetWorld(), GMaxRHIFeatureLevel);

	const float WorldBrushRadius = BrushProperties ? BrushProperties->BrushRadius : 0.0f;
	const float MeshBoundsMax = static_cast<float>(TargetMesh->GetBounds().MaxDim());
	const float MaxScale = static_cast<float>(Transform.GetScale3D().GetMax());
	if (MeshBoundsMax <= KINDA_SMALL_NUMBER || MaxScale <= KINDA_SMALL_NUMBER)
	{
		return;
	}

	const float UVBrushSize = (WorldBrushRadius / MaxScale / MeshBoundsMax) * 2.0f;
	const FVector2D RTSize(RTResource->GetSizeXY().X, RTResource->GetSizeXY().Y);
	const FVector2D PixelPos(Sample.UV.X * RTSize.X, Sample.UV.Y * RTSize.Y);
	const FVector2D PixelSize(UVBrushSize * RTSize.X, UVBrushSize * RTSize.Y);
	const FLinearColor PaintColor = Properties->bPaintShadow ? FLinearColor::Black : FLinearColor::White;
	const FVector2D StampPos = PixelPos - (PixelSize * 0.5f);

	FCanvasTileItem BrushItem(StampPos, GWhiteTexture, PixelSize, PaintColor);
	BrushItem.BlendMode = SE_BLEND_Translucent;

	Canvas.DrawItem(BrushItem);
	Canvas.Flush_GameThread(true);

	ENQUEUE_RENDER_COMMAND(UpdateSDFPaintRTCommand)(
		[RTResource](FRHICommandListImmediate& RHICmdList)
		{
			TransitionAndCopyTexture(RHICmdList, RTResource->GetRenderTargetTexture(), RTResource->TextureRHI, {});
		});
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
	if (NumSamples < 2)
	{
		return;
	}

	const FQuickSDFStrokeSample& P0 = StrokeSamples[NumSamples >= 3 ? NumSamples - 3 : NumSamples - 2];
	const FQuickSDFStrokeSample& P1 = StrokeSamples[NumSamples - 2];
	const FQuickSDFStrokeSample& P2 = StrokeSamples[NumSamples - 1];
	const FQuickSDFStrokeSample& P3 = StrokeSamples[NumSamples - 1];
	StampInterpolatedSegment(P0, P1, P2, P3);

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
	const double SegmentLength = FVector3d::Distance(P1.WorldPos, P2.WorldPos);
	if (SegmentLength <= KINDA_SMALL_NUMBER)
	{
		return;
	}

	const double Spacing = GetStrokeSpacingWorld(BrushProperties);
	const int32 NumSteps = FMath::Max(8, FMath::CeilToInt(SegmentLength / Spacing) * 4);

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
	DistanceSinceLastStamp = 0.0;
}

void UQuickSDFPaintTool::OnPropertyModified(UObject* PropertySet, FProperty* Property)
{
	Super::OnPropertyModified(PropertySet, Property);

	if (PropertySet == Properties)
	{
		if (Property && Property->GetFName() == GET_MEMBER_NAME_CHECKED(UQuickSDFToolProperties, EditAngleIndex))
		{
			if (PreviewMaterial && Properties->TransientRenderTargets.Num() > 0)
			{
				const int32 AngleIdx = FMath::Clamp(Properties->EditAngleIndex, 0, Properties->TransientRenderTargets.Num() - 1);
				if (Properties->TransientRenderTargets.IsValidIndex(AngleIdx))
				{
					PreviewMaterial->SetTextureParameterValue(TEXT("BaseColor"), Properties->TransientRenderTargets[AngleIdx]);
				}
			}
		}
	}
}

void UQuickSDFPaintTool::DrawHUD(FCanvas* Canvas, IToolsContextRenderAPI* RenderAPI)
{
	UQuickSDFToolProperties* Props = Properties;
	if (Props && Props->TransientRenderTargets.IsValidIndex(Props->EditAngleIndex))
	{
		UTextureRenderTarget2D* RT = Props->TransientRenderTargets[Props->EditAngleIndex];
		if (RT)
		{
			FCanvasTileItem TileItem(FVector2D(10, 10), RT->GetResource(), FVector2D(256, 256), FLinearColor::White);
			TileItem.BlendMode = SE_BLEND_Opaque;
			Canvas->DrawItem(TileItem);
		}
	}
}
