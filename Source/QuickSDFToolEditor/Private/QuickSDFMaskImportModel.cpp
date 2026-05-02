#include "QuickSDFMaskImportModel.h"

#include "Engine/Texture2D.h"

#define LOCTEXT_NAMESPACE "QuickSDFMaskImportModel"

namespace QuickSDFMaskImportModel
{
FString GetSourceName(const FQuickSDFMaskImportSource& Source)
{
	if (!Source.DisplayName.IsEmpty())
	{
		return Source.DisplayName;
	}

	return Source.Texture.IsValid() ? Source.Texture->GetName() : FString(TEXT("Missing mask"));
}

FString GetSourceKey(const FQuickSDFMaskImportSource& Source)
{
	return Source.Texture.IsValid() ? Source.Texture->GetPathName() : GetSourceName(Source);
}

bool DoSourcesReferToSameContent(const FQuickSDFMaskImportSource& A, const FQuickSDFMaskImportSource& B)
{
	if (A.ImportGuid.IsValid() && B.ImportGuid.IsValid())
	{
		return A.ImportGuid == B.ImportGuid;
	}

	return A.Texture == B.Texture;
}

bool IsEngineContentPath(const FString& AssetPath)
{
	return AssetPath.Equals(TEXT("/Engine"), ESearchCase::IgnoreCase) ||
		AssetPath.StartsWith(TEXT("/Engine/"), ESearchCase::IgnoreCase);
}

bool IsEngineTexture(const UTexture2D* Texture)
{
	return Texture && IsEngineContentPath(Texture->GetPathName());
}

FQuickSDFMaskImportSource MakeImportSourceFromTexture(UTexture2D* Texture)
{
	FQuickSDFMaskImportSource Source;
	if (Texture)
	{
		Source.Texture = Texture;
		Source.DisplayName = Texture->GetName();
		Source.Width = Texture->GetSizeX();
		Source.Height = Texture->GetSizeY();
	}
	return Source;
}

FText FormatSize(int32 Width, int32 Height)
{
	return (Width > 0 && Height > 0)
		? FText::Format(LOCTEXT("SizeFormat", "{0} x {1}"), FText::AsNumber(Width), FText::AsNumber(Height))
		: LOCTEXT("UnknownSize", "Unknown");
}

FText AppendWarningText(const FText& ExistingWarning, const FText& NewWarning)
{
	if (ExistingWarning.IsEmpty())
	{
		return NewWarning;
	}
	if (NewWarning.IsEmpty())
	{
		return ExistingWarning;
	}
	return FText::Format(LOCTEXT("AppendWarningFormat", "{0} / {1}"), ExistingWarning, NewWarning);
}
}

#undef LOCTEXT_NAMESPACE
