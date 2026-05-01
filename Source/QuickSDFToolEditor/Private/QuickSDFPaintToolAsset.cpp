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
#include "Misc/DefaultValueHelper.h"
#include "Containers/Ticker.h"
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
	if (!Asset || !Asset->AngleDataList.IsValidIndex(AngleIndex))
	{
		return false;
	}

	Asset->InitializeRenderTargets(GetToolManager()->GetContextQueriesAPI()->GetCurrentEditingWorld());
	EnsureMaskGuids(Asset);
	FQuickSDFAngleData& AngleData = Asset->AngleDataList[AngleIndex];
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

	Properties->TargetTextures.SetNum(Asset->AngleDataList.Num());
	Properties->TargetAngles.SetNum(Asset->AngleDataList.Num());
	Properties->NumAngles = Asset->AngleDataList.Num();
	Properties->EditAngleIndex = FMath::Clamp(AngleIndex, 0, Asset->AngleDataList.Num() - 1);
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
	for (int32 Index = 0; Index < Asset->AngleDataList.Num(); ++Index)
	{
		const FQuickSDFAngleData& AngleData = Asset->AngleDataList[Index];
		if (!AngleData.bAllowSourceTextureOverwrite)
		{
			continue;
		}
		if (!AngleData.TextureMask || !AngleData.PaintRenderTarget)
		{
			continue;
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
		Asset->AngleDataList[Index].bAllowSourceTextureOverwrite = false;
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
		Properties->bSymmetryMode = false;
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
		Asset->AngleDataList[Index].bAllowSourceTextureOverwrite = false;
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
	const int32 TargetAngleCount = GetQuickSDFDefaultAngleCount(Properties->bSymmetryMode);
	if (!Subsystem || !Asset || Asset->AngleDataList.Num() >= TargetAngleCount)
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
		if (Asset->AngleDataList.Num() >= TargetAngleCount)
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

	while (Asset->AngleDataList.Num() < TargetAngleCount)
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
		Asset->AngleDataList[AngleIndex].bAllowSourceTextureOverwrite = false;
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
			else if (Property->GetFName() == GET_MEMBER_NAME_CHECKED(UQuickSDFToolProperties, bEnableBrushAntialiasing) ||
				Property->GetFName() == GET_MEMBER_NAME_CHECKED(UQuickSDFToolProperties, BrushAntialiasingWidth))
			{
				BrushMaskTexture = nullptr;
				BuildBrushMaskTexture();
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
						const bool bBeforeAllowSourceTextureOverwrite = AngleData.bAllowSourceTextureOverwrite;
						UTexture2D* AfterTexture = Properties->TargetTextures[i];
						ActiveAsset->Modify();
						AngleData.TextureMask = AfterTexture;
						AngleData.bAllowSourceTextureOverwrite = false;

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
						Change->bBeforeAllowSourceTextureOverwrite = bBeforeAllowSourceTextureOverwrite;
						Change->bAfterAllowSourceTextureOverwrite = false;
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
				Properties->Resolution.X = FMath::Max(Properties->Resolution.X, 1);
				Properties->Resolution.Y = FMath::Max(Properties->Resolution.Y, 1);
				const FIntPoint NewResolution = Properties->Resolution;

				TArray<TArray<FColor>> ResizedPixelsByAngle;
				ResizedPixelsByAngle.SetNum(ActiveAsset->AngleDataList.Num());
				for (int32 AngleIndex = 0; AngleIndex < ActiveAsset->AngleDataList.Num(); ++AngleIndex)
				{
					UTextureRenderTarget2D* RenderTarget = ActiveAsset->AngleDataList[AngleIndex].PaintRenderTarget;
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

				ActiveAsset->Resolution = NewResolution;
				for (FQuickSDFAngleData& Data : ActiveAsset->AngleDataList)
				{
					Data.PaintRenderTarget = nullptr;
				}
				ActiveAsset->InitializeRenderTargets(GetToolManager()->GetContextQueriesAPI()->GetCurrentEditingWorld());
				for (int32 AngleIndex = 0; AngleIndex < ActiveAsset->AngleDataList.Num(); ++AngleIndex)
				{
					if (ResizedPixelsByAngle.IsValidIndex(AngleIndex) && ResizedPixelsByAngle[AngleIndex].Num() == NewResolution.X * NewResolution.Y)
					{
						RestoreRenderTargetPixels(ActiveAsset->AngleDataList[AngleIndex].PaintRenderTarget, ResizedPixelsByAngle[AngleIndex]);
					}
				}
				RefreshPreviewMaterial();
				MarkMasksChanged();
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
	if (Asset->AngleDataList.Num() == 0)
	{
		return;
	}

	const int32 SourceIndex = FMath::Clamp(Properties->EditAngleIndex, 0, Asset->AngleDataList.Num() - 1);
	if (!Asset->AngleDataList.IsValidIndex(SourceIndex) || !Asset->AngleDataList[SourceIndex].PaintRenderTarget)
	{
		return;
	}

	TArray<FColor> SourcePixels;
	if (!CaptureRenderTargetPixels(Asset->AngleDataList[SourceIndex].PaintRenderTarget, SourcePixels))
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
		if (Asset->AngleDataList.Num() == 0)
		{
			OutAngle = 0.0f;
			OutInsertIndex = 0;
			return;
		}

		const int32 CurrentIndex = FMath::Clamp(Properties->EditAngleIndex, 0, Asset->AngleDataList.Num() - 1);
		OutInsertIndex = CurrentIndex + 1;
		if (OutInsertIndex >= Asset->AngleDataList.Num())
		{
			const float MaxAngle = Properties->bSymmetryMode ? 90.0f : 180.0f;
			OutAngle = FMath::Min(Asset->AngleDataList.Last().Angle + 10.0f, MaxAngle);
		}
		else
		{
			const float PrevAngle = Asset->AngleDataList[OutInsertIndex - 1].Angle;
			const float NextAngle = Asset->AngleDataList[OutInsertIndex].Angle;
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
		for (const FQuickSDFAngleData& AngleData : Asset->AngleDataList)
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
			InsertIndex = Asset->AngleDataList.Num();
			for (int32 Index = 0; Index < Asset->AngleDataList.Num(); ++Index)
			{
				if (NewAngle < Asset->AngleDataList[Index].Angle)
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
	
	Asset->AngleDataList.Insert(NewData, InsertIndex);
	Properties->TargetAngles.Insert(NewAngle, InsertIndex);
	Properties->TargetTextures.Insert(nullptr, InsertIndex);
	Properties->NumAngles = Asset->AngleDataList.Num();

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
	
	if (Asset->AngleDataList.IsValidIndex(Index) && Asset->AngleDataList.Num() > 1)
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
		Change->BeforeAllowSourceTextureOverwrites = MoveTemp(BeforeAllowSourceTextureOverwrites);
		Change->BeforePixelsByMask = MoveTemp(BeforePixelsByMask);
		CaptureMaskState(*this, Asset, Change->AfterGuids, Change->AfterAngles, Change->AfterTextures, Change->AfterAllowSourceTextureOverwrites, Change->AfterPixelsByMask);
		GetToolManager()->EmitObjectChange(this, MoveTemp(Change), LOCTEXT("RemoveKeyframeMaskState", "Restore Quick SDF Removed Keyframe Mask State"));

		GetToolManager()->EndUndoTransaction();
		MarkMasksChanged();
	}
}
#undef LOCTEXT_NAMESPACE
