#pragma once
#include "CoreMinimal.h"
#include "BaseTools/SingleClickTool.h"
#include "QuickSDFSelectTool.generated.h"

class UMeshComponent;
class UMaterialInterface;
class UQuickSDFToolProperties;

UCLASS()
class QUICKSDFTOOLEDITOR_API UQuickSDFSelectToolBuilder : public USingleClickToolBuilder
{
	GENERATED_BODY()
public:
	virtual UInteractiveTool* BuildTool(const FToolBuilderState& SceneState) const override;
};

UCLASS()
class QUICKSDFTOOLEDITOR_API UQuickSDFSelectTool : public USingleClickTool
{
	GENERATED_BODY()
public:
	virtual void Setup() override;
	virtual void Shutdown(EToolShutdownType ShutdownType) override;
	virtual void OnTick(float DeltaTime) override;
	virtual void OnClicked(const FInputDeviceRay& ClickPos) override;

	void RefreshActiveMaterialSlotHighlight();
	void RestoreActiveMaterialSlotHighlight();

	UPROPERTY(Transient)
	TObjectPtr<UQuickSDFToolProperties> Properties;

private:
	void SelectTargetComponent(UMeshComponent* TargetComponent);
	void SyncFromSubsystemTarget(bool bBroadcastPropertyRefresh);
	UMaterialInterface* GetOrCreateActiveSlotHighlightMaterial();

	TWeakObjectPtr<UMeshComponent> SyncedComponent;
	TWeakObjectPtr<UMeshComponent> HighlightedComponent;
	int32 HighlightedMaterialSlot = INDEX_NONE;
	int32 CachedActiveTextureSetIndex = INDEX_NONE;
	int32 CachedActiveMaterialSlot = INDEX_NONE;
	bool bHasOriginalMaterialSlotOverlayMaterialState = false;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInterface> ActiveSlotHighlightMaterial;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UMaterialInterface>> OriginalMaterialSlotOverlayMaterials;
};
