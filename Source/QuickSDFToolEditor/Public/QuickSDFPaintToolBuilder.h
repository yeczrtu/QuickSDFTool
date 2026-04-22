#pragma once

#include "CoreMinimal.h"
#include "InteractiveToolBuilder.h"
#include "QuickSDFPaintToolBuilder.generated.h"

UCLASS()
class UQuickSDFPaintToolBuilder : public UInteractiveToolBuilder
{
	GENERATED_BODY()

public:
	virtual bool CanBuildTool(const FToolBuilderState& SceneState) const override;
	virtual UInteractiveTool* BuildTool(const FToolBuilderState& SceneState) const override;
};
