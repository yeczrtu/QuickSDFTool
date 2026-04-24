#pragma once

#include "CoreMinimal.h"
#include "Tools/UEdMode.h"
#include "Tools/LegacyEdModeInterfaces.h"
#include "QuickSDFEditorMode.generated.h"

UCLASS()
class UQuickSDFEditorMode : public UEdMode,  public ILegacyEdModeViewportInterface
{
	GENERATED_BODY()

public:
	const static FEditorModeID EM_QuickSDFEditorModeId;

	UQuickSDFEditorMode();

	virtual void Enter() override;
	virtual void Exit() override;
	virtual void Tick(FEditorViewportClient* ViewportClient, float DeltaTime) override;
	virtual TMap<FName, TArray<TSharedPtr<FUICommandInfo>>> GetModeCommands() const override;
	virtual void ActorSelectionChangeNotify() override;
	// Set the main directional light rotation for previewing
	void SetPreviewLightAngle(float AzimuthAngle);
	
protected:
	virtual void CreateToolkit() override;
};
