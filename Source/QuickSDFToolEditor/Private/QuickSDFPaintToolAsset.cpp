#include "QuickSDFPaintTool.h"
#include "QuickSDFPaintToolPrivate.h"
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
#include "Framework/Notifications/NotificationManager.h"
#include "IDesktopPlatform.h"
#include "IImageWrapper.h"
#include "IImageWrapperModule.h"
#include "HAL/FileManager.h"
#include "Misc/DefaultValueHelper.h"
#include "Misc/FileHelper.h"
#include "Containers/Ticker.h"
#include "Misc/Paths.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetToolsModule.h"
#include "IAssetTools.h"
#include "Misc/PackageName.h"
#include "ObjectTools.h"
#include "Widgets/Notifications/SNotificationList.h"

#if WITH_EDITOR
#include "Editor.h"
#include "IMaterialBakingModule.h"
#include "MaterialBakingStructures.h"
#endif

#define LOCTEXT_NAMESPACE "QuickSDFPaintTool"

using namespace QuickSDFPaintToolPrivate;

namespace
{
bool IsEngineTexture(const UTexture2D* Texture)
{
	return Texture &&
		(Texture->GetPathName().Equals(TEXT("/Engine"), ESearchCase::IgnoreCase) ||
			Texture->GetPathName().StartsWith(TEXT("/Engine/"), ESearchCase::IgnoreCase));
}

bool ResizeMaskPixelsBilinear(
	const TArray<FColor>& SourcePixels,
	int32 SourceWidth,
	int32 SourceHeight,
	int32 TargetWidth,
	int32 TargetHeight,
	TArray<FColor>& OutPixels)
{
	if (SourcePixels.Num() != SourceWidth * SourceHeight ||
		SourceWidth <= 0 || SourceHeight <= 0 ||
		TargetWidth <= 0 || TargetHeight <= 0)
	{
		return false;
	}

	if (SourceWidth == TargetWidth && SourceHeight == TargetHeight)
	{
		OutPixels = SourcePixels;
		return true;
	}

	OutPixels.SetNum(TargetWidth * TargetHeight);
	auto SampleChannel = [](uint8 C00, uint8 C10, uint8 C01, uint8 C11, float Tx, float Ty)
	{
		const float Top = FMath::Lerp(static_cast<float>(C00), static_cast<float>(C10), Tx);
		const float Bottom = FMath::Lerp(static_cast<float>(C01), static_cast<float>(C11), Tx);
		return static_cast<uint8>(FMath::Clamp(FMath::RoundToInt(FMath::Lerp(Top, Bottom, Ty)), 0, 255));
	};

	for (int32 Y = 0; Y < TargetHeight; ++Y)
	{
		const float SourceY = ((static_cast<float>(Y) + 0.5f) * SourceHeight / TargetHeight) - 0.5f;
		const int32 Y0 = FMath::Clamp(FMath::FloorToInt(SourceY), 0, SourceHeight - 1);
		const int32 Y1 = FMath::Clamp(Y0 + 1, 0, SourceHeight - 1);
		const float Ty = FMath::Clamp(SourceY - FMath::FloorToFloat(SourceY), 0.0f, 1.0f);

		for (int32 X = 0; X < TargetWidth; ++X)
		{
			const float SourceX = ((static_cast<float>(X) + 0.5f) * SourceWidth / TargetWidth) - 0.5f;
			const int32 X0 = FMath::Clamp(FMath::FloorToInt(SourceX), 0, SourceWidth - 1);
			const int32 X1 = FMath::Clamp(X0 + 1, 0, SourceWidth - 1);
			const float Tx = FMath::Clamp(SourceX - FMath::FloorToFloat(SourceX), 0.0f, 1.0f);

			const FColor& C00 = SourcePixels[Y0 * SourceWidth + X0];
			const FColor& C10 = SourcePixels[Y0 * SourceWidth + X1];
			const FColor& C01 = SourcePixels[Y1 * SourceWidth + X0];
			const FColor& C11 = SourcePixels[Y1 * SourceWidth + X1];
			FColor& Target = OutPixels[Y * TargetWidth + X];
			Target.R = SampleChannel(C00.R, C10.R, C01.R, C11.R, Tx, Ty);
			Target.G = SampleChannel(C00.G, C10.G, C01.G, C11.G, Tx, Ty);
			Target.B = SampleChannel(C00.B, C10.B, C01.B, C11.B, Tx, Ty);
			Target.A = SampleChannel(C00.A, C10.A, C01.A, C11.A, Tx, Ty);
		}
	}

	return true;
}

uint16 Float16ChannelToPng16(const FFloat16& Channel)
{
	const float Clamped = FMath::Clamp(Channel.GetFloat(), 0.0f, 1.0f);
	return static_cast<uint16>(FMath::Clamp(FMath::RoundToInt(Clamped * 65535.0f), 0, 65535));
}

FString MakeUniqueSDFExportFilePath(const FString& OutputFolder, const FString& FileBaseName)
{
	const FString CleanBaseName = FPaths::MakeValidFileName(FileBaseName.IsEmpty() ? FString(TEXT("T_QuickSDF_ThresholdMap")) : FileBaseName);
	FString CandidatePath = OutputFolder / FString::Printf(TEXT("%s.png"), *CleanBaseName);
	for (int32 Suffix = 2; IFileManager::Get().FileExists(*CandidatePath); ++Suffix)
	{
		CandidatePath = OutputFolder / FString::Printf(TEXT("%s_%d.png"), *CleanBaseName, Suffix);
	}
	return CandidatePath;
}

bool PromptForSDFFileExportFolder(FString& OutFolder)
{
	IDesktopPlatform* DesktopPlatform = FDesktopPlatformModule::Get();
	if (!DesktopPlatform)
	{
		FMessageDialog::Open(EAppMsgType::Ok, LOCTEXT("SDFFileExportNoDesktopPlatform", "Cannot open a folder picker for SDF file export."));
		return false;
	}

	const void* ParentWindowHandle = nullptr;
	if (FSlateApplication::IsInitialized())
	{
		ParentWindowHandle = FSlateApplication::Get().FindBestParentWindowHandleForDialogs(nullptr);
	}

	return DesktopPlatform->OpenDirectoryDialog(
		ParentWindowHandle,
		LOCTEXT("SDFFileExportChooseFolder", "Choose Folder for SDF PNG Export").ToString(),
		FPaths::ProjectSavedDir(),
		OutFolder);
}

bool SaveSDFPixelsToPngFile(
	const TArray<FFloat16Color>& Pixels,
	int32 Width,
	int32 Height,
	ESDFOutputFormat TextureSaveFormat,
	bool bForceRGBA16F,
	const FString& OutputFolder,
	const FString& OutputTextureName,
	FString& OutSavedPath,
	FText& OutError)
{
	if (Width <= 0 || Height <= 0 || Pixels.Num() != Width * Height)
	{
		OutError = LOCTEXT("SDFFileExportInvalidPixels", "Generated SDF pixels are invalid.");
		return false;
	}

	if (!IFileManager::Get().MakeDirectory(*OutputFolder, true))
	{
		OutError = FText::Format(LOCTEXT("SDFFileExportCreateFolderFailed", "Failed to create SDF export folder:\n{0}"), FText::FromString(OutputFolder));
		return false;
	}

	IImageWrapperModule& ImageWrapperModule = FModuleManager::LoadModuleChecked<IImageWrapperModule>("ImageWrapper");
	TSharedPtr<IImageWrapper> ImageWrapper = ImageWrapperModule.CreateImageWrapper(EImageFormat::PNG);
	if (!ImageWrapper.IsValid())
	{
		OutError = LOCTEXT("SDFFileExportNoImageWrapper", "Failed to create a PNG encoder for SDF file export.");
		return false;
	}

	const bool bWriteGrayscale = TextureSaveFormat == ESDFOutputFormat::Monopolar && !bForceRGBA16F;
	bool bEncoded = false;
	if (bWriteGrayscale)
	{
		TArray<uint16> GrayPixels;
		GrayPixels.SetNumUninitialized(Pixels.Num());
		for (int32 Index = 0; Index < Pixels.Num(); ++Index)
		{
			GrayPixels[Index] = Float16ChannelToPng16(Pixels[Index].R);
		}

		bEncoded = ImageWrapper->SetRaw(
			GrayPixels.GetData(),
			GrayPixels.Num() * sizeof(uint16),
			Width,
			Height,
			ERGBFormat::Gray,
			16);
	}
	else
	{
		TArray<uint16> RgbaPixels;
		RgbaPixels.SetNumUninitialized(Pixels.Num() * 4);
		for (int32 Index = 0; Index < Pixels.Num(); ++Index)
		{
			const int32 BaseIndex = Index * 4;
			RgbaPixels[BaseIndex + 0] = Float16ChannelToPng16(Pixels[Index].R);
			RgbaPixels[BaseIndex + 1] = Float16ChannelToPng16(Pixels[Index].G);
			RgbaPixels[BaseIndex + 2] = Float16ChannelToPng16(Pixels[Index].B);
			RgbaPixels[BaseIndex + 3] = Float16ChannelToPng16(Pixels[Index].A);
		}

		bEncoded = ImageWrapper->SetRaw(
			RgbaPixels.GetData(),
			RgbaPixels.Num() * sizeof(uint16),
			Width,
			Height,
			ERGBFormat::RGBA,
			16);
	}

	if (!bEncoded)
	{
		OutError = LOCTEXT("SDFFileExportEncodeFailed", "Failed to encode the generated SDF as PNG.");
		return false;
	}

	const TArray64<uint8> PngData = ImageWrapper->GetCompressed();
	OutSavedPath = MakeUniqueSDFExportFilePath(OutputFolder, OutputTextureName);
	if (PngData.Num() == 0 || !FFileHelper::SaveArrayToFile(PngData, *OutSavedPath))
	{
		OutError = FText::Format(LOCTEXT("SDFFileExportSaveFailed", "Failed to save SDF PNG:\n{0}"), FText::FromString(OutSavedPath));
		return false;
	}

	return true;
}

struct FQuickSDFIslandBuildData
{
	FQuickSDFIslandMirrorChart Chart;
	TArray<int32> TriangleIDs;
};

bool IsPointInUVTriangle(const FVector2f& P, const FVector2f& A, const FVector2f& B, const FVector2f& C)
{
	const FVector2f V0 = B - A;
	const FVector2f V1 = C - A;
	const FVector2f V2 = P - A;
	const float Denom = V0.X * V1.Y - V1.X * V0.Y;
	if (FMath::Abs(Denom) <= SMALL_NUMBER)
	{
		return false;
	}

	const float InvDenom = 1.0f / Denom;
	const float U = (V2.X * V1.Y - V1.X * V2.Y) * InvDenom;
	const float V = (V0.X * V2.Y - V2.X * V0.Y) * InvDenom;
	const float W = 1.0f - U - V;
	constexpr float Tolerance = -0.0005f;
	return U >= Tolerance && V >= Tolerance && W >= Tolerance;
}

FString MakeIslandKey(int32 UVChannel, const TArray<FIntPoint>& QuantizedUVs)
{
	uint32 Hash = ::GetTypeHash(UVChannel);
	for (const FIntPoint& UV : QuantizedUVs)
	{
		Hash = HashCombine(Hash, HashCombine(::GetTypeHash(UV.X), ::GetTypeHash(UV.Y)));
	}
	return FString::Printf(TEXT("UV%d_%08x"), UVChannel, Hash);
}

void BuildIslandShapeMask(
	const UE::Geometry::FDynamicMesh3& Mesh,
	const UE::Geometry::FDynamicMeshUVOverlay& UVOverlay,
	const FQuickSDFIslandBuildData& Island,
	TArray<uint8>& OutMask)
{
	constexpr int32 ShapeSize = 16;
	OutMask.SetNumZeroed(ShapeSize * ShapeSize);
	const FVector2f Size(
		FMath::Max(Island.Chart.UVMax.X - Island.Chart.UVMin.X, KINDA_SMALL_NUMBER),
		FMath::Max(Island.Chart.UVMax.Y - Island.Chart.UVMin.Y, KINDA_SMALL_NUMBER));
	const auto ToLocalUV = [&Island, Size](const FVector2f& UV)
	{
		return FVector2f(
			(UV.X - Island.Chart.UVMin.X) / Size.X,
			(UV.Y - Island.Chart.UVMin.Y) / Size.Y);
	};

	for (int32 TriangleID : Island.TriangleIDs)
	{
		if (!Mesh.IsTriangle(TriangleID) || !UVOverlay.IsSetTriangle(TriangleID))
		{
			continue;
		}

		const UE::Geometry::FIndex3i UVIndices = UVOverlay.GetTriangle(TriangleID);
		const FVector2f UV0 = ToLocalUV(UVOverlay.GetElement(UVIndices.A));
		const FVector2f UV1 = ToLocalUV(UVOverlay.GetElement(UVIndices.B));
		const FVector2f UV2 = ToLocalUV(UVOverlay.GetElement(UVIndices.C));
		const int32 MinX = FMath::Clamp(FMath::FloorToInt(FMath::Min3(UV0.X, UV1.X, UV2.X) * ShapeSize), 0, ShapeSize - 1);
		const int32 MaxX = FMath::Clamp(FMath::CeilToInt(FMath::Max3(UV0.X, UV1.X, UV2.X) * ShapeSize), 0, ShapeSize - 1);
		const int32 MinY = FMath::Clamp(FMath::FloorToInt(FMath::Min3(UV0.Y, UV1.Y, UV2.Y) * ShapeSize), 0, ShapeSize - 1);
		const int32 MaxY = FMath::Clamp(FMath::CeilToInt(FMath::Max3(UV0.Y, UV1.Y, UV2.Y) * ShapeSize), 0, ShapeSize - 1);

		for (int32 Y = MinY; Y <= MaxY; ++Y)
		{
			for (int32 X = MinX; X <= MaxX; ++X)
			{
				const FVector2f P((static_cast<float>(X) + 0.5f) / ShapeSize, (static_cast<float>(Y) + 0.5f) / ShapeSize);
				if (IsPointInUVTriangle(P, UV0, UV1, UV2))
				{
					OutMask[Y * ShapeSize + X] = 1;
				}
			}
		}
	}
}

float CompareIslandShapeMasks(
	const TArray<uint8>& SourceMask,
	const TArray<uint8>& TargetMask,
	EQuickSDFIslandMirrorTransform Transform)
{
	constexpr int32 ShapeSize = 16;
	if (SourceMask.Num() != ShapeSize * ShapeSize || TargetMask.Num() != ShapeSize * ShapeSize)
	{
		return 0.0f;
	}

	int32 MatchingCells = 0;
	for (int32 Y = 0; Y < ShapeSize; ++Y)
	{
		for (int32 X = 0; X < ShapeSize; ++X)
		{
			const FVector2f TargetLocal((static_cast<float>(X) + 0.5f) / ShapeSize, (static_cast<float>(Y) + 0.5f) / ShapeSize);
			const FVector2f SourceLocal = TransformIslandMirrorLocalUV(TargetLocal, Transform);
			const int32 SourceX = FMath::Clamp(FMath::FloorToInt(SourceLocal.X * ShapeSize), 0, ShapeSize - 1);
			const int32 SourceY = FMath::Clamp(FMath::FloorToInt(SourceLocal.Y * ShapeSize), 0, ShapeSize - 1);
			if (TargetMask[Y * ShapeSize + X] == SourceMask[SourceY * ShapeSize + SourceX])
			{
				++MatchingCells;
			}
		}
	}

	return static_cast<float>(MatchingCells) / static_cast<float>(ShapeSize * ShapeSize);
}

EQuickSDFIslandMirrorTransform GetInverseIslandMirrorTransform(EQuickSDFIslandMirrorTransform Transform)
{
	switch (Transform)
	{
	case EQuickSDFIslandMirrorTransform::SwapUVFlipU:
		return EQuickSDFIslandMirrorTransform::SwapUVFlipV;
	case EQuickSDFIslandMirrorTransform::SwapUVFlipV:
		return EQuickSDFIslandMirrorTransform::SwapUVFlipU;
	case EQuickSDFIslandMirrorTransform::FlipU:
	case EQuickSDFIslandMirrorTransform::FlipV:
	case EQuickSDFIslandMirrorTransform::Rotate180:
	default:
		return Transform;
	}
}

bool BuildIslandMirrorData(
	const UE::Geometry::FDynamicMesh3& Mesh,
	int32 UVChannel,
	const TMap<int32, int32>& TriangleChartIDs,
	int32 Width,
	int32 Height,
	TArray<FQuickSDFIslandMirrorChart>& OutCharts,
	TArray<int32>& OutPixelChartIDs,
	TArray<uint8>& OutAmbiguousPixelFlags)
{
	OutCharts.Reset();
	OutPixelChartIDs.Init(INDEX_NONE, Width * Height);
	OutAmbiguousPixelFlags.Init(0, Width * Height);
	if (Width <= 0 || Height <= 0 || !Mesh.HasAttributes())
	{
		return false;
	}

	const UE::Geometry::FDynamicMeshUVOverlay* UVOverlay = Mesh.Attributes()->GetUVLayer(UVChannel);
	if (!UVOverlay)
	{
		return false;
	}

	TMap<int32, FQuickSDFIslandBuildData> IslandsByChartID;
	TMap<int32, TArray<FIntPoint>> QuantizedUVsByChartID;
	for (const TPair<int32, int32>& TriangleChartPair : TriangleChartIDs)
	{
		const int32 TriangleID = TriangleChartPair.Key;
		const int32 ChartID = TriangleChartPair.Value;
		if (!Mesh.IsTriangle(TriangleID) || !UVOverlay->IsSetTriangle(TriangleID))
		{
			continue;
		}

		FQuickSDFIslandBuildData& Island = IslandsByChartID.FindOrAdd(ChartID);
		Island.Chart.ChartID = ChartID;
		Island.TriangleIDs.Add(TriangleID);

		const UE::Geometry::FIndex3i UVIndices = UVOverlay->GetTriangle(TriangleID);
		const FVector2f UVs[3] = {
			UVOverlay->GetElement(UVIndices.A),
			UVOverlay->GetElement(UVIndices.B),
			UVOverlay->GetElement(UVIndices.C)
		};

		for (int32 UVIndex = 0; UVIndex < 3; ++UVIndex)
		{
			const FVector2f& UV = UVs[UVIndex];
			if (Island.TriangleIDs.Num() == 1 && UVIndex == 0)
			{
				Island.Chart.UVMin = UV;
				Island.Chart.UVMax = UV;
			}
			else
			{
				Island.Chart.UVMin.X = FMath::Min(Island.Chart.UVMin.X, UV.X);
				Island.Chart.UVMin.Y = FMath::Min(Island.Chart.UVMin.Y, UV.Y);
				Island.Chart.UVMax.X = FMath::Max(Island.Chart.UVMax.X, UV.X);
				Island.Chart.UVMax.Y = FMath::Max(Island.Chart.UVMax.Y, UV.Y);
			}
			Island.Chart.bOutOfRange = Island.Chart.bOutOfRange || UV.X < 0.0f || UV.X > 1.0f || UV.Y < 0.0f || UV.Y > 1.0f;
			QuantizedUVsByChartID.FindOrAdd(ChartID).AddUnique(QuantizeUVForOverlay(UV));
		}

		const float SignedArea = 0.5f * FMath::Abs(
			(UVs[1].X - UVs[0].X) * (UVs[2].Y - UVs[0].Y) -
			(UVs[2].X - UVs[0].X) * (UVs[1].Y - UVs[0].Y));
		Island.Chart.Area += SignedArea;
	}

	TArray<FQuickSDFIslandBuildData> Islands;
	IslandsByChartID.GenerateValueArray(Islands);
	for (FQuickSDFIslandBuildData& Island : Islands)
	{
		TArray<FIntPoint>& QuantizedUVs = QuantizedUVsByChartID.FindOrAdd(Island.Chart.ChartID);
		QuantizedUVs.Sort([](const FIntPoint& A, const FIntPoint& B)
		{
			return A.X == B.X ? A.Y < B.Y : A.X < B.X;
		});
		Island.Chart.Key = MakeIslandKey(UVChannel, QuantizedUVs);
		const FVector2f Size = Island.Chart.UVMax - Island.Chart.UVMin;
		Island.Chart.AspectRatio = FMath::Abs(Size.Y) > KINDA_SMALL_NUMBER ? FMath::Abs(Size.X / Size.Y) : 1.0f;
		BuildIslandShapeMask(Mesh, *UVOverlay, Island, Island.Chart.ShapeMask);
		OutCharts.Add(Island.Chart);
	}

	for (const FQuickSDFIslandBuildData& Island : Islands)
	{
		for (int32 TriangleID : Island.TriangleIDs)
		{
			if (!Mesh.IsTriangle(TriangleID) || !UVOverlay->IsSetTriangle(TriangleID))
			{
				continue;
			}

			const UE::Geometry::FIndex3i UVIndices = UVOverlay->GetTriangle(TriangleID);
			const FVector2f UV0 = UVOverlay->GetElement(UVIndices.A);
			const FVector2f UV1 = UVOverlay->GetElement(UVIndices.B);
			const FVector2f UV2 = UVOverlay->GetElement(UVIndices.C);
			const int32 MinX = FMath::Clamp(FMath::FloorToInt(FMath::Min3(UV0.X, UV1.X, UV2.X) * Width), 0, Width - 1);
			const int32 MaxX = FMath::Clamp(FMath::CeilToInt(FMath::Max3(UV0.X, UV1.X, UV2.X) * Width), 0, Width - 1);
			const int32 MinY = FMath::Clamp(FMath::FloorToInt(FMath::Min3(UV0.Y, UV1.Y, UV2.Y) * Height), 0, Height - 1);
			const int32 MaxY = FMath::Clamp(FMath::CeilToInt(FMath::Max3(UV0.Y, UV1.Y, UV2.Y) * Height), 0, Height - 1);

			for (int32 Y = MinY; Y <= MaxY; ++Y)
			{
				for (int32 X = MinX; X <= MaxX; ++X)
				{
					const FVector2f UV((static_cast<float>(X) + 0.5f) / Width, (static_cast<float>(Y) + 0.5f) / Height);
					if (!IsPointInUVTriangle(UV, UV0, UV1, UV2))
					{
						continue;
					}

					const int32 PixelIndex = Y * Width + X;
					if (OutPixelChartIDs[PixelIndex] != INDEX_NONE && OutPixelChartIDs[PixelIndex] != Island.Chart.ChartID)
					{
						OutAmbiguousPixelFlags[PixelIndex] = 1;
					}
					OutPixelChartIDs[PixelIndex] = Island.Chart.ChartID;
				}
			}
		}
	}

	return OutCharts.Num() > 0;
}

void AutoBuildIslandMirrorPairs(
	const TArray<FQuickSDFIslandMirrorChart>& Charts,
	const TArray<FQuickSDFIslandMirrorPair>& ExistingPairs,
	TArray<FQuickSDFIslandMirrorPair>& OutPairs)
{
	OutPairs.Reset();
	TSet<FString> LockedTargets;
	for (const FQuickSDFIslandMirrorPair& Pair : ExistingPairs)
	{
		if (Pair.bUserLocked)
		{
			OutPairs.Add(Pair);
			LockedTargets.Add(Pair.TargetIslandKey);
		}
	}

	for (const FQuickSDFIslandMirrorChart& Chart : Charts)
	{
		if (LockedTargets.Contains(Chart.Key))
		{
			continue;
		}

		FQuickSDFIslandMirrorPair SelfPair;
		SelfPair.SourceIslandKey = Chart.Key;
		SelfPair.TargetIslandKey = Chart.Key;
		SelfPair.Transform = EQuickSDFIslandMirrorTransform::FlipU;
		SelfPair.Confidence = 1.0f;
		OutPairs.Add(SelfPair);
	}
}
}

void UQuickSDFPaintTool::GenerateSDF()
{
	GenerateSDFInternal(true, false);
}

void UQuickSDFPaintTool::GenerateSDFToFile()
{
	GenerateSDFInternal(false, true);
}

void UQuickSDFPaintTool::GenerateSDFInternal(bool bSaveAsset, bool bPromptForFileExport)
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
	const int32 OrigW = Asset->GetActiveResolution().X;
	const int32 OrigH = Asset->GetActiveResolution().Y;
	if (OrigW <= 0 || OrigH <= 0)
	{
		return;
	}

	Properties->SyncLegacySymmetryFlag();
	const bool bFrontHalfAngles = Properties->UsesFrontHalfAngles();
	const bool bIslandChannelSymmetry = Properties->UsesIslandChannelSymmetry();
	if (FQuickSDFTextureSetData* ActiveSet = Asset->GetActiveTextureSet())
	{
		ActiveSet->bHasWarning = false;
		ActiveSet->WarningMessage = FText::GetEmpty();
	}
	const TArray<int32> ProcessableIndices = CollectProcessableMaskIndices(*Asset, bFrontHalfAngles);
	if (ProcessableIndices.Num() == 0)
	{
		return;
	}

	FString SDFFileOutputFolder;
	if (bPromptForFileExport && !PromptForSDFFileExportFolder(SDFFileOutputFolder))
	{
		return;
	}
	const bool bExportFile = !SDFFileOutputFolder.IsEmpty();
	if (!bSaveAsset && !bExportFile)
	{
		return;
	}

	WarnIfMonotonicGuardViolations(LOCTEXT("MonotonicGuardBeforeGenerateContext", "before SDF generation"));

	// --- 繝励Ο繧ｰ繝ｬ繧ｹ繝舌・縺ｮ蛻晄悄蛹・---
	// 蟾･遞具ｼ售DF逕滓・(ValidIndices.Num()) + 蜷域・(1) + 菫晏ｭ・1)
	FScopedSlowTask SlowTask(static_cast<float>(ProcessableIndices.Num()) + 2.0f, LOCTEXT("GenerateSDF", "Generating Multi-Channel SDF..."));
	SlowTask.MakeDialog(true);

	// --- 1. SDF繝・・繧ｿ縺ｮ逕滓・縺ｨ蜿朱寔 ---
	TArray<FMaskData> ProcessedData;
	const int32 Upscale = FMath::Clamp(Properties->UpscaleFactor, 1, 8);
	const int32 HighW = OrigW * Upscale;
	const int32 HighH = OrigH * Upscale;
	const float MaxAngle = Properties->GetPaintMaxAngle();

	for (int32 Index : ProcessableIndices)
	{
		const float RawAngle = Asset->GetActiveAngleDataList()[Index].Angle;
		// 繝励Ο繧ｰ繝ｬ繧ｹ繝舌・譖ｴ譁ｰ
		SlowTask.EnterProgressFrame(1.f, FText::Format(LOCTEXT("ProcessMask", "Processing Mask {0}..."), Index));
		if (SlowTask.ShouldCancel())
		{
			return;
		}

		FMaskData Data;
		if (TryBuildMaskData(*this, Asset->GetActiveAngleDataList()[Index].PaintRenderTarget, RawAngle, MaxAngle, OrigW, OrigH, Upscale, Data))
		{
			ProcessedData.Add(MoveTemp(Data));
		}
	}

	if (ProcessedData.Num() == 0)
	{
		return;
	}

	SortMaskData(ProcessedData);

	// --- 2. Bipolar縺ｮ閾ｪ蜍募愛螳・---
	const bool bNeedsBipolar = NeedsBipolarOutput(ProcessedData, HighW * HighH);
	const ESDFOutputFormat EffectiveFormat = bNeedsBipolar ? ESDFOutputFormat::Bipolar : ESDFOutputFormat::Monopolar;
	UE_LOG(LogTemp, Warning, TEXT("QuickSDF: Auto-Detected Format: %s"), bNeedsBipolar ? TEXT("BIPOLAR") : TEXT("MONOPOLAR"));

	// --- 3. 蜷域・蜃ｦ逅・---
	SlowTask.EnterProgressFrame(1.f, LOCTEXT("CombineSDF", "Combining SDF Channels..."));
	if (SlowTask.ShouldCancel())
	{
		return;
	}

	TArray<FVector4f> CombinedField;
	FSDFProcessor::CombineSDFs(ProcessedData, CombinedField, HighW, HighH, EffectiveFormat, bFrontHalfAngles);

	FQuickSDFIslandMirrorApplyResult IslandMirrorResult;
	if (bIslandChannelSymmetry)
	{
		FQuickSDFTextureSetData* ActiveSet = Asset->GetActiveTextureSet();
		TArray<FQuickSDFIslandMirrorChart> IslandCharts;
		TArray<int32> PixelChartIDs;
		TArray<uint8> AmbiguousPixelFlags;
		bool bBuiltIslandData = false;
		if (TargetMesh.IsValid())
		{
			EnsurePaintChartCache();
			bBuiltIslandData = BuildIslandMirrorData(
				*TargetMesh,
				Properties->UVChannel,
				TargetTrianglePaintChartIDs,
				HighW,
				HighH,
				IslandCharts,
				PixelChartIDs,
				AmbiguousPixelFlags);
		}

		if (bBuiltIslandData && ActiveSet)
		{
			int32 OutOfRangeIslandCount = 0;
			for (const FQuickSDFIslandMirrorChart& Chart : IslandCharts)
			{
				if (Chart.bOutOfRange)
				{
					++OutOfRangeIslandCount;
				}
			}

			TArray<FQuickSDFIslandMirrorPair> RefreshedPairs;
			AutoBuildIslandMirrorPairs(IslandCharts, ActiveSet->IslandMirrorPairs, RefreshedPairs);
			Asset->Modify();
			ActiveSet->IslandMirrorPairs = MoveTemp(RefreshedPairs);
			IslandMirrorResult = ApplyIslandMirrorToCombinedField(
				CombinedField,
				HighW,
				HighH,
				EffectiveFormat == ESDFOutputFormat::Bipolar,
				IslandCharts,
				PixelChartIDs,
				AmbiguousPixelFlags,
				ActiveSet->IslandMirrorPairs);

			if (IslandMirrorResult.MissingPairPixels > 0 ||
				IslandMirrorResult.MissingSourcePixels > 0 ||
				IslandMirrorResult.AmbiguousPixels > 0 ||
				OutOfRangeIslandCount > 0)
			{
				ActiveSet->bHasWarning = true;
				ActiveSet->WarningMessage = FText::Format(
					LOCTEXT("IslandMirrorGenerateWarning", "Island Mirror used fallback pixels. Missing pairs: {0}, missing sources: {1}, ambiguous UV pixels: {2}, out-of-range islands: {3}."),
					FText::AsNumber(IslandMirrorResult.MissingPairPixels),
					FText::AsNumber(IslandMirrorResult.MissingSourcePixels),
					FText::AsNumber(IslandMirrorResult.AmbiguousPixels),
					FText::AsNumber(OutOfRangeIslandCount));
			}
		}
		else if (ActiveSet)
		{
			ActiveSet->bHasWarning = true;
			ActiveSet->WarningMessage = LOCTEXT("IslandMirrorNoMeshWarning", "Island Mirror needs a selected mesh with a valid UV channel. 90-180 channels were copied from 0-90.");
			TArray<int32> FallbackCharts;
			TArray<uint8> FallbackAmbiguous;
			FallbackCharts.Init(INDEX_NONE, HighW * HighH);
			FallbackAmbiguous.Init(0, HighW * HighH);
			IslandMirrorResult = ApplyIslandMirrorToCombinedField(
				CombinedField,
				HighW,
				HighH,
				EffectiveFormat == ESDFOutputFormat::Bipolar,
				IslandCharts,
				FallbackCharts,
				FallbackAmbiguous,
				ActiveSet->IslandMirrorPairs);
		}
	}

	// --- 4. 菫晏ｭ伜・逅・---
	SlowTask.EnterProgressFrame(1.f, LOCTEXT("SaveSDF", "Downscaling and Saving..."));
	if (SlowTask.ShouldCancel())
	{
		return;
	}

	const EQuickSDFThresholdMapOutputMode OutputMode = Properties->SDFOutputFormat;
	ESDFOutputFormat TextureSaveFormat = EffectiveFormat;
	bool bForceRGBA16F = bIslandChannelSymmetry;
	bool bSupportsGeneratedSDFPreview = true;
	TArray<FFloat16Color> FinalPixels;
	if (OutputMode == EQuickSDFThresholdMapOutputMode::LilToonCompatible)
	{
		EQuickSDFLilToonLeftChannelSource LeftChannelSource = EQuickSDFLilToonLeftChannelSource::InternalY;
		if (bIslandChannelSymmetry)
		{
			LeftChannelSource = EQuickSDFLilToonLeftChannelSource::InternalW;
		}
		else if (Properties->UsesWholeTextureSymmetry())
		{
			LeftChannelSource = EQuickSDFLilToonLeftChannelSource::MirroredX;
		}

		FinalPixels = FSDFProcessor::DownscaleAndConvertToLilToon(CombinedField, HighW, HighH, Upscale, LeftChannelSource);
		TextureSaveFormat = ESDFOutputFormat::Bipolar;
		bForceRGBA16F = true;
		bSupportsGeneratedSDFPreview = false;
	}
	else
	{
		FinalPixels = FSDFProcessor::DownscaleAndConvert(CombinedField, HighW, HighH, Upscale);
		if (OutputMode == EQuickSDFThresholdMapOutputMode::Grayscale)
		{
			TextureSaveFormat = ESDFOutputFormat::Monopolar;
			bForceRGBA16F = false;
		}
	}
	FText SaveError;
	FString OutputTextureName = Properties->SDFTextureName;
	if (const FQuickSDFTextureSetData* ActiveSet = Asset->GetActiveTextureSet())
	{
		const FString AssetName = Properties->QuickSDFAssetName.IsEmpty() ? FString(TEXT("QuickSDF")) : Properties->QuickSDFAssetName;
		const FString SlotName = ActiveSet->SlotName.IsNone()
			? FString::Printf(TEXT("Slot_%d"), ActiveSet->MaterialSlotIndex)
			: ActiveSet->SlotName.ToString();
		OutputTextureName = FString::Printf(
			TEXT("T_%s_%s_Threshold"),
			*ObjectTools::SanitizeObjectName(AssetName),
			*ObjectTools::SanitizeObjectName(SlotName));
	}

	if (bExportFile)
	{
		FString SavedPath;
		FText FileSaveError;
		if (SaveSDFPixelsToPngFile(
			FinalPixels,
			OrigW,
			OrigH,
			TextureSaveFormat,
			bForceRGBA16F,
			SDFFileOutputFolder,
			OutputTextureName,
			SavedPath,
			FileSaveError))
		{
			FPlatformProcess::ExploreFolder(*SDFFileOutputFolder);
		}
		else if (!FileSaveError.IsEmpty())
		{
			FMessageDialog::Open(EAppMsgType::Ok, FileSaveError);
		}
	}

	if (!bSaveAsset)
	{
		return;
	}

	UTexture2D* FinalTexture = Subsystem->CreateSDFTexture(FinalPixels, OrigW, OrigH, Properties->SDFOutputFolder, OutputTextureName, TextureSaveFormat, Properties->bOverwriteExistingSDF, &SaveError, bForceRGBA16F);
	if (FinalTexture)
	{
		const EQuickSDFMaterialPreviewMode PreviousPreviewMode = Properties->MaterialPreviewMode;
		Asset->Modify();
		Asset->GetActiveFinalSDFTexture() = FinalTexture;
		if (FQuickSDFTextureSetData* ActiveSet = Asset->GetActiveTextureSet())
		{
			ActiveSet->FinalSDFTexture = FinalTexture;
			ActiveSet->bDirty = false;
			ActiveSet->bInitialBakeComplete = true;
		}
		Asset->SyncLegacyFromActiveTextureSet();
		Asset->MarkPackageDirty();

		if (Properties->bAutoPreviewGeneratedSDF && bSupportsGeneratedSDFPreview)
		{
			Properties->Modify();
			Properties->MaterialPreviewMode = EQuickSDFMaterialPreviewMode::GeneratedSDF;
		}
		else if (!bSupportsGeneratedSDFPreview && Properties->MaterialPreviewMode == EQuickSDFMaterialPreviewMode::GeneratedSDF)
		{
			Properties->Modify();
			Properties->MaterialPreviewMode = EQuickSDFMaterialPreviewMode::Mask;
		}
		RefreshPreviewMaterial();
		if (bSupportsGeneratedSDFPreview)
		{
			ShowGeneratedSDFPreviewNotification(PreviousPreviewMode, FinalTexture);
		}
		if (GEditor)
		{
			GEditor->RedrawAllViewports(false);
		}
	}
	else if (!SaveError.IsEmpty())
	{
		FMessageDialog::Open(EAppMsgType::Ok, SaveError);
	}
}

void UQuickSDFPaintTool::ShowGeneratedSDFPreviewNotification(EQuickSDFMaterialPreviewMode PreviousMode, UTexture2D* FinalTexture)
{
	UQuickSDFToolSubsystem* Subsystem = GEditor ? GEditor->GetEditorSubsystem<UQuickSDFToolSubsystem>() : nullptr;
	const UQuickSDFAsset* Asset = Subsystem ? Subsystem->GetActiveSDFAsset() : nullptr;
	const FQuickSDFTextureSetData* ActiveSet = Asset ? Asset->GetActiveTextureSet() : nullptr;
	const FString SlotName = ActiveSet
		? (ActiveSet->SlotName.IsNone()
			? FString::Printf(TEXT("Slot_%d"), ActiveSet->MaterialSlotIndex)
			: ActiveSet->SlotName.ToString())
		: FString(TEXT("active slot"));

	FNotificationInfo Info(FText::Format(
		LOCTEXT("GeneratedSDFPreviewComplete", "Generated SDF preview for {0}"),
		FText::FromString(SlotName)));
	Info.ExpireDuration = 5.0f;
	Info.bUseLargeFont = false;
	Info.ButtonDetails.Add(FNotificationButtonInfo(
		LOCTEXT("RestorePreviousPreviewButton", "Restore Previous Preview"),
		LOCTEXT("RestorePreviousPreviewTooltip", "Restore the material preview mode that was active before SDF generation."),
		FSimpleDelegate::CreateWeakLambda(this, [this, PreviousMode]()
		{
			if (!Properties)
			{
				return;
			}
			Properties->Modify();
			Properties->MaterialPreviewMode = PreviousMode;
			FProperty* Prop = Properties->GetClass()->FindPropertyByName(GET_MEMBER_NAME_CHECKED(UQuickSDFToolProperties, MaterialPreviewMode));
			OnPropertyModified(Properties, Prop);
			if (GEditor)
			{
				GEditor->RedrawAllViewports(false);
			}
		}),
		SNotificationItem::CS_Success));
	Info.ButtonDetails.Add(FNotificationButtonInfo(
		LOCTEXT("PaintedTexturePreviewButton", "Painted Texture"),
		LOCTEXT("PaintedTexturePreviewTooltip", "Switch the material preview back to the painted texture."),
		FSimpleDelegate::CreateWeakLambda(this, [this]()
		{
			if (!Properties)
			{
				return;
			}
			Properties->Modify();
			Properties->MaterialPreviewMode = EQuickSDFMaterialPreviewMode::Mask;
			FProperty* Prop = Properties->GetClass()->FindPropertyByName(GET_MEMBER_NAME_CHECKED(UQuickSDFToolProperties, MaterialPreviewMode));
			OnPropertyModified(Properties, Prop);
			if (GEditor)
			{
				GEditor->RedrawAllViewports(false);
			}
		}),
		SNotificationItem::CS_Success));

	if (FinalTexture)
	{
		Info.SubText = FText::FromString(FinalTexture->GetName());
	}

	if (TSharedPtr<SNotificationItem> Notification = FSlateNotificationManager::Get().AddNotification(Info))
	{
		Notification->SetCompletionState(SNotificationItem::CS_Success);
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
	RequestImportPanel();
}

void UQuickSDFPaintTool::RequestImportPanel()
{
	bImportPanelRequested = true;
}

bool UQuickSDFPaintTool::ConsumeImportPanelRequest()
{
	const bool bRequested = bImportPanelRequested;
	bImportPanelRequested = false;
	return bRequested;
}

bool UQuickSDFPaintTool::AssignMaskTextureToAngle(int32 AngleIndex, UTexture2D* Texture, bool bAllowSourceTextureOverwrite)
{
	if (!Properties)
	{
		return false;
	}

	UQuickSDFToolSubsystem* Subsystem = GEditor->GetEditorSubsystem<UQuickSDFToolSubsystem>();
	UQuickSDFAsset* Asset = Subsystem ? Subsystem->GetActiveSDFAsset() : nullptr;
	if (!Asset || !Asset->GetActiveAngleDataList().IsValidIndex(AngleIndex))
	{
		return false;
	}

	Asset->InitializeRenderTargets(GetToolManager()->GetContextQueriesAPI()->GetCurrentEditingWorld());
	EnsureMaskGuids(Asset);
	FQuickSDFAngleData& AngleData = Asset->GetActiveAngleDataList()[AngleIndex];
	if (!AngleData.PaintRenderTarget)
	{
		return false;
	}

	TArray<FColor> BeforePixels;
	CaptureRenderTargetPixels(AngleData.PaintRenderTarget, BeforePixels);
	UTexture2D* BeforeTexture = AngleData.TextureMask;
	const bool bBeforeAllowSourceTextureOverwrite = AngleData.bAllowSourceTextureOverwrite;

	GetToolManager()->BeginUndoTransaction(LOCTEXT("AssignDroppedQuickSDFMask", "Assign Dropped Quick SDF Mask"));
	Asset->Modify();
	Properties->Modify();

	Properties->TargetTextures.SetNum(Asset->GetActiveAngleDataList().Num());
	Properties->TargetAngles.SetNum(Asset->GetActiveAngleDataList().Num());
	Properties->NumAngles = Asset->GetActiveAngleDataList().Num();
	for (int32 Index = 0; Index < Asset->GetActiveAngleDataList().Num(); ++Index)
	{
		Properties->TargetAngles[Index] = Asset->GetActiveAngleDataList()[Index].Angle;
		Properties->TargetTextures[Index] = Asset->GetActiveAngleDataList()[Index].TextureMask;
	}
	Properties->EditAngleIndex = FMath::Clamp(AngleIndex, 0, Asset->GetActiveAngleDataList().Num() - 1);
	Properties->TargetTextures[AngleIndex] = Texture;
	AngleData.TextureMask = Texture;
	AngleData.bAllowSourceTextureOverwrite = bAllowSourceTextureOverwrite;

	FProperty* EditProp = Properties->GetClass()->FindPropertyByName(GET_MEMBER_NAME_CHECKED(UQuickSDFToolProperties, EditAngleIndex));
	OnPropertyModified(Properties, EditProp);

	if (Texture)
	{
		Subsystem->DrawTextureToRenderTarget(Texture, AngleData.PaintRenderTarget);
	}
	else
	{
		Subsystem->ClearRenderTarget(AngleData.PaintRenderTarget);
	}

	TArray<FColor> AfterPixels;
	CaptureRenderTargetPixels(AngleData.PaintRenderTarget, AfterPixels);

	TUniquePtr<FQuickSDFTextureSlotChange> Change = MakeUnique<FQuickSDFTextureSlotChange>();
	Change->AngleIndex = AngleIndex;
	Change->AngleGuid = AngleData.MaskGuid;
	Change->BeforeTexture = BeforeTexture;
	Change->AfterTexture = Texture;
	Change->bBeforeAllowSourceTextureOverwrite = bBeforeAllowSourceTextureOverwrite;
	Change->bAfterAllowSourceTextureOverwrite = bAllowSourceTextureOverwrite;
	Change->BeforePixels = MoveTemp(BeforePixels);
	Change->AfterPixels = MoveTemp(AfterPixels);
	GetToolManager()->EmitObjectChange(this, MoveTemp(Change), LOCTEXT("AssignQuickSDFMaskTexture", "Assign Quick SDF Mask Texture"));

	RefreshPreviewMaterial();
	MarkMasksChanged();

	GetToolManager()->EndUndoTransaction();
	WarnIfMonotonicGuardViolations(LOCTEXT("MonotonicGuardAfterAssignTextureContext", "after assigning a mask texture"));
	return true;
}

void UQuickSDFPaintTool::OverwriteSourceTextures()
{
	if (!Properties)
	{
		return;
	}

	UQuickSDFToolSubsystem* Subsystem = GEditor ? GEditor->GetEditorSubsystem<UQuickSDFToolSubsystem>() : nullptr;
	UQuickSDFAsset* Asset = Subsystem ? Subsystem->GetActiveSDFAsset() : nullptr;
	if (!Subsystem || !Asset)
	{
		return;
	}

	Asset->InitializeRenderTargets(GetToolManager()->GetContextQueriesAPI()->GetCurrentEditingWorld());

	struct FOverwriteSourceTarget
	{
		int32 AngleIndex = INDEX_NONE;
		float Angle = 0.0f;
		UTexture2D* Texture = nullptr;
		UTextureRenderTarget2D* RenderTarget = nullptr;
		bool bResolutionMismatch = false;
	};

	TArray<FOverwriteSourceTarget> Targets;
	TMap<UTexture2D*, int32> TextureToFirstIndex;
	for (int32 Index = 0; Index < Asset->GetActiveAngleDataList().Num(); ++Index)
	{
		const FQuickSDFAngleData& AngleData = Asset->GetActiveAngleDataList()[Index];
		if (!AngleData.bAllowSourceTextureOverwrite)
		{
			continue;
		}
		if (!AngleData.TextureMask || !AngleData.PaintRenderTarget)
		{
			continue;
		}
		if (IsEngineTexture(AngleData.TextureMask))
		{
			FMessageDialog::Open(
				EAppMsgType::Ok,
				FText::Format(
					LOCTEXT("OverwriteEngineTextureBlocked", "Cannot overwrite {0}. Engine Texture2D assets are protected."),
					FText::FromString(AngleData.TextureMask->GetPathName())));
			return;
		}

		if (const int32* ExistingIndex = TextureToFirstIndex.Find(AngleData.TextureMask))
		{
			FMessageDialog::Open(
				EAppMsgType::Ok,
				FText::Format(
					LOCTEXT("OverwriteDuplicateTextureBlocked", "Cannot overwrite source textures because {0} is writable from multiple slots ({1} and {2}). Disable source overwrite on one slot first."),
					FText::FromString(AngleData.TextureMask->GetPathName()),
					FText::AsNumber(*ExistingIndex + 1),
					FText::AsNumber(Index + 1)));
			return;
		}
		TextureToFirstIndex.Add(AngleData.TextureMask, Index);

		FOverwriteSourceTarget& OverwriteTarget = Targets.AddDefaulted_GetRef();
		OverwriteTarget.AngleIndex = Index;
		OverwriteTarget.Angle = AngleData.Angle;
		OverwriteTarget.Texture = AngleData.TextureMask;
		OverwriteTarget.RenderTarget = AngleData.PaintRenderTarget;

		const int32 SourceWidth = AngleData.TextureMask->Source.IsValid() ? AngleData.TextureMask->Source.GetSizeX() : AngleData.TextureMask->GetSizeX();
		const int32 SourceHeight = AngleData.TextureMask->Source.IsValid() ? AngleData.TextureMask->Source.GetSizeY() : AngleData.TextureMask->GetSizeY();
		OverwriteTarget.bResolutionMismatch = SourceWidth > 0 && SourceHeight > 0 &&
			(SourceWidth != AngleData.PaintRenderTarget->SizeX || SourceHeight != AngleData.PaintRenderTarget->SizeY);
	}

	if (Targets.Num() == 0)
	{
		FMessageDialog::Open(EAppMsgType::Ok, LOCTEXT("NoWritableSourceTextures", "No writable source textures are assigned. Enable Allow source overwrite in the import preview first."));
		return;
	}

	FString ConfirmText = FString::Printf(TEXT("Overwrite %d source texture%s with the current mask pixels?\n\n"), Targets.Num(), Targets.Num() == 1 ? TEXT("") : TEXT("s"));
	const int32 MaxPreviewRows = 12;
	for (int32 TargetIndex = 0; TargetIndex < FMath::Min(Targets.Num(), MaxPreviewRows); ++TargetIndex)
	{
		const FOverwriteSourceTarget& OverwriteTarget = Targets[TargetIndex];
		ConfirmText += FString::Printf(
			TEXT("Slot %d / %.0f deg -> %s%s\n"),
			OverwriteTarget.AngleIndex + 1,
			OverwriteTarget.Angle,
			*OverwriteTarget.Texture->GetPathName(),
			OverwriteTarget.bResolutionMismatch ? TEXT(" (resolution will change)") : TEXT(""));
	}
	if (Targets.Num() > MaxPreviewRows)
	{
		ConfirmText += FString::Printf(TEXT("...and %d more\n"), Targets.Num() - MaxPreviewRows);
	}
	if (!IsPersistentQuickSDFAsset(Asset))
	{
		ConfirmText += TEXT("\nWarning: the active QuickSDF asset has not been saved, so source-overwrite permissions may not persist until you save it.");
	}
	ConfirmText += TEXT("\nThis cannot restore overwritten Texture2D pixels via QuickSDF undo.");

	if (FMessageDialog::Open(EAppMsgType::YesNo, FText::FromString(ConfirmText)) != EAppReturnType::Yes)
	{
		return;
	}

	int32 OverwrittenCount = 0;
	for (const FOverwriteSourceTarget& OverwriteTarget : Targets)
	{
		FText Error;
		if (Subsystem->OverwriteTextureWithRenderTarget(OverwriteTarget.Texture, OverwriteTarget.RenderTarget, &Error))
		{
			++OverwrittenCount;
		}
		else
		{
			if (!Error.IsEmpty())
			{
				FMessageDialog::Open(EAppMsgType::Ok, Error);
			}
			break;
		}
	}

	if (OverwrittenCount > 0)
	{
		FNotificationInfo Info(FText::Format(
			LOCTEXT("OverwriteSourceTexturesComplete", "Overwrote {0} source textures"),
			FText::AsNumber(OverwrittenCount)));
		Info.ExpireDuration = 4.0f;
		Info.bUseLargeFont = false;
		if (TSharedPtr<SNotificationItem> Notification = FSlateNotificationManager::Get().AddNotification(Info))
		{
			Notification->SetCompletionState(SNotificationItem::CS_Success);
		}
	}
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
			Properties->SetSymmetryEnabled(false);
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
		Asset->GetActiveResolution() = Properties->Resolution;
	}

	Asset->GetActiveUVChannel() = Properties->UVChannel;
	Asset->GetActiveAngleDataList().SetNum(Items.Num());
	Properties->NumAngles = Items.Num();
	Properties->TargetAngles.SetNum(Items.Num());
	Properties->TargetTextures.SetNum(Items.Num());

	for (int32 Index = 0; Index < Items.Num(); ++Index)
	{
		Asset->GetActiveAngleDataList()[Index].Angle = Items[Index].Angle;
		Asset->GetActiveAngleDataList()[Index].MaskGuid = FGuid::NewGuid();
		Asset->GetActiveAngleDataList()[Index].TextureMask = Items[Index].Texture;
		Asset->GetActiveAngleDataList()[Index].bAllowSourceTextureOverwrite = false;
		Asset->GetActiveAngleDataList()[Index].PaintRenderTarget = nullptr;
		Properties->TargetAngles[Index] = Items[Index].Angle;
		Properties->TargetTextures[Index] = Items[Index].Texture;
	}

	Properties->EditAngleIndex = FMath::Clamp(Properties->EditAngleIndex, 0, Properties->NumAngles - 1);
	Asset->InitializeRenderTargets(GetToolManager()->GetContextQueriesAPI()->GetCurrentEditingWorld());
	for (int32 Index = 0; Index < Items.Num(); ++Index)
	{
		Subsystem->DrawTextureToRenderTarget(Items[Index].Texture, Asset->GetActiveAngleDataList()[Index].PaintRenderTarget);
	}

	RefreshPreviewMaterial();
	bUseImportedMasksForQuickCreate = true;
	MarkMasksChanged();
	GetToolManager()->EndUndoTransaction();
	WarnIfMonotonicGuardViolations(LOCTEXT("MonotonicGuardAfterImportWithAnglesContext", "after mask import"));
	return true;
}

bool UQuickSDFPaintTool::ImportEditedMasksFromTexturesWithAngles(const TArray<UTexture2D*>& InTextures, const TArray<float>& InAngles)
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
	};

	TArray<FImportTextureItem> Items;
	Items.Reserve(InTextures.Num());
	bool bAnyAngleAboveSymmetryRange = false;
	for (int32 Index = 0; Index < InTextures.Num(); ++Index)
	{
		UTexture2D* Texture = InTextures[Index];
		if (!Texture)
		{
			continue;
		}

		const float Angle = InAngles.IsValidIndex(Index) ? InAngles[Index] : 0.0f;
		bAnyAngleAboveSymmetryRange |= Angle > 90.01f;

		FImportTextureItem& Item = Items.AddDefaulted_GetRef();
		Item.Texture = Texture;
		Item.Name = Texture->GetName();
		Item.Angle = Angle;
	}

	if (Items.Num() == 0)
	{
		return false;
	}

	GetToolManager()->BeginUndoTransaction(LOCTEXT("ImportEditedMasksWithAngles", "Import Edited Quick SDF Masks"));
	Asset->Modify();
	Properties->Modify();

	if (bAnyAngleAboveSymmetryRange)
	{
			Properties->SetSymmetryEnabled(false);
	}

	const float MaxAngle = Properties->bSymmetryMode ? 90.0f : 180.0f;
	for (FImportTextureItem& Item : Items)
	{
		Item.Angle = FMath::Clamp(Item.Angle, 0.0f, MaxAngle);
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
		Asset->GetActiveResolution() = Properties->Resolution;
	}

	Asset->GetActiveUVChannel() = Properties->UVChannel;
	Asset->GetActiveAngleDataList().SetNum(Items.Num());
	Properties->NumAngles = Items.Num();
	Properties->TargetAngles.SetNum(Items.Num());
	Properties->TargetTextures.SetNum(Items.Num());

	for (int32 Index = 0; Index < Items.Num(); ++Index)
	{
		Asset->GetActiveAngleDataList()[Index].Angle = Items[Index].Angle;
		Asset->GetActiveAngleDataList()[Index].MaskGuid = FGuid::NewGuid();
		Asset->GetActiveAngleDataList()[Index].TextureMask = Items[Index].Texture;
		Asset->GetActiveAngleDataList()[Index].bAllowSourceTextureOverwrite = false;
		Asset->GetActiveAngleDataList()[Index].PaintRenderTarget = nullptr;
		Properties->TargetAngles[Index] = Items[Index].Angle;
		Properties->TargetTextures[Index] = Items[Index].Texture;
	}

	Properties->EditAngleIndex = FMath::Clamp(Properties->EditAngleIndex, 0, Properties->NumAngles - 1);
	Asset->InitializeRenderTargets(GetToolManager()->GetContextQueriesAPI()->GetCurrentEditingWorld());
	for (int32 Index = 0; Index < Items.Num(); ++Index)
	{
		Subsystem->DrawTextureToRenderTarget(Items[Index].Texture, Asset->GetActiveAngleDataList()[Index].PaintRenderTarget);
	}

	RefreshPreviewMaterial();
	bUseImportedMasksForQuickCreate = true;
	MarkMasksChanged();
	GetToolManager()->EndUndoTransaction();
	WarnIfMonotonicGuardViolations(LOCTEXT("MonotonicGuardAfterImportContext", "after mask import"));
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
	ActiveAsset->SyncLegacyFromActiveTextureSet();
	const TArray<FQuickSDFAngleData> SourceAngleData = ActiveAsset->GetActiveAngleDataList();

	SavedAsset->GetActiveResolution() = ActiveAsset->GetActiveResolution();
	SavedAsset->GetActiveUVChannel() = ActiveAsset->GetActiveUVChannel();
	SavedAsset->GetActiveFinalSDFTexture() = ActiveAsset->GetActiveFinalSDFTexture();
	SavedAsset->GetActiveAngleDataList().SetNum(SourceAngleData.Num());

	const FString AssetFolder = FPackageName::GetLongPackagePath(SavedAsset->GetOutermost()->GetName());
	const FString MaskFolder = AssetFolder / FString::Printf(TEXT("%s_Masks"), *SavedAsset->GetName());

	for (int32 AngleIndex = 0; AngleIndex < SourceAngleData.Num(); ++AngleIndex)
	{
		const FQuickSDFAngleData& SourceData = SourceAngleData[AngleIndex];
		FQuickSDFAngleData& SavedData = SavedAsset->GetActiveAngleDataList()[AngleIndex];

		SavedData.Angle = SourceData.Angle;
		SavedData.MaskGuid = SourceData.MaskGuid.IsValid() ? SourceData.MaskGuid : FGuid::NewGuid();
		SavedData.bAllowSourceTextureOverwrite = SourceData.bAllowSourceTextureOverwrite;
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
				if (ActiveAsset->GetActiveAngleDataList().IsValidIndex(AngleIndex))
				{
					ActiveAsset->GetActiveAngleDataList()[AngleIndex].TextureMask = ExportedTexture;
				}
			}
			else if (!Error.IsEmpty())
			{
				FMessageDialog::Open(EAppMsgType::Ok, Error);
			}
		}

		SavedData.TextureMask = MaskTexture;
	}

	ActiveAsset->SyncLegacyFromActiveTextureSet();
	SavedAsset->TextureSets = ActiveAsset->TextureSets;
	SavedAsset->ActiveTextureSetIndex = ActiveAsset->ActiveTextureSetIndex;
	if (Properties->bSaveMaskTexturesWithAsset)
	{
		for (int32 TextureSetIndex = 0; TextureSetIndex < SavedAsset->TextureSets.Num(); ++TextureSetIndex)
		{
			FQuickSDFTextureSetData& SavedSet = SavedAsset->TextureSets[TextureSetIndex];
			FQuickSDFTextureSetData* ActiveSet = ActiveAsset->TextureSets.IsValidIndex(TextureSetIndex)
				? &ActiveAsset->TextureSets[TextureSetIndex]
				: nullptr;
			const FString SlotName = SavedSet.SlotName.IsNone()
				? FString::Printf(TEXT("Slot_%d"), SavedSet.MaterialSlotIndex)
				: SavedSet.SlotName.ToString();
			const FString CleanSlotName = ObjectTools::SanitizeObjectName(SlotName);
			const FString TextureSetMaskFolder = MaskFolder / CleanSlotName;
			for (int32 AngleIndex = 0; AngleIndex < SavedSet.AngleDataList.Num(); ++AngleIndex)
			{
				FQuickSDFAngleData& SavedData = SavedSet.AngleDataList[AngleIndex];
				UTextureRenderTarget2D* SourceRenderTarget = SavedData.PaintRenderTarget;
				if (!SourceRenderTarget)
				{
					continue;
				}

				const FString MaskName = FString::Printf(TEXT("T_%s_%s_Mask_%02d"), *SavedAsset->GetName(), *CleanSlotName, AngleIndex);
				FText Error;
				const bool bOverwriteMaskTexture = bWasPersistentAsset || Properties->bOverwriteExistingMasks;
				if (UTexture2D* ExportedTexture = Subsystem->CreateMaskTexture(SourceRenderTarget, TextureSetMaskFolder, MaskName, bOverwriteMaskTexture, &Error))
				{
					SavedData.TextureMask = ExportedTexture;
					if (ActiveSet && ActiveSet->AngleDataList.IsValidIndex(AngleIndex))
					{
						ActiveSet->AngleDataList[AngleIndex].TextureMask = ExportedTexture;
					}
				}
				else if (!Error.IsEmpty())
				{
					FMessageDialog::Open(EAppMsgType::Ok, Error);
				}
			}
		}
	}

	SavedAsset->MarkPackageDirty();
	SavedAsset->GetOutermost()->MarkPackageDirty();

	Subsystem->SetActiveSDFAsset(SavedAsset);
	Properties->TargetAsset = SavedAsset;
	SavedAsset->InitializeRenderTargets(GetToolManager()->GetContextQueriesAPI()->GetCurrentEditingWorld());
	for (int32 AngleIndex = 0; AngleIndex < SourceAngleData.Num(); ++AngleIndex)
	{
		if (!SavedAsset->GetActiveAngleDataList().IsValidIndex(AngleIndex) ||
			!SourceAngleData[AngleIndex].PaintRenderTarget ||
			!SavedAsset->GetActiveAngleDataList()[AngleIndex].PaintRenderTarget)
		{
			continue;
		}

		TArray<FColor> SourcePixels;
		if (CaptureRenderTargetPixels(SourceAngleData[AngleIndex].PaintRenderTarget, SourcePixels))
		{
			RestoreRenderTargetPixels(SavedAsset->GetActiveAngleDataList()[AngleIndex].PaintRenderTarget, SourcePixels);
		}
	}
	SyncPropertiesFromActiveAsset();
	RefreshPreviewMaterial();
	++MaskRevision;

	if (GEditor)
	{
		TArray<UObject*> AssetsToSync;
		AssetsToSync.Add(SavedAsset);
		GEditor->SyncBrowserToObjects(AssetsToSync);
	}
}

void UQuickSDFPaintTool::EnsureInitialMasksReady()
{
	if (!Properties)
	{
		return;
	}

	UQuickSDFToolSubsystem* Subsystem = GEditor->GetEditorSubsystem<UQuickSDFToolSubsystem>();
	UQuickSDFAsset* Asset = Subsystem ? Subsystem->GetActiveSDFAsset() : nullptr;
	if (!Subsystem || !Asset)
	{
		return;
	}

	FQuickSDFTextureSetData* ActiveSet = Asset->GetActiveTextureSet();
	if (ActiveSet && ActiveSet->bInitialBakeComplete)
	{
		Asset->InitializeRenderTargets(GetToolManager()->GetContextQueriesAPI()->GetCurrentEditingWorld());
		return;
	}

	EnsureMaskGuids(Asset);
	Asset->InitializeRenderTargets(GetToolManager()->GetContextQueriesAPI()->GetCurrentEditingWorld());
	if (HasImportedSourceMasks(Asset) || HasNonWhitePaintMasks(*this, Asset))
	{
		if (ActiveSet)
		{
			ActiveSet->bInitialBakeComplete = true;
			ActiveSet->bDirty = false;
			Asset->SyncLegacyFromActiveTextureSet();
		}
		return;
	}

	Asset->Modify();
	Properties->Modify();

	if (Asset->GetActiveResolution().X <= 0 || Asset->GetActiveResolution().Y <= 0)
	{
		const int32 PresetSize = GetQuickSDFPresetSize(EQuickSDFQualityPreset::Standard1024);
		Asset->GetActiveResolution() = FIntPoint(PresetSize, PresetSize);
		Properties->Resolution = Asset->GetActiveResolution();
	}

	if (Asset->GetActiveAngleDataList().Num() == 0)
	{
		InitializeDefaultAngleData(Asset->GetActiveAngleDataList(), true);
	}

	Properties->NumAngles = Asset->GetActiveAngleDataList().Num();
	Properties->TargetAngles.SetNum(Properties->NumAngles);
	Properties->TargetTextures.SetNum(Properties->NumAngles);

	for (int32 Index = 0; Index < Asset->GetActiveAngleDataList().Num(); ++Index)
	{
		Properties->TargetAngles[Index] = Asset->GetActiveAngleDataList()[Index].Angle;
		Properties->TargetTextures[Index] = Asset->GetActiveAngleDataList()[Index].TextureMask;
	}

	Asset->InitializeRenderTargets(GetToolManager()->GetContextQueriesAPI()->GetCurrentEditingWorld());
	if (ActiveSet)
	{
		ActiveSet->bInitialBakeComplete = false;
		ActiveSet->bDirty = false;
		Asset->SyncLegacyFromActiveTextureSet();
	}
}

void UQuickSDFPaintTool::RebakeCurrentMask()
{
	if (!Properties)
	{
		return;
	}

	FillOriginalShading(Properties->EditAngleIndex);
	if (UQuickSDFToolSubsystem* Subsystem = GEditor->GetEditorSubsystem<UQuickSDFToolSubsystem>())
	{
		if (UQuickSDFAsset* Asset = Subsystem->GetActiveSDFAsset())
		{
			if (FQuickSDFTextureSetData* ActiveSet = Asset->GetActiveTextureSet())
			{
				ActiveSet->bInitialBakeComplete = true;
				ActiveSet->bDirty = false;
				Asset->SyncLegacyFromActiveTextureSet();
			}
		}
	}
	WarnIfMonotonicGuardViolations(LOCTEXT("MonotonicGuardAfterRebakeCurrentContext", "after rebaking the current mask"));
}

void UQuickSDFPaintTool::RebakeAllMasks()
{
	FillOriginalShadingAll();
	if (UQuickSDFToolSubsystem* Subsystem = GEditor->GetEditorSubsystem<UQuickSDFToolSubsystem>())
	{
		if (UQuickSDFAsset* Asset = Subsystem->GetActiveSDFAsset())
		{
			if (FQuickSDFTextureSetData* ActiveSet = Asset->GetActiveTextureSet())
			{
				ActiveSet->bInitialBakeComplete = true;
				ActiveSet->bDirty = false;
				Asset->SyncLegacyFromActiveTextureSet();
			}
		}
	}
	WarnIfMonotonicGuardViolations(LOCTEXT("MonotonicGuardAfterRebakeAllContext", "after rebaking all masks"));
}

void UQuickSDFPaintTool::CompleteToEightMasks()
{
	if (!Properties)
	{
		return;
	}

	UQuickSDFToolSubsystem* Subsystem = GEditor->GetEditorSubsystem<UQuickSDFToolSubsystem>();
	UQuickSDFAsset* Asset = Subsystem ? Subsystem->GetActiveSDFAsset() : nullptr;
	const int32 TargetAngleCount = GetQuickSDFDefaultAngleCount(Properties->bSymmetryMode);
	if (!Subsystem || !Asset || Asset->GetActiveAngleDataList().Num() >= TargetAngleCount)
	{
		return;
	}

	TArray<float> AddedAngles;
	const float MaxAngle = Properties->bSymmetryMode ? 90.0f : 180.0f;
	TArray<float> StandardAngles;
	for (int32 Index = 0; Index < TargetAngleCount; ++Index)
	{
		StandardAngles.Add(TargetAngleCount > 1
			? (static_cast<float>(Index) / static_cast<float>(TargetAngleCount - 1)) * MaxAngle
			: 0.0f);
	}

	EnsureMaskGuids(Asset);
	Asset->InitializeRenderTargets(GetToolManager()->GetContextQueriesAPI()->GetCurrentEditingWorld());
	TArray<FGuid> BeforeGuids;
	TArray<float> BeforeAngles;
	TArray<UTexture2D*> BeforeTextures;
	TArray<bool> BeforeAllowSourceTextureOverwrites;
	TArray<TArray<FColor>> BeforePixelsByMask;
	CaptureMaskState(*this, Asset, BeforeGuids, BeforeAngles, BeforeTextures, BeforeAllowSourceTextureOverwrites, BeforePixelsByMask);

	GetToolManager()->BeginUndoTransaction(LOCTEXT("CompleteToDefaultMasks", "Complete Quick SDF Masks"));
	Asset->Modify();
	Properties->Modify();

	for (float CandidateAngle : StandardAngles)
	{
		if (Asset->GetActiveAngleDataList().Num() >= TargetAngleCount)
		{
			break;
		}

		bool bAlreadyCovered = false;
		for (const FQuickSDFAngleData& ExistingData : Asset->GetActiveAngleDataList())
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
			Asset->GetActiveAngleDataList().Add(NewData);
			AddedAngles.Add(CandidateAngle);
		}
	}

	while (Asset->GetActiveAngleDataList().Num() < TargetAngleCount)
	{
		FQuickSDFAngleData NewData;
		NewData.Angle = StandardAngles.IsValidIndex(Asset->GetActiveAngleDataList().Num())
			? StandardAngles[Asset->GetActiveAngleDataList().Num()]
			: MaxAngle;
		NewData.MaskGuid = FGuid::NewGuid();
		Asset->GetActiveAngleDataList().Add(NewData);
		AddedAngles.Add(NewData.Angle);
	}

	Asset->GetActiveAngleDataList().Sort([](const FQuickSDFAngleData& A, const FQuickSDFAngleData& B)
	{
		return A.Angle < B.Angle;
	});
	Asset->InitializeRenderTargets(GetToolManager()->GetContextQueriesAPI()->GetCurrentEditingWorld());
	SyncPropertiesFromActiveAsset();

	for (float AddedAngle : AddedAngles)
	{
		int32 AddedIndex = INDEX_NONE;
		for (int32 Index = 0; Index < Asset->GetActiveAngleDataList().Num(); ++Index)
		{
			if (FMath::IsNearlyEqual(Asset->GetActiveAngleDataList()[Index].Angle, AddedAngle, 0.5f))
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
	Change->BeforeAllowSourceTextureOverwrites = MoveTemp(BeforeAllowSourceTextureOverwrites);
	Change->BeforePixelsByMask = MoveTemp(BeforePixelsByMask);
	CaptureMaskState(*this, Asset, Change->AfterGuids, Change->AfterAngles, Change->AfterTextures, Change->AfterAllowSourceTextureOverwrites, Change->AfterPixelsByMask);
	GetToolManager()->EmitObjectChange(this, MoveTemp(Change), LOCTEXT("CompleteDefaultMaskState", "Restore Quick SDF Complete Mask State"));

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
	if (!Asset || Asset->GetActiveAngleDataList().Num() == 0)
	{
		return;
	}

	GetToolManager()->BeginUndoTransaction(LOCTEXT("RedistributeAnglesEvenly", "Redistribute Quick SDF Angles Evenly"));
	Asset->Modify();
	Properties->Modify();
	EnsureMaskGuids(Asset);

	Asset->GetActiveAngleDataList().Sort([](const FQuickSDFAngleData& A, const FQuickSDFAngleData& B)
	{
		return A.Angle < B.Angle;
	});

	const float MaxAngle = Properties->bSymmetryMode ? 90.0f : 180.0f;
	const int32 NumAngles = Asset->GetActiveAngleDataList().Num();
	for (int32 Index = 0; Index < NumAngles; ++Index)
	{
		Asset->GetActiveAngleDataList()[Index].Angle = NumAngles > 1
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
		for (int32 Index = 0; Index < Asset->GetActiveAngleDataList().Num(); ++Index)
		{
			TargetIndices.Add(Index);
		}
	}
	else
	{
		TargetIndices.Add(FMath::Clamp(Properties->EditAngleIndex, 0, Asset->GetActiveAngleDataList().Num() - 1));
	}

	GetToolManager()->BeginUndoTransaction(FillColor.Equals(FLinearColor::Black)
		? LOCTEXT("FillMasksBlack", "Fill Quick SDF Masks Black")
		: LOCTEXT("FillMasksWhite", "Fill Quick SDF Masks White"));
	Asset->Modify();
	Properties->Modify();
	EnsureMaskGuids(Asset);

	for (int32 AngleIndex : TargetIndices)
	{
		if (!Asset->GetActiveAngleDataList().IsValidIndex(AngleIndex) || !Asset->GetActiveAngleDataList()[AngleIndex].PaintRenderTarget)
		{
			continue;
		}

		UTextureRenderTarget2D* RenderTarget = Asset->GetActiveAngleDataList()[AngleIndex].PaintRenderTarget;
		const TArray<FColor> Pixels = MakeSolidPixels(RenderTarget->SizeX, RenderTarget->SizeY, FillColor);
		ApplyPixelsWithUndo(AngleIndex, Pixels, FillColor.Equals(FLinearColor::Black)
			? LOCTEXT("FillMaskBlackChange", "Fill Quick SDF Mask Black")
			: LOCTEXT("FillMaskWhiteChange", "Fill Quick SDF Mask White"));
		Asset->GetActiveAngleDataList()[AngleIndex].TextureMask = nullptr;
		Asset->GetActiveAngleDataList()[AngleIndex].bAllowSourceTextureOverwrite = false;
		if (Properties->TargetTextures.IsValidIndex(AngleIndex))
		{
			Properties->TargetTextures[AngleIndex] = nullptr;
		}
	}

	GetToolManager()->EndUndoTransaction();
	MarkMasksChanged();
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
	Properties->Resolution = Asset->GetActiveResolution();
	Properties->UVChannel = Asset->GetActiveUVChannel();
	Properties->ActiveTextureSetIndex = Asset->ActiveTextureSetIndex;
	if (const FQuickSDFTextureSetData* ActiveSet = Asset->GetActiveTextureSet())
	{
		Properties->TargetMaterialSlot = ActiveSet->MaterialSlotIndex;
	}
	Properties->NumAngles = Asset->GetActiveAngleDataList().Num();
	Properties->TargetAngles.SetNum(Properties->NumAngles);
	Properties->TargetTextures.SetNum(Properties->NumAngles);

	for (int32 Index = 0; Index < Properties->NumAngles; ++Index)
	{
		Properties->TargetAngles[Index] = Asset->GetActiveAngleDataList()[Index].Angle;
		Properties->TargetTextures[Index] = Asset->GetActiveAngleDataList()[Index].TextureMask;
	}

	Properties->EditAngleIndex = FMath::Clamp(Properties->EditAngleIndex, 0, FMath::Max(Properties->NumAngles - 1, 0));
}

void UQuickSDFPaintTool::MarkMasksChanged()
{
	SyncActiveTextureSetFromProperties();
	++MaskRevision;
}

void UQuickSDFPaintTool::OnPropertyModified(UObject* PropertySet, FProperty* Property)
{
	Super::OnPropertyModified(PropertySet, Property);

	if (PropertySet == Properties)
	{
		if (Property)
		{
			if (Property->GetFName() == GET_MEMBER_NAME_CHECKED(UQuickSDFToolProperties, PaintTargetMode))
			{
				Properties->bPaintAllAngles = Properties->PaintTargetMode == EQuickSDFPaintTargetMode::All;
			}
			else if (Property->GetFName() == GET_MEMBER_NAME_CHECKED(UQuickSDFToolProperties, bPaintAllAngles))
			{
				Properties->PaintTargetMode = Properties->bPaintAllAngles
					? EQuickSDFPaintTargetMode::All
					: EQuickSDFPaintTargetMode::CurrentOnly;
			}
			else if (Property->GetFName() == GET_MEMBER_NAME_CHECKED(UQuickSDFToolProperties, SymmetryMode))
			{
				Properties->SyncLegacySymmetryFlag();
			}
			else if (Property->GetFName() == GET_MEMBER_NAME_CHECKED(UQuickSDFToolProperties, bSymmetryMode))
			{
				Properties->SetSymmetryEnabled(Properties->bSymmetryMode);
			}
			else if (Property->GetFName() == GET_MEMBER_NAME_CHECKED(UQuickSDFToolProperties, bEnableBrushAntialiasing) ||
				Property->GetFName() == GET_MEMBER_NAME_CHECKED(UQuickSDFToolProperties, BrushAntialiasingWidth))
			{
				BrushMaskTexture = nullptr;
				BuildBrushMaskTexture();
			}
			else if (Property->GetFName() == GET_MEMBER_NAME_CHECKED(UQuickSDFToolProperties, ActiveTextureSetIndex))
			{
				SelectTextureSet(Properties->ActiveTextureSetIndex);
			}
			else if (Property->GetFName() == GET_MEMBER_NAME_CHECKED(UQuickSDFToolProperties, BrushProjectionMode))
			{
				ResetStrokeState();
			}
		}

		UQuickSDFToolSubsystem* Subsystem = GEditor->GetEditorSubsystem<UQuickSDFToolSubsystem>();
		UQuickSDFAsset* ActiveAsset = Subsystem ? Subsystem->GetActiveSDFAsset() : nullptr;

		if (ActiveAsset)
		{
			if (Property && Property->GetFName() == GET_MEMBER_NAME_CHECKED(UQuickSDFToolProperties, QualityPreset))
			{
				const int32 PresetSize = GetQuickSDFPresetSize(Properties->QualityPreset);
				Properties->Resolution = FIntPoint(PresetSize, PresetSize);
			}

			// 隧ｳ邏ｰ繝代ロ繝ｫ縺九ｉ蛻･縺ｮ繧｢繧ｻ繝・ヨ縺ｫ蛻・ｊ譖ｿ縺医◆蝣ｴ蜷医・蜃ｦ逅・
			if (Property && Property->GetFName() == GET_MEMBER_NAME_CHECKED(UQuickSDFToolProperties, TargetAsset))
			{
				if (Properties->TargetAsset != nullptr)
				{
					Subsystem->SetActiveSDFAsset(Properties->TargetAsset);
					ActiveAsset = Properties->TargetAsset;
					RefreshTextureSetsForCurrentComponent();
					EnsureMaskGuids(ActiveAsset);
					ActiveAsset->InitializeRenderTargets(GetToolManager()->GetContextQueriesAPI()->GetCurrentEditingWorld());
					
					// 譁ｰ縺励＞繧｢繧ｻ繝・ヨ縺ｮ蛟､繧旦I縺ｫ繝ｭ繝ｼ繝・
					Properties->Resolution = ActiveAsset->GetActiveResolution();
					Properties->UVChannel = ActiveAsset->GetActiveUVChannel();
					Properties->NumAngles = ActiveAsset->GetActiveAngleDataList().Num();
					Properties->TargetAngles.SetNum(Properties->NumAngles);
					Properties->TargetTextures.SetNum(Properties->NumAngles);
					for (int32 i = 0; i < Properties->NumAngles; ++i)
					{
						Properties->TargetAngles[i] = ActiveAsset->GetActiveAngleDataList()[i].Angle;
						Properties->TargetTextures[i] = ActiveAsset->GetActiveAngleDataList()[i].TextureMask;
					}
					RefreshPreviewMaterial();
					++MaskRevision;
				}
			}

			// 繧｢繝ｳ繧ｰ繝ｫ縺ｮ縲梧焚縲阪′螟峨ｏ縺｣縺溷ｴ蜷医√い繧ｻ繝・ヨ縺ｮ驟榊・繧ｵ繧､繧ｺ繧貞酔譛・(Linear reset has been disabled to support custom timeline editing)
			if (Property && Property->GetFName() == GET_MEMBER_NAME_CHECKED(UQuickSDFToolProperties, NumAngles))
			{
				/*
				Properties->TargetAngles.SetNum(Properties->NumAngles);
				ActiveAsset->GetActiveAngleDataList().SetNum(Properties->NumAngles);
				for (int32 i = 0; i < Properties->NumAngles; ++i)
				{
					Properties->TargetAngles[i] = ((float)i / (float)FMath::Max(1, Properties->NumAngles - 1)) * 180.0f;
					ActiveAsset->GetActiveAngleDataList()[i].Angle = Properties->TargetAngles[i];
				}
				ActiveAsset->InitializeRenderTargets(GetToolManager()->GetContextQueriesAPI()->GetCurrentEditingWorld());
				*/
			}

			// 謇句虚縺ｧ繧｢繝ｳ繧ｰ繝ｫ縺ｮ縲瑚ｧ貞ｺｦ縲阪′螟峨ｏ縺｣縺溷ｴ蜷・
			if (Property && Property->GetFName() == GET_MEMBER_NAME_CHECKED(UQuickSDFToolProperties, TargetAngles))
			{
				for (int32 i = 0; i < FMath::Min(Properties->TargetAngles.Num(), ActiveAsset->GetActiveAngleDataList().Num()); ++i)
				{
					ActiveAsset->GetActiveAngleDataList()[i].Angle = Properties->TargetAngles[i];
				}
			}
			// 謇句虚縺ｧ縲後ユ繧ｯ繧ｹ繝√Ε繧ｹ繝ｭ繝・ヨ縲阪↓逕ｻ蜒上′繧｢繧ｵ繧､繝ｳ・医∪縺溘・蜑企勁・峨＆繧後◆蝣ｴ蜷・
			if (Property && Property->GetFName() == GET_MEMBER_NAME_CHECKED(UQuickSDFToolProperties, TargetTextures))
			{
				EnsureMaskGuids(ActiveAsset);
				for (int32 i = 0; i < FMath::Min(Properties->TargetTextures.Num(), ActiveAsset->GetActiveAngleDataList().Num()); ++i)
				{
					// UI縺ｮ繝・け繧ｹ繝√Ε縺ｨ繧｢繧ｻ繝・ヨ縺ｮ繝・け繧ｹ繝√Ε縺ｫ蟾ｮ蛻・′縺ゅｌ縺ｰ譖ｴ譁ｰ
					if (ActiveAsset->GetActiveAngleDataList()[i].TextureMask != Properties->TargetTextures[i])
					{
						FQuickSDFAngleData& AngleData = ActiveAsset->GetActiveAngleDataList()[i];
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
						const bool bBeforeAllowSourceTextureOverwrite = AngleData.bAllowSourceTextureOverwrite;
						UTexture2D* AfterTexture = Properties->TargetTextures[i];
						ActiveAsset->Modify();
						AngleData.TextureMask = AfterTexture;
						AngleData.bAllowSourceTextureOverwrite = false;

						// 逕ｻ蜒上′繧ｻ繝・ヨ縺輔ｌ縺溘↑繧峨く繝｣繝ｳ繝舌せ縺ｫ霆｢蜀吶∝､悶＆繧後◆縺ｪ繧臥區邏吶↓謌ｻ縺・
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
						Change->bBeforeAllowSourceTextureOverwrite = bBeforeAllowSourceTextureOverwrite;
						Change->bAfterAllowSourceTextureOverwrite = false;
						Change->BeforePixels = MoveTemp(BeforePixels);
						Change->AfterPixels = MoveTemp(AfterPixels);
						GetToolManager()->EmitObjectChange(this, MoveTemp(Change), LOCTEXT("AssignQuickSDFMaskTexture", "Assign Quick SDF Mask Texture"));
					}
				}
				RefreshPreviewMaterial();
				MarkMasksChanged();
			}//TODO:蠕後°繧峨ユ繧ｯ繧ｹ繝√Ε繧定ｿｽ蜉縺吶ｋ蜃ｦ逅・ｒ螳溯｣・☆繧・
			// 隗｣蜒丞ｺｦ縺ｮ蜷梧悄 窶・FIntPoint 縺ｮ繧ｵ繝悶・繝ｭ繝代ユ繧｣ (X, Y) 螟画峩繧よ､懷・縺吶ｋ縺溘ａ縲∝錐蜑阪〒縺ｯ縺ｪ縺丞､縺ｮ蟾ｮ蛻・〒蛻､螳・
			if (ActiveAsset->GetActiveResolution() != Properties->Resolution)
			{
				Properties->Resolution.X = FMath::Max(Properties->Resolution.X, 1);
				Properties->Resolution.Y = FMath::Max(Properties->Resolution.Y, 1);
				const FIntPoint NewResolution = Properties->Resolution;

				TArray<TArray<FColor>> ResizedPixelsByAngle;
				ResizedPixelsByAngle.SetNum(ActiveAsset->GetActiveAngleDataList().Num());
				for (int32 AngleIndex = 0; AngleIndex < ActiveAsset->GetActiveAngleDataList().Num(); ++AngleIndex)
				{
					UTextureRenderTarget2D* RenderTarget = ActiveAsset->GetActiveAngleDataList()[AngleIndex].PaintRenderTarget;
					TArray<FColor> SourcePixels;
					if (RenderTarget &&
						CaptureRenderTargetPixels(RenderTarget, SourcePixels) &&
						SourcePixels.Num() == RenderTarget->SizeX * RenderTarget->SizeY)
					{
						ResizeMaskPixelsBilinear(
							SourcePixels,
							RenderTarget->SizeX,
							RenderTarget->SizeY,
							NewResolution.X,
							NewResolution.Y,
							ResizedPixelsByAngle[AngleIndex]);
					}
				}

				ActiveAsset->GetActiveResolution() = NewResolution;
				for (FQuickSDFAngleData& Data : ActiveAsset->GetActiveAngleDataList())
				{
					Data.PaintRenderTarget = nullptr;
				}
				ActiveAsset->InitializeRenderTargets(GetToolManager()->GetContextQueriesAPI()->GetCurrentEditingWorld());
				for (int32 AngleIndex = 0; AngleIndex < ActiveAsset->GetActiveAngleDataList().Num(); ++AngleIndex)
				{
					if (ResizedPixelsByAngle.IsValidIndex(AngleIndex) && ResizedPixelsByAngle[AngleIndex].Num() == NewResolution.X * NewResolution.Y)
					{
						RestoreRenderTargetPixels(ActiveAsset->GetActiveAngleDataList()[AngleIndex].PaintRenderTarget, ResizedPixelsByAngle[AngleIndex]);
					}
				}
				RefreshPreviewMaterial();
				MarkMasksChanged();
			}

			if (Property && Property->GetFName() == GET_MEMBER_NAME_CHECKED(UQuickSDFToolProperties, UVChannel))
			{
				ActiveAsset->GetActiveUVChannel() = Properties->UVChannel;
				InvalidateUVOverlayCache();
				RefreshPreviewMaterial();
				MarkMasksChanged();
			}
		}

		if (Property && (Property->GetFName() == GET_MEMBER_NAME_CHECKED(UQuickSDFToolProperties, EditAngleIndex) ||
				 Property->GetFName() == GET_MEMBER_NAME_CHECKED(UQuickSDFToolProperties, TargetAngles) ||
				 Property->GetFName() == GET_MEMBER_NAME_CHECKED(UQuickSDFToolProperties, MaterialPreviewMode) ||
				 Property->GetFName() == GET_MEMBER_NAME_CHECKED(UQuickSDFToolProperties, SymmetryMode) ||
				 Property->GetFName() == GET_MEMBER_NAME_CHECKED(UQuickSDFToolProperties, bSymmetryMode)))
		{
			RefreshPreviewMaterial();
		}

		if (Property && (Property->GetFName() == GET_MEMBER_NAME_CHECKED(UQuickSDFToolProperties, TargetMaterialSlot) ||
				 Property->GetFName() == GET_MEMBER_NAME_CHECKED(UQuickSDFToolProperties, bIsolateTargetMaterialSlot)))
		{
			InvalidateUVOverlayCache();
			ApplyTargetMaterialSlotIsolation();
		}
	}
}

void UQuickSDFPaintTool::AddKeyframe()
{
	AddKeyframeInternal(0.0f, false);
}

void UQuickSDFPaintTool::AddKeyframeAtAngle(float Angle)
{
	AddKeyframeInternal(Angle, true);
}

void UQuickSDFPaintTool::DuplicateKeyframeAtAngle(float Angle)
{
	if (!Properties) return;
	UQuickSDFToolSubsystem* Subsystem = GEditor->GetEditorSubsystem<UQuickSDFToolSubsystem>();
	if (!Subsystem || !Subsystem->GetActiveSDFAsset()) return;

	UQuickSDFAsset* Asset = Subsystem->GetActiveSDFAsset();
	Asset->InitializeRenderTargets(GetToolManager()->GetContextQueriesAPI()->GetCurrentEditingWorld());
	if (Asset->GetActiveAngleDataList().Num() == 0)
	{
		return;
	}

	const int32 SourceIndex = FMath::Clamp(Properties->EditAngleIndex, 0, Asset->GetActiveAngleDataList().Num() - 1);
	if (!Asset->GetActiveAngleDataList().IsValidIndex(SourceIndex) || !Asset->GetActiveAngleDataList()[SourceIndex].PaintRenderTarget)
	{
		return;
	}

	TArray<FColor> SourcePixels;
	if (!CaptureRenderTargetPixels(Asset->GetActiveAngleDataList()[SourceIndex].PaintRenderTarget, SourcePixels))
	{
		return;
	}

	AddKeyframeInternal(Angle, true, &SourcePixels);
}

void UQuickSDFPaintTool::AddKeyframeInternal(float RequestedAngle, bool bUseRequestedAngle, const TArray<FColor>* SourcePixels)
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
	TArray<bool> BeforeAllowSourceTextureOverwrites;
	TArray<TArray<FColor>> BeforePixelsByMask;
	CaptureMaskState(*this, Asset, BeforeGuids, BeforeAngles, BeforeTextures, BeforeAllowSourceTextureOverwrites, BeforePixelsByMask);

	const bool bDuplicateKeyframe = SourcePixels && SourcePixels->Num() > 0;
	GetToolManager()->BeginUndoTransaction(bDuplicateKeyframe
		? LOCTEXT("DuplicateKeyframe", "Duplicate Timeline Keyframe")
		: LOCTEXT("AddKeyframe", "Add Timeline Keyframe"));
	Asset->Modify();
	Properties->Modify();

	const auto GetFallbackInsert = [this, Asset](int32& OutInsertIndex, float& OutAngle)
	{
		if (Asset->GetActiveAngleDataList().Num() == 0)
		{
			OutAngle = 0.0f;
			OutInsertIndex = 0;
			return;
		}

		const int32 CurrentIndex = FMath::Clamp(Properties->EditAngleIndex, 0, Asset->GetActiveAngleDataList().Num() - 1);
		OutInsertIndex = CurrentIndex + 1;
		if (OutInsertIndex >= Asset->GetActiveAngleDataList().Num())
		{
			const float MaxAngle = Properties->bSymmetryMode ? 90.0f : 180.0f;
			OutAngle = FMath::Min(Asset->GetActiveAngleDataList().Last().Angle + 10.0f, MaxAngle);
		}
		else
		{
			const float PrevAngle = Asset->GetActiveAngleDataList()[OutInsertIndex - 1].Angle;
			const float NextAngle = Asset->GetActiveAngleDataList()[OutInsertIndex].Angle;
			OutAngle = (PrevAngle + NextAngle) * 0.5f;
		}
	};

	int32 InsertIndex = 0;
	float NewAngle = 0.0f;

	if (bUseRequestedAngle)
	{
		const float MaxAngle = Properties->bSymmetryMode ? 90.0f : 180.0f;
		NewAngle = FMath::Clamp(RequestedAngle, 0.0f, MaxAngle);

		bool bOverlapsExistingKey = false;
		for (const FQuickSDFAngleData& AngleData : Asset->GetActiveAngleDataList())
		{
			if (FMath::IsNearlyEqual(AngleData.Angle, NewAngle, 0.05f))
			{
				bOverlapsExistingKey = true;
				break;
			}
		}

		if (bOverlapsExistingKey)
		{
			GetFallbackInsert(InsertIndex, NewAngle);
		}
		else
		{
			InsertIndex = Asset->GetActiveAngleDataList().Num();
			for (int32 Index = 0; Index < Asset->GetActiveAngleDataList().Num(); ++Index)
			{
				if (NewAngle < Asset->GetActiveAngleDataList()[Index].Angle)
				{
					InsertIndex = Index;
					break;
				}
			}
		}
	}
	else
	{
		GetFallbackInsert(InsertIndex, NewAngle);
	}

	FQuickSDFAngleData NewData;
	NewData.Angle = NewAngle;
	NewData.MaskGuid = FGuid::NewGuid();
	
	Asset->GetActiveAngleDataList().Insert(NewData, InsertIndex);
	Properties->TargetAngles.Insert(NewAngle, InsertIndex);
	Properties->TargetTextures.Insert(nullptr, InsertIndex);
	Properties->NumAngles = Asset->GetActiveAngleDataList().Num();

	Asset->InitializeRenderTargets(GetToolManager()->GetContextQueriesAPI()->GetCurrentEditingWorld());
	
	Properties->EditAngleIndex = InsertIndex;
	FProperty* Prop = Properties->GetClass()->FindPropertyByName(GET_MEMBER_NAME_CHECKED(UQuickSDFToolProperties, EditAngleIndex));
	OnPropertyModified(Properties, Prop);

	if (bDuplicateKeyframe)
	{
		const bool bWasSuppressingMaskPixelUndo = bSuppressMaskPixelUndo;
		bSuppressMaskPixelUndo = true;
		ApplyPixelsWithUndo(InsertIndex, *SourcePixels, LOCTEXT("DuplicateKeyframePixels", "Duplicate Quick SDF Mask"));
		bSuppressMaskPixelUndo = bWasSuppressingMaskPixelUndo;
	}
	else if (CurrentComponent.IsValid())
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
	Change->BeforeAllowSourceTextureOverwrites = MoveTemp(BeforeAllowSourceTextureOverwrites);
	Change->BeforePixelsByMask = MoveTemp(BeforePixelsByMask);
	CaptureMaskState(*this, Asset, Change->AfterGuids, Change->AfterAngles, Change->AfterTextures, Change->AfterAllowSourceTextureOverwrites, Change->AfterPixelsByMask);
	GetToolManager()->EmitObjectChange(this, MoveTemp(Change), bDuplicateKeyframe
		? LOCTEXT("DuplicateKeyframeMaskState", "Restore Quick SDF Duplicated Keyframe Mask State")
		: LOCTEXT("AddKeyframeMaskState", "Restore Quick SDF Added Keyframe Mask State"));

	GetToolManager()->EndUndoTransaction();
}

void UQuickSDFPaintTool::RemoveKeyframe(int32 Index)
{
	if (!Properties) return;
	UQuickSDFToolSubsystem* Subsystem = GEditor->GetEditorSubsystem<UQuickSDFToolSubsystem>();
	if (!Subsystem || !Subsystem->GetActiveSDFAsset()) return;

	UQuickSDFAsset* Asset = Subsystem->GetActiveSDFAsset();
	
	if (Asset->GetActiveAngleDataList().IsValidIndex(Index) && Asset->GetActiveAngleDataList().Num() > 1)
	{
		EnsureMaskGuids(Asset);
		Asset->InitializeRenderTargets(GetToolManager()->GetContextQueriesAPI()->GetCurrentEditingWorld());
		TArray<FGuid> BeforeGuids;
		TArray<float> BeforeAngles;
		TArray<UTexture2D*> BeforeTextures;
		TArray<bool> BeforeAllowSourceTextureOverwrites;
		TArray<TArray<FColor>> BeforePixelsByMask;
		CaptureMaskState(*this, Asset, BeforeGuids, BeforeAngles, BeforeTextures, BeforeAllowSourceTextureOverwrites, BeforePixelsByMask);

		GetToolManager()->BeginUndoTransaction(LOCTEXT("RemoveKeyframe", "Remove Timeline Keyframe"));
		Asset->Modify();
		Properties->Modify();

		Asset->GetActiveAngleDataList().RemoveAt(Index);
		Properties->TargetAngles.RemoveAt(Index);
		Properties->TargetTextures.RemoveAt(Index);
		Properties->NumAngles = Asset->GetActiveAngleDataList().Num();
		
		Properties->EditAngleIndex = FMath::Clamp(Properties->EditAngleIndex, 0, Properties->NumAngles - 1);
		
		Asset->InitializeRenderTargets(GetToolManager()->GetContextQueriesAPI()->GetCurrentEditingWorld());

		FProperty* Prop = Properties->GetClass()->FindPropertyByName(GET_MEMBER_NAME_CHECKED(UQuickSDFToolProperties, EditAngleIndex));
		OnPropertyModified(Properties, Prop);

		TUniquePtr<FQuickSDFMaskStateChange> Change = MakeUnique<FQuickSDFMaskStateChange>();
		Change->BeforeGuids = MoveTemp(BeforeGuids);
		Change->BeforeAngles = MoveTemp(BeforeAngles);
		Change->BeforeTextures = MoveTemp(BeforeTextures);
		Change->BeforeAllowSourceTextureOverwrites = MoveTemp(BeforeAllowSourceTextureOverwrites);
		Change->BeforePixelsByMask = MoveTemp(BeforePixelsByMask);
		CaptureMaskState(*this, Asset, Change->AfterGuids, Change->AfterAngles, Change->AfterTextures, Change->AfterAllowSourceTextureOverwrites, Change->AfterPixelsByMask);
		GetToolManager()->EmitObjectChange(this, MoveTemp(Change), LOCTEXT("RemoveKeyframeMaskState", "Restore Quick SDF Removed Keyframe Mask State"));

		GetToolManager()->EndUndoTransaction();
		MarkMasksChanged();
	}
}
#undef LOCTEXT_NAMESPACE
