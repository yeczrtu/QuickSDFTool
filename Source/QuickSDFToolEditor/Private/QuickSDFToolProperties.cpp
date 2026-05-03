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

float UQuickSDFToolProperties::GetPaintMaxAngle() const
{
	return UsesFrontHalfAngles() ? 90.0f : 180.0f;
}

void UQuickSDFToolProperties::SetSymmetryMode(EQuickSDFSymmetryMode NewMode)
{
	SymmetryMode = NewMode;
	SyncLegacySymmetryFlag();
}

void UQuickSDFToolProperties::SetSymmetryEnabled(bool bEnabled)
{
	SetSymmetryMode(bEnabled ? EQuickSDFSymmetryMode::WholeTextureFlip90 : EQuickSDFSymmetryMode::None180);
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

void UQuickSDFToolProperties::ValidateMonotonicGuard()
{
	if (UQuickSDFPaintTool* Tool = Cast<UQuickSDFPaintTool>(GetOuter()))
	{
		Tool->ValidateMonotonicGuard();
	}
}

#undef LOCTEXT_NAMESPACE
