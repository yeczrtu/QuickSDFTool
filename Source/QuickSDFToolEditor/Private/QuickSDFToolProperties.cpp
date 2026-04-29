#include "QuickSDFToolProperties.h"

#include "QuickSDFAsset.h"
#include "QuickSDFPaintTool.h"
#include "QuickSDFToolSubsystem.h"
#include "Editor.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/Texture2D.h"
#include "Misc/MessageDialog.h"

void UQuickSDFToolProperties::ExportToTexture()
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
	Asset->Modify();

	FString EffectiveMaskExportFolder = MaskExportFolder;
	if (bCreateMaskFolderPerExport && !MaskExportFolderPrefix.IsEmpty())
	{
		const FDateTime Now = FDateTime::Now();
		const FString ExportFolderName = FString::Printf(TEXT("%s_%s_%03d"), *MaskExportFolderPrefix, *Now.ToString(TEXT("%Y%m%d_%H%M%S")), Now.GetMillisecond());
		EffectiveMaskExportFolder /= ExportFolderName;
	}

	for (int32 AngleIndex = 0; AngleIndex < Asset->AngleDataList.Num(); ++AngleIndex)
	{
		UTextureRenderTarget2D* RenderTarget = Asset->AngleDataList[AngleIndex].PaintRenderTarget;
		if (!RenderTarget)
		{
			continue;
		}

		const FString AssetName = FString::Printf(TEXT("%s%d"), *MaskTextureNamePrefix, AngleIndex);
		FText Error;
		UTexture2D* NewTexture = Subsystem->CreateMaskTexture(RenderTarget, EffectiveMaskExportFolder, AssetName, bOverwriteExistingMasks, &Error);
		if (NewTexture)
		{
			Asset->AngleDataList[AngleIndex].TextureMask = NewTexture;
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
		Asset->MarkPackageDirty();
	}
}

void UQuickSDFToolProperties::FillOriginalShadingToCurrentAngle()
{
	if (UQuickSDFPaintTool* Tool = Cast<UQuickSDFPaintTool>(GetOuter()))
	{
		Tool->FillOriginalShading(EditAngleIndex);
	}
}

void UQuickSDFToolProperties::FillOriginalShadingToAllAngles()
{
	if (UQuickSDFPaintTool* Tool = Cast<UQuickSDFPaintTool>(GetOuter()))
	{
		Tool->FillOriginalShadingAll();
	}
}

void UQuickSDFToolProperties::GenerateSDFThresholdMap()
{
	if (UQuickSDFPaintTool* Tool = Cast<UQuickSDFPaintTool>(GetOuter()))
	{
		Tool->GenerateSDF();
	}
}
