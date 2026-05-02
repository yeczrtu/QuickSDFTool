#pragma once

#include "CoreMinimal.h"
#include "SQuickSDFMaskImportPanel.h"

class UTexture2D;

namespace QuickSDFMaskImportModel
{
FString GetSourceName(const FQuickSDFMaskImportSource& Source);
FString GetSourceKey(const FQuickSDFMaskImportSource& Source);
bool DoSourcesReferToSameContent(const FQuickSDFMaskImportSource& A, const FQuickSDFMaskImportSource& B);
bool IsEngineContentPath(const FString& AssetPath);
bool IsEngineTexture(const UTexture2D* Texture);
FQuickSDFMaskImportSource MakeImportSourceFromTexture(UTexture2D* Texture);
FText FormatSize(int32 Width, int32 Height);
FText AppendWarningText(const FText& ExistingWarning, const FText& NewWarning);
}
