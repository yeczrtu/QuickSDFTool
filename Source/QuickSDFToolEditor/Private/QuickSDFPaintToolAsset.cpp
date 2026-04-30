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
#include "IDesktopPlatform.h"
#include "Misc/DefaultValueHelper.h"
#include "Containers/Ticker.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetToolsModule.h"
#include "IAssetTools.h"
#include "Misc/PackageName.h"
#include "ObjectTools.h"

#if WITH_EDITOR
#include "Editor.h"
#include "IMaterialBakingModule.h"
#include "MaterialBakingStructures.h"
#endif

#define LOCTEXT_NAMESPACE "QuickSDFPaintTool"

using namespace QuickSDFPaintToolPrivate;

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
	if (!Properties)
	{
		return;
	}

	TArray<UTexture2D*> Textures = CollectSelectedTextureAssets();
	if (Textures.Num() == 0)
	{
		IDesktopPlatform* DesktopPlatform = FDesktopPlatformModule::Get();
		if (!DesktopPlatform)
		{
			return;
		}

		TArray<FString> SourceFilenames;
		const void* ParentWindowHandle = FSlateApplication::Get().FindBestParentWindowHandleForDialogs(nullptr);
		const bool bSelectedFiles = DesktopPlatform->OpenFileDialog(
			ParentWindowHandle,
			TEXT("Import Edited Masks"),
			FString(),
			FString(),
			TEXT("Image files|*.png;*.jpg;*.jpeg;*.bmp;*.tga;*.exr|All files|*.*"),
			EFileDialogFlags::Multiple,
			SourceFilenames);

		if (!bSelectedFiles || SourceFilenames.Num() == 0)
		{
			return;
		}

		UQuickSDFToolSubsystem* Subsystem = GEditor->GetEditorSubsystem<UQuickSDFToolSubsystem>();
		if (!Subsystem)
		{
			return;
		}

		FText ImportError;
		if (!Subsystem->ImportMaskFilesAsTextures(SourceFilenames, Properties->ImportedMaskFolder, Textures, &ImportError))
		{
			if (!ImportError.IsEmpty())
			{
				FMessageDialog::Open(EAppMsgType::Ok, ImportError);
			}
			return;
		}
	}

	ImportEditedMasksFromTextures(Textures);
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
	if (!Subsystem || !Asset || Asset->AngleDataList.Num() >= QuickSDFDefaultAngleCount)
	{
		return;
	}

	TArray<float> AddedAngles;
	const float MaxAngle = Properties->bSymmetryMode ? 90.0f : 180.0f;
	TArray<float> StandardAngles;
	for (int32 Index = 0; Index < QuickSDFDefaultAngleCount; ++Index)
	{
		StandardAngles.Add(QuickSDFDefaultAngleCount > 1
			? (static_cast<float>(Index) / static_cast<float>(QuickSDFDefaultAngleCount - 1)) * MaxAngle
			: 0.0f);
	}

	EnsureMaskGuids(Asset);
	Asset->InitializeRenderTargets(GetToolManager()->GetContextQueriesAPI()->GetCurrentEditingWorld());
	TArray<FGuid> BeforeGuids;
	TArray<float> BeforeAngles;
	TArray<UTexture2D*> BeforeTextures;
	TArray<TArray<FColor>> BeforePixelsByMask;
	CaptureMaskState(*this, Asset, BeforeGuids, BeforeAngles, BeforeTextures, BeforePixelsByMask);

	GetToolManager()->BeginUndoTransaction(LOCTEXT("CompleteToEightMasks", "Complete Quick SDF Masks to 8"));
	Asset->Modify();
	Properties->Modify();

	for (float CandidateAngle : StandardAngles)
	{
		if (Asset->AngleDataList.Num() >= QuickSDFDefaultAngleCount)
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

	while (Asset->AngleDataList.Num() < QuickSDFDefaultAngleCount)
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
	Change->BeforePixelsByMask = MoveTemp(BeforePixelsByMask);
	CaptureMaskState(*this, Asset, Change->AfterGuids, Change->AfterAngles, Change->AfterTextures, Change->AfterPixelsByMask);
	GetToolManager()->EmitObjectChange(this, MoveTemp(Change), LOCTEXT("CompleteToEightMaskState", "Restore Quick SDF Complete to 8 Mask State"));

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
						UTexture2D* AfterTexture = Properties->TargetTextures[i];
						ActiveAsset->Modify();
						AngleData.TextureMask = AfterTexture;

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
				ActiveAsset->Resolution = Properties->Resolution;
				// Force re-creation of render targets at the new resolution
				for (FQuickSDFAngleData& Data : ActiveAsset->AngleDataList)
				{
					Data.PaintRenderTarget = nullptr;
				}
				ActiveAsset->InitializeRenderTargets(GetToolManager()->GetContextQueriesAPI()->GetCurrentEditingWorld());
				RefreshPreviewMaterial();
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

void UQuickSDFPaintTool::AddKeyframeInternal(float RequestedAngle, bool bUseRequestedAngle)
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
	TArray<TArray<FColor>> BeforePixelsByMask;
	CaptureMaskState(*this, Asset, BeforeGuids, BeforeAngles, BeforeTextures, BeforePixelsByMask);

	GetToolManager()->BeginUndoTransaction(LOCTEXT("AddKeyframe", "Add Timeline Keyframe"));
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

	if (CurrentComponent.IsValid())
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
	Change->BeforePixelsByMask = MoveTemp(BeforePixelsByMask);
	CaptureMaskState(*this, Asset, Change->AfterGuids, Change->AfterAngles, Change->AfterTextures, Change->AfterPixelsByMask);
	GetToolManager()->EmitObjectChange(this, MoveTemp(Change), LOCTEXT("AddKeyframeMaskState", "Restore Quick SDF Added Keyframe Mask State"));

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
		TArray<TArray<FColor>> BeforePixelsByMask;
		CaptureMaskState(*this, Asset, BeforeGuids, BeforeAngles, BeforeTextures, BeforePixelsByMask);

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
		Change->BeforePixelsByMask = MoveTemp(BeforePixelsByMask);
		CaptureMaskState(*this, Asset, Change->AfterGuids, Change->AfterAngles, Change->AfterTextures, Change->AfterPixelsByMask);
		GetToolManager()->EmitObjectChange(this, MoveTemp(Change), LOCTEXT("RemoveKeyframeMaskState", "Restore Quick SDF Removed Keyframe Mask State"));

		GetToolManager()->EndUndoTransaction();
		MarkMasksChanged();
	}
}
#undef LOCTEXT_NAMESPACE
