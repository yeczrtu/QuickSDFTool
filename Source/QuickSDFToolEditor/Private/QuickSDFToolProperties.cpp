#include "QuickSDFToolProperties.h"

#include "ContentBrowserModule.h"
#include "IContentBrowserSingleton.h"
#include "QuickSDFAsset.h"
#include "QuickSDFPaintTool.h"
#include "QuickSDFToolSubsystem.h"
#include "DesktopPlatformModule.h"
#include "Editor.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/Texture2D.h"
#include "Framework/Application/SlateApplication.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformProcess.h"
#include "IDesktopPlatform.h"
#include "IImageWrapper.h"
#include "IImageWrapperModule.h"
#include "Misc/FileHelper.h"
#include "Misc/MessageDialog.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "ObjectTools.h"

#define LOCTEXT_NAMESPACE "QuickSDFToolProperties"

namespace
{
constexpr float QuickSDFAngleOffsetMinDegrees = -90.0f;
constexpr float QuickSDFAngleOffsetMaxDegrees = 90.0f;
constexpr float QuickSDFAngleOffsetClampMarginDegrees = 0.01f;

FString MakeMaskExportBatchName(const FString& Prefix)
{
	const FDateTime Now = FDateTime::Now();
	return FString::Printf(TEXT("%s_%s_%03d"), *Prefix, *Now.ToString(TEXT("%Y%m%d_%H%M%S")), Now.GetMillisecond());
}

FString MakeMaskExportTextureName(const FString& Prefix, int32 AngleIndex)
{
	return ObjectTools::SanitizeObjectName(FString::Printf(TEXT("%s%d"), *Prefix, AngleIndex));
}

FString MakeAlphabeticIndexName(int32 Index)
{
	FString Name;
	int32 Value = FMath::Max(Index, 0);
	do
	{
		const TCHAR Letter = static_cast<TCHAR>(TEXT('a') + (Value % 26));
		Name.InsertAt(0, Letter);
		Value = (Value / 26) - 1;
	}
	while (Value >= 0);

	return Name;
}

FString MakeMaskExportFileBaseName(EQuickSDFMaskFileNameMode FileNameMode, int32 ExportIndex, float Angle)
{
	switch (FileNameMode)
	{
	case EQuickSDFMaskFileNameMode::Numbered:
		return FString::FromInt(FMath::Max(ExportIndex, 0) + 1);
	case EQuickSDFMaskFileNameMode::Angle:
		return FString::FromInt(FMath::RoundToInt(Angle));
	case EQuickSDFMaskFileNameMode::Alphabetic:
	default:
		return MakeAlphabeticIndexName(ExportIndex);
	}
}

FString MakeUniqueMaskExportFilePath(const FString& OutputFolder, const FString& FileBaseName)
{
	const FString CleanBaseName = FPaths::MakeValidFileName(FileBaseName.IsEmpty() ? FString(TEXT("mask")) : FileBaseName);
	FString CandidatePath = OutputFolder / FString::Printf(TEXT("%s.png"), *CleanBaseName);

	for (int32 Suffix = 2; IFileManager::Get().FileExists(*CandidatePath); ++Suffix)
	{
		CandidatePath = OutputFolder / FString::Printf(TEXT("%s_%d.png"), *CleanBaseName, Suffix);
	}

	return CandidatePath;
}

float GetQuickSDFMaterialAngleForEffectiveOffset(float AuthoredAngle, float EffectiveOffsetDegrees, bool bUsesFrontHalfAngles)
{
	constexpr float PivotAngle = 90.0f;
	const float SafeOffset = FMath::Clamp(EffectiveOffsetDegrees, 0.0f, 90.0f);
	if (FMath::IsNearlyZero(SafeOffset))
	{
		return AuthoredAngle;
	}

	if (bUsesFrontHalfAngles || AuthoredAngle <= PivotAngle)
	{
		const float Alpha = AuthoredAngle / PivotAngle;
		return -SafeOffset + Alpha * (PivotAngle + SafeOffset);
	}

	const float Alpha = (AuthoredAngle - PivotAngle) / PivotAngle;
	return PivotAngle + Alpha * (PivotAngle + SafeOffset);
}

float GetQuickSDFAngleDeltaAtIndex(const TArray<float>& Deltas, int32 Index)
{
	return Deltas.IsValidIndex(Index) ? Deltas[Index] : 0.0f;
}

void OpenContentBrowserToExportFolder(const FString& FolderPath, const TArray<UObject*>& Assets)
{
	if (FolderPath.IsEmpty())
	{
		return;
	}

	FContentBrowserModule& ContentBrowserModule = FModuleManager::LoadModuleChecked<FContentBrowserModule>("ContentBrowser");
	IContentBrowserSingleton& ContentBrowser = ContentBrowserModule.Get();
	const TArray<FString> FoldersToSync = { FolderPath };

	ContentBrowser.FocusPrimaryContentBrowser(false);
	ContentBrowser.SetSelectedPaths(FoldersToSync, true, false);
	ContentBrowser.SyncBrowserToFolders(FoldersToSync, true, true);
	if (!Assets.IsEmpty())
	{
		ContentBrowser.SyncBrowserToAssets(Assets, true, true);
	}
}
}

bool UQuickSDFToolProperties::UsesFrontHalfAngles() const
{
	return SymmetryMode != EQuickSDFSymmetryMode::None180;
}

bool UQuickSDFToolProperties::UsesWholeTextureSymmetry() const
{
	return SymmetryMode == EQuickSDFSymmetryMode::WholeTextureFlip90;
}

bool UQuickSDFToolProperties::UsesIslandChannelSymmetry() const
{
	return SymmetryMode == EQuickSDFSymmetryMode::UVIslandChannelFlip90;
}

bool UQuickSDFToolProperties::UsesOverlappedUVSplitSymmetry() const
{
	return SymmetryMode == EQuickSDFSymmetryMode::OverlappedUVSplit90;
}

float UQuickSDFToolProperties::GetPaintMaxAngle() const
{
	return UsesFrontHalfAngles() ? 90.0f : 180.0f;
}

float UQuickSDFToolProperties::GetMaterialAngle(float AuthoredAngle) const
{
	return GetQuickSDFMaterialAngleForEffectiveOffset(AuthoredAngle, BakeAngleOffsetDegrees, UsesFrontHalfAngles());
}

float UQuickSDFToolProperties::GetMaterialAngle(float AuthoredAngle, float AngleOffsetDeltaDegrees) const
{
	const float BaseBakeAngle = GetMaterialAngle(AuthoredAngle);
	const float ImageBakeShift = FMath::Clamp(AngleOffsetDeltaDegrees, QuickSDFAngleOffsetMinDegrees, QuickSDFAngleOffsetMaxDegrees);
	return BaseBakeAngle + ImageBakeShift;
}

float UQuickSDFToolProperties::GetMaterialAngleForKey(int32 AngleIndex) const
{
	if (!TargetAngles.IsValidIndex(AngleIndex))
	{
		return 0.0f;
	}

	const float Delta = GetQuickSDFAngleDeltaAtIndex(TargetAngleOffsetDeltas, AngleIndex);
	if (FMath::IsNearlyZero(Delta))
	{
		return GetMaterialAngle(TargetAngles[AngleIndex]);
	}

	float MinPreviewAngle = 0.0f;
	float MaxPreviewAngle = GetPaintMaxAngle();
	GetAngleOffsetPreviewRange(AngleIndex, MinPreviewAngle, MaxPreviewAngle);
	return FMath::Clamp(GetMaterialAngle(TargetAngles[AngleIndex], Delta), MinPreviewAngle, MaxPreviewAngle);
}

float UQuickSDFToolProperties::GetClampedAngleOffsetDelta(int32 AngleIndex, float RequestedDeltaDegrees) const
{
	if (!TargetAngles.IsValidIndex(AngleIndex))
	{
		return 0.0f;
	}

	const float RequestedDelta = FMath::Clamp(RequestedDeltaDegrees, QuickSDFAngleOffsetMinDegrees, QuickSDFAngleOffsetMaxDegrees);
	if (FMath::IsNearlyZero(RequestedDelta))
	{
		return 0.0f;
	}

	float MinPreviewAngle = 0.0f;
	float MaxPreviewAngle = GetPaintMaxAngle();
	GetAngleOffsetPreviewRange(AngleIndex, MinPreviewAngle, MaxPreviewAngle);

	const float AuthoredAngle = TargetAngles[AngleIndex];
	const float RequestedPreviewAngle = GetMaterialAngle(AuthoredAngle, RequestedDelta);
	const float ClampedPreviewAngle = FMath::Clamp(RequestedPreviewAngle, MinPreviewAngle, MaxPreviewAngle);
	if (FMath::IsNearlyEqual(RequestedPreviewAngle, ClampedPreviewAngle, KINDA_SMALL_NUMBER))
	{
		return RequestedDelta;
	}

	const float BaseBakeAngle = GetMaterialAngle(AuthoredAngle);
	return FMath::Clamp(ClampedPreviewAngle - BaseBakeAngle, QuickSDFAngleOffsetMinDegrees, QuickSDFAngleOffsetMaxDegrees);
}

void UQuickSDFToolProperties::GetAngleOffsetPreviewRange(int32 AngleIndex, float& OutMinPreviewAngle, float& OutMaxPreviewAngle) const
{
	const float MaxAngle = GetPaintMaxAngle();
	OutMinPreviewAngle = 0.0f;
	OutMaxPreviewAngle = MaxAngle;

	if (!TargetAngles.IsValidIndex(AngleIndex))
	{
		return;
	}

	const bool bFrontHalfAngles = UsesFrontHalfAngles();
	TArray<int32> VisibleIndices;
	for (int32 Index = 0; Index < TargetAngles.Num(); ++Index)
	{
		if (!bFrontHalfAngles || TargetAngles[Index] <= MaxAngle)
		{
			VisibleIndices.Add(Index);
		}
	}

	VisibleIndices.Sort([this](int32 A, int32 B)
	{
		return TargetAngles[A] < TargetAngles[B];
	});

	const int32 VisualIndex = VisibleIndices.IndexOfByKey(AngleIndex);
	if (VisualIndex == INDEX_NONE)
	{
		return;
	}

	if (VisibleIndices.IsValidIndex(VisualIndex - 1))
	{
		const int32 PreviousIndex = VisibleIndices[VisualIndex - 1];
		const float PreviousEffectiveAngle = FMath::Clamp(
			GetMaterialAngle(TargetAngles[PreviousIndex], GetQuickSDFAngleDeltaAtIndex(TargetAngleOffsetDeltas, PreviousIndex)),
			0.0f,
			MaxAngle);
		OutMinPreviewAngle = PreviousEffectiveAngle + QuickSDFAngleOffsetClampMarginDegrees;
	}

	if (VisibleIndices.IsValidIndex(VisualIndex + 1))
	{
		const int32 NextIndex = VisibleIndices[VisualIndex + 1];
		const float NextEffectiveAngle = FMath::Clamp(
			GetMaterialAngle(TargetAngles[NextIndex], GetQuickSDFAngleDeltaAtIndex(TargetAngleOffsetDeltas, NextIndex)),
			0.0f,
			MaxAngle);
		OutMaxPreviewAngle = NextEffectiveAngle - QuickSDFAngleOffsetClampMarginDegrees;
	}

	OutMinPreviewAngle = FMath::Clamp(OutMinPreviewAngle, 0.0f, MaxAngle);
	OutMaxPreviewAngle = FMath::Clamp(OutMaxPreviewAngle, 0.0f, MaxAngle);
	if (OutMinPreviewAngle > OutMaxPreviewAngle)
	{
		const float Midpoint = (OutMinPreviewAngle + OutMaxPreviewAngle) * 0.5f;
		OutMinPreviewAngle = Midpoint;
		OutMaxPreviewAngle = Midpoint;
	}
}

void UQuickSDFToolProperties::SetSymmetryMode(EQuickSDFSymmetryMode NewMode)
{
	SymmetryMode = NewMode;
	SyncLegacySymmetryFlag();
}

void UQuickSDFToolProperties::SetSymmetryEnabled(bool bEnabled)
{
	SetSymmetryMode(bEnabled ? EQuickSDFSymmetryMode::Auto : EQuickSDFSymmetryMode::None180);
}

void UQuickSDFToolProperties::SyncLegacySymmetryFlag()
{
	bSymmetryMode = UsesFrontHalfAngles();
}

void UQuickSDFToolProperties::ExportToTexture()
{
	ExportMaskTexturesToAssets();
}

void UQuickSDFToolProperties::ExportMaskTexturesToAssets()
{
	UQuickSDFPaintTool* Tool = Cast<UQuickSDFPaintTool>(GetOuter());
	if (!Tool)
	{
		return;
	}

	UQuickSDFToolSubsystem* Subsystem = GEditor->GetEditorSubsystem<UQuickSDFToolSubsystem>();
	if (!Subsystem || !Subsystem->GetActiveSDFAsset())
	{
		return;
	}

	UQuickSDFAsset* Asset = Subsystem->GetActiveSDFAsset();
	int32 ExportedCount = 0;
	TArray<UObject*> ExportedTextures;
	Asset->Modify();

	FString EffectiveMaskExportFolder = MaskExportFolder;
	if (bCreateMaskFolderPerExport && !MaskExportFolderPrefix.IsEmpty())
	{
		EffectiveMaskExportFolder /= MakeMaskExportBatchName(MaskExportFolderPrefix);
	}

	for (int32 AngleIndex = 0; AngleIndex < Asset->GetActiveAngleDataList().Num(); ++AngleIndex)
	{
		UTextureRenderTarget2D* RenderTarget = Asset->GetActiveAngleDataList()[AngleIndex].PaintRenderTarget;
		if (!RenderTarget)
		{
			continue;
		}

		const FString AssetName = MakeMaskExportTextureName(MaskTextureNamePrefix, AngleIndex);
		FText Error;
		UTexture2D* NewTexture = Subsystem->CreateMaskTexture(RenderTarget, EffectiveMaskExportFolder, AssetName, bOverwriteExistingMasks, &Error);
		if (NewTexture)
		{
			Asset->GetActiveAngleDataList()[AngleIndex].TextureMask = NewTexture;
			ExportedTextures.Add(NewTexture);
			++ExportedCount;
		}
		else if (!Error.IsEmpty())
		{
			FMessageDialog::Open(EAppMsgType::Ok, Error);
			break;
		}
	}

	if (ExportedCount > 0)
	{
		Asset->SyncLegacyFromActiveTextureSet();
		Asset->MarkPackageDirty();
		OpenContentBrowserToExportFolder(EffectiveMaskExportFolder, ExportedTextures);
	}
}

void UQuickSDFToolProperties::ExportMaskTexturesToFiles()
{
	UQuickSDFPaintTool* Tool = Cast<UQuickSDFPaintTool>(GetOuter());
	if (!Tool)
	{
		return;
	}

	UQuickSDFToolSubsystem* Subsystem = GEditor ? GEditor->GetEditorSubsystem<UQuickSDFToolSubsystem>() : nullptr;
	if (!Subsystem || !Subsystem->GetActiveSDFAsset())
	{
		return;
	}

	IDesktopPlatform* DesktopPlatform = FDesktopPlatformModule::Get();
	if (!DesktopPlatform)
	{
		FMessageDialog::Open(EAppMsgType::Ok, LOCTEXT("MaskFileExportNoDesktopPlatform", "Cannot open a folder picker for mask file export."));
		return;
	}

	const void* ParentWindowHandle = nullptr;
	if (FSlateApplication::IsInitialized())
	{
		ParentWindowHandle = FSlateApplication::Get().FindBestParentWindowHandleForDialogs(nullptr);
	}

	FString SelectedFolder;
	if (!DesktopPlatform->OpenDirectoryDialog(
		ParentWindowHandle,
		LOCTEXT("MaskFileExportChooseFolder", "Choose Folder for Mask PNG Export").ToString(),
		FPaths::ProjectSavedDir(),
		SelectedFolder))
	{
		return;
	}

	FString OutputFolder = SelectedFolder;
	if (bCreateMaskFolderPerExport && !MaskExportFolderPrefix.IsEmpty())
	{
		OutputFolder /= FPaths::MakeValidFileName(MakeMaskExportBatchName(MaskExportFolderPrefix));
	}

	if (!IFileManager::Get().MakeDirectory(*OutputFolder, true))
	{
		FMessageDialog::Open(
			EAppMsgType::Ok,
			FText::Format(LOCTEXT("MaskFileExportCreateFolderFailed", "Failed to create mask export folder:\n{0}"), FText::FromString(OutputFolder)));
		return;
	}

	UQuickSDFAsset* Asset = Subsystem->GetActiveSDFAsset();
	IImageWrapperModule& ImageWrapperModule = FModuleManager::LoadModuleChecked<IImageWrapperModule>("ImageWrapper");
	int32 ExportedCount = 0;
	bool bEncounteredError = false;

	for (int32 AngleIndex = 0; AngleIndex < Asset->GetActiveAngleDataList().Num(); ++AngleIndex)
	{
		UTextureRenderTarget2D* RenderTarget = Asset->GetActiveAngleDataList()[AngleIndex].PaintRenderTarget;
		if (!RenderTarget)
		{
			continue;
		}

		TArray<FColor> Pixels;
		if (!Subsystem->CaptureRenderTargetPixels(RenderTarget, Pixels))
		{
			FMessageDialog::Open(EAppMsgType::Ok, LOCTEXT("MaskFileExportCaptureFailed", "Failed to read pixels from a mask render target."));
			bEncounteredError = true;
			break;
		}

		TSharedPtr<IImageWrapper> ImageWrapper = ImageWrapperModule.CreateImageWrapper(EImageFormat::PNG);
		if (!ImageWrapper.IsValid() ||
			!ImageWrapper->SetRaw(Pixels.GetData(), Pixels.Num() * sizeof(FColor), RenderTarget->SizeX, RenderTarget->SizeY, ERGBFormat::BGRA, 8))
		{
			FMessageDialog::Open(EAppMsgType::Ok, LOCTEXT("MaskFileExportEncodeFailed", "Failed to encode a mask render target as PNG."));
			bEncounteredError = true;
			break;
		}

		const TArray64<uint8> PngData = ImageWrapper->GetCompressed();
		const FString FileBaseName = MakeMaskExportFileBaseName(MaskFileNameMode, ExportedCount, Asset->GetActiveAngleDataList()[AngleIndex].Angle);
		const FString OutputPath = MakeUniqueMaskExportFilePath(OutputFolder, FileBaseName);
		if (PngData.Num() == 0 || !FFileHelper::SaveArrayToFile(PngData, *OutputPath))
		{
			FMessageDialog::Open(
				EAppMsgType::Ok,
				FText::Format(LOCTEXT("MaskFileExportSaveFailed", "Failed to save mask PNG:\n{0}"), FText::FromString(OutputPath)));
			bEncounteredError = true;
			break;
		}

		++ExportedCount;
	}

	if (ExportedCount == 0 && !bEncounteredError)
	{
		FMessageDialog::Open(EAppMsgType::Ok, LOCTEXT("MaskFileExportNoFiles", "No mask PNG files were exported."));
	}
	else if (ExportedCount > 0)
	{
		FPlatformProcess::ExploreFolder(*OutputFolder);
	}
}

void UQuickSDFToolProperties::CreateQuickThresholdMap()
{
	if (UQuickSDFPaintTool* Tool = Cast<UQuickSDFPaintTool>(GetOuter()))
	{
		Tool->CreateQuickThresholdMap();
	}
}

void UQuickSDFToolProperties::ImportEditedMasks()
{
	if (UQuickSDFPaintTool* Tool = Cast<UQuickSDFPaintTool>(GetOuter()))
	{
		Tool->ImportEditedMasks();
	}
}

void UQuickSDFToolProperties::OverwriteSourceTextures()
{
	if (UQuickSDFPaintTool* Tool = Cast<UQuickSDFPaintTool>(GetOuter()))
	{
		Tool->OverwriteSourceTextures();
	}
}

void UQuickSDFToolProperties::SaveQuickSDFAsset()
{
	if (UQuickSDFPaintTool* Tool = Cast<UQuickSDFPaintTool>(GetOuter()))
	{
		Tool->SaveQuickSDFAsset();
	}
}

void UQuickSDFToolProperties::FillCurrentMaskWhite()
{
	if (UQuickSDFPaintTool* Tool = Cast<UQuickSDFPaintTool>(GetOuter()))
	{
		Tool->FillMaskColor(false, FLinearColor::White);
	}
}

void UQuickSDFToolProperties::FillCurrentMaskBlack()
{
	if (UQuickSDFPaintTool* Tool = Cast<UQuickSDFPaintTool>(GetOuter()))
	{
		Tool->FillMaskColor(false, FLinearColor::Black);
	}
}

void UQuickSDFToolProperties::FillAllMasksWhite()
{
	if (UQuickSDFPaintTool* Tool = Cast<UQuickSDFPaintTool>(GetOuter()))
	{
		Tool->FillMaskColor(true, FLinearColor::White);
	}
}

void UQuickSDFToolProperties::FillAllMasksBlack()
{
	if (UQuickSDFPaintTool* Tool = Cast<UQuickSDFPaintTool>(GetOuter()))
	{
		Tool->FillMaskColor(true, FLinearColor::Black);
	}
}

void UQuickSDFToolProperties::CompleteToEightMasks()
{
	if (UQuickSDFPaintTool* Tool = Cast<UQuickSDFPaintTool>(GetOuter()))
	{
		Tool->CompleteToEightMasks();
	}
}

void UQuickSDFToolProperties::RedistributeAnglesEvenly()
{
	if (UQuickSDFPaintTool* Tool = Cast<UQuickSDFPaintTool>(GetOuter()))
	{
		Tool->RedistributeAnglesEvenly();
	}
}

void UQuickSDFToolProperties::FillOriginalShadingToCurrentAngle()
{
	if (UQuickSDFPaintTool* Tool = Cast<UQuickSDFPaintTool>(GetOuter()))
	{
		Tool->RebakeCurrentMask();
	}
}

void UQuickSDFToolProperties::FillOriginalShadingToAllAngles()
{
	if (UQuickSDFPaintTool* Tool = Cast<UQuickSDFPaintTool>(GetOuter()))
	{
		Tool->RebakeAllMasks();
	}
}

void UQuickSDFToolProperties::GenerateSDFThresholdMap()
{
	if (UQuickSDFPaintTool* Tool = Cast<UQuickSDFPaintTool>(GetOuter()))
	{
		Tool->GenerateSelectedTextureSetSDF();
	}
}

void UQuickSDFToolProperties::GenerateSDFThresholdMapToFile()
{
	if (UQuickSDFPaintTool* Tool = Cast<UQuickSDFPaintTool>(GetOuter()))
	{
		Tool->GenerateSDFToFile();
	}
}

void UQuickSDFToolProperties::ConvertIntermediateSDF()
{
	if (UQuickSDFPaintTool* Tool = Cast<UQuickSDFPaintTool>(GetOuter()))
	{
		Tool->ConvertIntermediateSDF();
	}
}

void UQuickSDFToolProperties::ConvertIntermediateSDF(EQuickSDFThresholdMapOutputMode OutputMode)
{
	if (UQuickSDFPaintTool* Tool = Cast<UQuickSDFPaintTool>(GetOuter()))
	{
		Tool->ConvertIntermediateSDF(OutputMode);
	}
}

void UQuickSDFToolProperties::ValidateMonotonicGuard()
{
	if (UQuickSDFPaintTool* Tool = Cast<UQuickSDFPaintTool>(GetOuter()))
	{
		Tool->ValidateMonotonicGuard();
	}
}

#undef LOCTEXT_NAMESPACE
