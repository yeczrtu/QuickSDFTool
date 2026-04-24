#pragma once
#include "CoreMinimal.h"
#include "BaseTools/SingleClickTool.h"
#include "QuickSDFSelectTool.generated.h"

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
	virtual void OnClicked(const FInputDeviceRay& ClickPos) override;
};