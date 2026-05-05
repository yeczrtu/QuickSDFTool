#include "QuickSDFSelectTool.h"

#include "CollisionQueryParams.h"
#include "Components/MeshComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Editor.h"
#include "EngineUtils.h"
#include "GameFramework/Actor.h"
#include "InteractiveToolManager.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpressionConstant.h"
#include "Materials/MaterialExpressionConstant3Vector.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "QuickSDFAsset.h"
#include "QuickSDFMaterialSlotHitTest.h"
#include "QuickSDFTextureSetSync.h"
#include "QuickSDFToolProperties.h"
#include "QuickSDFToolSubsystem.h"
#include "ToolContextInterfaces.h"
#include "UnrealClient.h"

namespace
{
constexpr double QuickSDFSelectRayLength = 100000.0;
const TCHAR* QuickSDFActiveSlotHighlightMaterialObjectPath = TEXT("/QuickSDFTool/Materials/M_ActiveMaterialSlotHighlight.M_ActiveMaterialSlotHighlight");

void ConnectExpressionToInput(UMaterialExpression* Expression, FExpressionInput& Input)
{
	if (!Expression)
	{
		return;
	}

	Input.Expression = Expression;
	const TArray<FExpressionOutput>& Outputs = Expression->GetOutputs();
	if (Outputs.Num() > 0)
	{
		Input.Mask = Outputs[0].Mask;
		Input.MaskR = Outputs[0].MaskR;
		Input.MaskG = Outputs[0].MaskG;
		Input.MaskB = Outputs[0].MaskB;
		Input.MaskA = Outputs[0].MaskA;
	}
}

UMaterialInterface* CreateTransientActiveSlotHighlightMaterial(UObject* Outer)
{
	UMaterial* HighlightMaterial = NewObject<UMaterial>(Outer ? Outer : GetTransientPackage(), NAME_None, RF_Transient);
	if (!HighlightMaterial)
	{
		return nullptr;
	}

	HighlightMaterial->BlendMode = BLEND_Translucent;
	HighlightMaterial->SetShadingModel(MSM_Unlit);
	HighlightMaterial->TwoSided = true;
	HighlightMaterial->bUsedWithSkeletalMesh = true;
	HighlightMaterial->bUsedWithInstancedStaticMeshes = true;
	HighlightMaterial->bAutomaticallySetUsageInEditor = true;

#if WITH_EDITORONLY_DATA
	UMaterialEditorOnlyData* EditorOnlyData = HighlightMaterial->GetEditorOnlyData();
	if (!EditorOnlyData)
	{
		return HighlightMaterial;
	}

	UMaterialExpressionConstant3Vector* ColorExpression = NewObject<UMaterialExpressionConstant3Vector>(HighlightMaterial);
	ColorExpression->Constant = FLinearColor(0.0f, 0.82f, 1.0f, 1.0f);
	HighlightMaterial->GetExpressionCollection().AddExpression(ColorExpression);
	ConnectExpressionToInput(ColorExpression, EditorOnlyData->BaseColor);
	ConnectExpressionToInput(ColorExpression, EditorOnlyData->EmissiveColor);

	UMaterialExpressionConstant* OpacityExpression = NewObject<UMaterialExpressionConstant>(HighlightMaterial);
	OpacityExpression->R = 0.28f;
	HighlightMaterial->GetExpressionCollection().AddExpression(OpacityExpression);
	ConnectExpressionToInput(OpacityExpression, EditorOnlyData->Opacity);
#endif

	HighlightMaterial->PostEditChange();
	return HighlightMaterial;
}

UMeshComponent* ResolveMeshComponentFromPrimitive(UPrimitiveComponent* HitComponent)
{
	UMeshComponent* TargetComponent = Cast<UMeshComponent>(HitComponent);
	if (!TargetComponent && HitComponent)
	{
		if (AActor* Owner = HitComponent->GetOwner())
		{
			TargetComponent = Owner->FindComponentByClass<UMeshComponent>();
		}
	}
	return TargetComponent;
}

UMeshComponent* ResolveMeshComponentFromHitProxy(const FInputDeviceRay& ClickPos, IToolsContextQueriesAPI* ContextAPI)
{
	if (!ClickPos.bHas2D || !ContextAPI)
	{
		return nullptr;
	}

	FViewport* Viewport = ContextAPI->GetHoveredViewport();
	if (!Viewport)
	{
		Viewport = ContextAPI->GetFocusedViewport();
	}
	if (!Viewport)
	{
		return nullptr;
	}

	const int32 HitX = FMath::RoundToInt(ClickPos.ScreenPosition.X);
	const int32 HitY = FMath::RoundToInt(ClickPos.ScreenPosition.Y);
	HActor* ActorHit = HitProxyCast<HActor>(Viewport->GetHitProxy(HitX, HitY));
	if (!ActorHit)
	{
		return nullptr;
	}

	if (UMeshComponent* TargetComponent = ResolveMeshComponentFromPrimitive(
		const_cast<UPrimitiveComponent*>(ActorHit->PrimComponent.Get())))
	{
		return TargetComponent;
	}

	AActor* Actor = ActorHit->Actor;
	if (Actor && Actor->IsSelectionChild())
	{
		Actor = Actor->GetRootSelectionParent();
	}

	return Actor ? Actor->FindComponentByClass<UMeshComponent>() : nullptr;
}

void SyncEditorSelectionToTarget(UMeshComponent* TargetComponent)
{
	if (!TargetComponent || !GEditor)
	{
		return;
	}

	AActor* Owner = TargetComponent->GetOwner();
	if (!Owner)
	{
		return;
	}

	GEditor->SelectNone(false, true, false);
	GEditor->SelectActor(Owner, true, false, true, true);
	GEditor->SelectComponent(TargetComponent, true, true, true);
}
}

UInteractiveTool* UQuickSDFSelectToolBuilder::BuildTool(const FToolBuilderState& SceneState) const
{
	return NewObject<UQuickSDFSelectTool>(SceneState.ToolManager);
}

void UQuickSDFSelectTool::Setup()
{
	Super::Setup();

	Properties = NewObject<UQuickSDFToolProperties>(this);
	Properties->SetFlags(RF_Transactional);
	AddToolPropertySource(Properties);
	SyncFromSubsystemTarget(false);
}

void UQuickSDFSelectTool::Shutdown(EToolShutdownType ShutdownType)
{
	RestoreActiveMaterialSlotHighlight();
	Properties = nullptr;
	SyncedComponent.Reset();
	Super::Shutdown(ShutdownType);
}

void UQuickSDFSelectTool::OnTick(float DeltaTime)
{
	Super::OnTick(DeltaTime);

	const UQuickSDFToolSubsystem* Subsystem = GEditor ? GEditor->GetEditorSubsystem<UQuickSDFToolSubsystem>() : nullptr;
	UMeshComponent* TargetComponent = Subsystem ? Subsystem->GetTargetMeshComponent() : nullptr;
	if (SyncedComponent.Get() != TargetComponent)
	{
		SyncFromSubsystemTarget(true);
		return;
	}

	const UQuickSDFAsset* Asset = Subsystem ? Subsystem->GetActiveSDFAsset() : nullptr;
	const FQuickSDFTextureSetData* ActiveSet = Asset ? Asset->GetActiveTextureSet() : nullptr;
	const int32 ActiveTextureSetIndex = Asset ? Asset->ActiveTextureSetIndex : INDEX_NONE;
	const int32 ActiveMaterialSlot = ActiveSet ? ActiveSet->MaterialSlotIndex : INDEX_NONE;
	if (CachedActiveTextureSetIndex != ActiveTextureSetIndex ||
		CachedActiveMaterialSlot != ActiveMaterialSlot)
	{
		RefreshActiveMaterialSlotHighlight();
	}
}

void UQuickSDFSelectTool::OnClicked(const FInputDeviceRay& ClickPos)
{
	IToolsContextQueriesAPI* ContextAPI = GetToolManager()->GetContextQueriesAPI();
	if (UMeshComponent* HitProxyTargetComponent = ResolveMeshComponentFromHitProxy(ClickPos, ContextAPI))
	{
		QuickSDFMaterialSlotHitTest::FResult SlotHit;
		const int32 HitMaterialSlot = QuickSDFMaterialSlotHitTest::HitTestMaterialSlot(HitProxyTargetComponent, ClickPos.WorldRay, SlotHit)
			? SlotHit.MaterialSlotIndex
			: INDEX_NONE;
		SelectTargetComponent(HitProxyTargetComponent, HitMaterialSlot);
		return;
	}

	UWorld* World = ContextAPI ? ContextAPI->GetCurrentEditingWorld() : nullptr;
	if (!World)
	{
		return;
	}

	const FVector RayOrigin = ClickPos.WorldRay.Origin;
	const FVector RayDirection = ClickPos.WorldRay.Direction.GetSafeNormal();
	if (RayDirection.IsNearlyZero())
	{
		return;
	}

	FHitResult OutHit;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(QuickSDFSelect), true);
	const bool bPhysicsHit = World->LineTraceSingleByChannel(
		OutHit,
		RayOrigin,
		RayOrigin + RayDirection * QuickSDFSelectRayLength,
		ECC_Visibility,
		Params);

	if (!bPhysicsHit)
	{
		return;
	}

	UMeshComponent* TargetComponent = ResolveMeshComponentFromPrimitive(OutHit.GetComponent());
	QuickSDFMaterialSlotHitTest::FResult SlotHit;
	const int32 HitMaterialSlot = QuickSDFMaterialSlotHitTest::HitTestMaterialSlot(TargetComponent, ClickPos.WorldRay, SlotHit)
		? SlotHit.MaterialSlotIndex
		: INDEX_NONE;
	SelectTargetComponent(TargetComponent, HitMaterialSlot);
}

void UQuickSDFSelectTool::SelectTargetComponent(UMeshComponent* TargetComponent, int32 MaterialSlotIndex)
{
	if (!TargetComponent || !GEditor)
	{
		return;
	}

	if (UQuickSDFToolSubsystem* QuickSDFToolSubsystem = GEditor->GetEditorSubsystem<UQuickSDFToolSubsystem>())
	{
		QuickSDFToolSubsystem->SetTargetComponent(TargetComponent);
		SyncEditorSelectionToTarget(TargetComponent);
		SyncFromSubsystemTarget(true);
		if (MaterialSlotIndex != INDEX_NONE)
		{
			SelectActiveMaterialSlot(TargetComponent, MaterialSlotIndex);
		}
		GEditor->RedrawAllViewports(false);
	}
}

bool UQuickSDFSelectTool::SelectActiveMaterialSlot(UMeshComponent* TargetComponent, int32 MaterialSlotIndex)
{
	if (!Properties || !TargetComponent || MaterialSlotIndex < 0 || MaterialSlotIndex >= TargetComponent->GetNumMaterials() || !GEditor)
	{
		return false;
	}

	UQuickSDFToolSubsystem* Subsystem = GEditor->GetEditorSubsystem<UQuickSDFToolSubsystem>();
	UQuickSDFAsset* Asset = Subsystem ? Subsystem->GetActiveSDFAsset() : nullptr;
	if (!Asset)
	{
		return false;
	}

	const TArray<int32> VisibleTextureSetIndices = QuickSDFTextureSetSync::GetVisibleTextureSetIndices(Asset, TargetComponent);
	for (const int32 TextureSetIndex : VisibleTextureSetIndices)
	{
		if (Asset->TextureSets.IsValidIndex(TextureSetIndex) &&
			Asset->TextureSets[TextureSetIndex].MaterialSlotIndex == MaterialSlotIndex)
		{
			if (!Asset->SetActiveTextureSetIndex(TextureSetIndex))
			{
				return false;
			}

			QuickSDFTextureSetSync::SyncPropertiesFromActiveAsset(Properties, Asset);
			RefreshActiveMaterialSlotHighlight();
			NotifyOfPropertyChangeByTool(Properties);
			return true;
		}
	}

	return false;
}

UMaterialInterface* UQuickSDFSelectTool::GetOrCreateActiveSlotHighlightMaterial()
{
	if (ActiveSlotHighlightMaterial)
	{
		return ActiveSlotHighlightMaterial;
	}

	if (UMaterialInterface* ConfiguredMaterial = Cast<UMaterialInterface>(StaticLoadObject(
		UMaterialInterface::StaticClass(),
		nullptr,
		QuickSDFActiveSlotHighlightMaterialObjectPath,
		nullptr,
		LOAD_NoWarn)))
	{
		ActiveSlotHighlightMaterial = UMaterialInstanceDynamic::Create(ConfiguredMaterial, this);
		if (ActiveSlotHighlightMaterial)
		{
			ActiveSlotHighlightMaterial->SetFlags(RF_Transient);
		}
		return ActiveSlotHighlightMaterial;
	}

	ActiveSlotHighlightMaterial = CreateTransientActiveSlotHighlightMaterial(this);
	return ActiveSlotHighlightMaterial;
}

void UQuickSDFSelectTool::RefreshActiveMaterialSlotHighlight()
{
	UQuickSDFToolSubsystem* Subsystem = GEditor ? GEditor->GetEditorSubsystem<UQuickSDFToolSubsystem>() : nullptr;
	UMeshComponent* TargetComponent = Subsystem ? Subsystem->GetTargetMeshComponent() : nullptr;
	const UQuickSDFAsset* Asset = Subsystem ? Subsystem->GetActiveSDFAsset() : nullptr;
	const FQuickSDFTextureSetData* ActiveSet = Asset ? Asset->GetActiveTextureSet() : nullptr;
	const int32 ActiveTextureSetIndex = Asset ? Asset->ActiveTextureSetIndex : INDEX_NONE;
	const int32 ActiveMaterialSlot = ActiveSet ? ActiveSet->MaterialSlotIndex : INDEX_NONE;

	CachedActiveTextureSetIndex = ActiveTextureSetIndex;
	CachedActiveMaterialSlot = ActiveMaterialSlot;

	if (!TargetComponent ||
		ActiveMaterialSlot < 0 ||
		ActiveMaterialSlot >= TargetComponent->GetNumMaterials())
	{
		RestoreActiveMaterialSlotHighlight();
		return;
	}

	if (HighlightedComponent.Get() != TargetComponent)
	{
		RestoreActiveMaterialSlotHighlight();
		HighlightedComponent = TargetComponent;
		OriginalMaterialSlotOverlayMaterials = TargetComponent->MaterialSlotsOverlayMaterial;
		bHasOriginalMaterialSlotOverlayMaterialState = true;
	}
	else if (!bHasOriginalMaterialSlotOverlayMaterialState)
	{
		OriginalMaterialSlotOverlayMaterials = TargetComponent->MaterialSlotsOverlayMaterial;
		bHasOriginalMaterialSlotOverlayMaterialState = true;
	}

	UMaterialInterface* HighlightMaterial = GetOrCreateActiveSlotHighlightMaterial();
	if (!HighlightMaterial)
	{
		return;
	}

	TargetComponent->MaterialSlotsOverlayMaterial = OriginalMaterialSlotOverlayMaterials;
	if (TargetComponent->MaterialSlotsOverlayMaterial.Num() <= ActiveMaterialSlot)
	{
		TargetComponent->MaterialSlotsOverlayMaterial.SetNumZeroed(ActiveMaterialSlot + 1);
	}
	TargetComponent->MaterialSlotsOverlayMaterial[ActiveMaterialSlot] = HighlightMaterial;
	HighlightedMaterialSlot = ActiveMaterialSlot;
	TargetComponent->MarkRenderStateDirty();

	if (GEditor)
	{
		GEditor->RedrawAllViewports(false);
	}
}

void UQuickSDFSelectTool::RestoreActiveMaterialSlotHighlight()
{
	UMeshComponent* Component = HighlightedComponent.Get();
	if (Component && bHasOriginalMaterialSlotOverlayMaterialState)
	{
		Component->MaterialSlotsOverlayMaterial = OriginalMaterialSlotOverlayMaterials;
		Component->MarkRenderStateDirty();
	}

	HighlightedComponent.Reset();
	HighlightedMaterialSlot = INDEX_NONE;
	bHasOriginalMaterialSlotOverlayMaterialState = false;
	OriginalMaterialSlotOverlayMaterials.Reset();

	if (GEditor)
	{
		GEditor->RedrawAllViewports(false);
	}
}

void UQuickSDFSelectTool::SyncFromSubsystemTarget(bool bBroadcastPropertyRefresh)
{
	if (!Properties || !GEditor)
	{
		return;
	}

	UQuickSDFToolSubsystem* Subsystem = GEditor->GetEditorSubsystem<UQuickSDFToolSubsystem>();
	UMeshComponent* TargetComponent = Subsystem ? Subsystem->GetTargetMeshComponent() : nullptr;
	UQuickSDFAsset* Asset = Subsystem ? Subsystem->GetActiveSDFAsset() : nullptr;
	if (TargetComponent && Subsystem)
	{
		Asset = Subsystem->GetOrCreateSDFAssetForComponent(TargetComponent);
		Subsystem->SetActiveSDFAsset(Asset);
	}

	QuickSDFTextureSetSync::RefreshTextureSetsForComponent(
		Asset,
		TargetComponent,
		Properties,
		nullptr,
		false);

	SyncedComponent = TargetComponent;
	RefreshActiveMaterialSlotHighlight();

	if (bBroadcastPropertyRefresh)
	{
		NotifyOfPropertyChangeByTool(Properties);
		RemoveToolPropertySource(Properties);
		AddToolPropertySource(Properties);
	}
}
