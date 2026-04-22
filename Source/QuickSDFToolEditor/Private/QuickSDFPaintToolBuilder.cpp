#include "QuickSDFPaintToolBuilder.h"
#include "QuickSDFPaintTool.h"
#include "InteractiveToolManager.h"

bool UQuickSDFPaintToolBuilder::CanBuildTool(const FToolBuilderState& SceneState) const
{
	return true;
}

UInteractiveTool* UQuickSDFPaintToolBuilder::BuildTool(const FToolBuilderState& SceneState) const
{
	UQuickSDFPaintTool* NewTool = NewObject<UQuickSDFPaintTool>(SceneState.ToolManager);
	return NewTool;
}
