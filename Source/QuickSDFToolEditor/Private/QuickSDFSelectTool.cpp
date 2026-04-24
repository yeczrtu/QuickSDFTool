#include "QuickSDFSelectTool.h"
#include "InteractiveToolManager.h"
#include "ToolContextInterfaces.h"
#include "QuickSDFToolSubsystem.h"

UInteractiveTool* UQuickSDFSelectToolBuilder::BuildTool(const FToolBuilderState& SceneState) const
{
	return NewObject<UQuickSDFSelectTool>(SceneState.ToolManager);
}

void UQuickSDFSelectTool::Setup()
{
	UInteractiveTool::Setup();
	// ヘルプメッセージなどを表示
}

void UQuickSDFSelectTool::OnClicked(const FInputDeviceRay& ClickPos)
{
	/*FHitResult OutHit;
	UWorld* World = GetToolManager()->GetContextQueriesAPI()->GetCurrentEditingWorld();
	FCollisionQueryParams Params(SCENE_QUERY_STAT(QuickSDFSelect), true);

	// 1. 普通のレイキャストでメッシュを探す
	if (World->LineTraceSingleByChannel(OutHit, ClickPos.WorldRay.Origin, 
		ClickPos.WorldRay.Origin + ClickPos.WorldRay.Direction * 100000.f, 
		ECC_Visibility, Params))
	{
		if (UPrimitiveComponent* HitComponent = OutHit.GetComponent())
		{
			// 2. サブシステムに「これがターゲットだ」と伝える
			if (UQuickSDFToolSubsystem* QuickSDFToolSubsystem = GEditor->GetEditorSubsystem<UQuickSDFToolSubsystem>())
			{
				QuickSDFToolSubsystem->SetTargetComponent(HitComponent);
			}
			// 3. 選択できたら、自動的にペイントツールに切り替える（MeshPaint風の挙動にする場合）
			// GetParentEditorMode()->ActivatePaintTool(); 
		}
	}*/
}