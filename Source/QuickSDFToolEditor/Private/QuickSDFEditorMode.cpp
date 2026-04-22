#include "QuickSDFEditorMode.h"
#include "QuickSDFEditorModeToolkit.h"
#include "EngineUtils.h"
#include "Engine/DirectionalLight.h"
#include "InteractiveToolManager.h"
#include "QuickSDFPaintToolBuilder.h"
#include "Tools/EdModeInteractiveToolsContext.h"


const FEditorModeID UQuickSDFEditorMode::EM_QuickSDFEditorModeId = TEXT("EM_QuickSDFEditorMode");

UQuickSDFEditorMode::UQuickSDFEditorMode()
{
	Info = FEditorModeInfo(EM_QuickSDFEditorModeId, 
		INVTEXT("Quick SDF"),
		FSlateIcon(FAppStyle::GetAppStyleSetName(), "LevelEditor.MeshPaintMode", "LevelEditor.MeshPaintMode.Small"), 
		true);
}

void UQuickSDFEditorMode::Enter()
{
	Super::Enter();
	//GetToolManager()->RegisterToolType(TEXT("QuickSDFPaintTool"), NewObject<UQuickSDFPaintToolBuilder>(this));
	// コマンドを作ってビルダーと紐づけてレジストもする。参考↓
	//Engine/Plugins/MeshPainting/Source/MeshPaintEditorMode/Private/MeshPaintMode.cpp:146 
	UEditorInteractiveToolsContext* UseToolsContext = GetInteractiveToolsContext(GetDefaultToolScope());
	UseToolsContext->ToolManager->RegisterToolType(TEXT("QuickSDFPaintTool"), NewObject<UQuickSDFPaintToolBuilder>(this));
	GetInteractiveToolsContext()->StartTool(TEXT("QuickSDFPaintTool"));
	GetToolManager()->ConfigureChangeTrackingMode(EToolChangeTrackingMode::NoChangeTracking);
	Toolkit->SetCurrentPalette(TEXT("test"));
}

void UQuickSDFEditorMode::Exit()
{
	Super::Exit();
	// Clean up tools
}

void UQuickSDFEditorMode::Tick(FEditorViewportClient* ViewportClient, float DeltaTime)
{

}

void UQuickSDFEditorMode::CreateToolkit()
{
	Toolkit = MakeShared<FQuickSDFEditorModeToolkit>();
}

void UQuickSDFEditorMode::SetPreviewLightAngle(float AzimuthAngle)
{
	UWorld* World = GetWorld();
	if (!World) return;

	for (TActorIterator<ADirectionalLight> It(World); It; ++It)
	{
		ADirectionalLight* DirLight = *It;
		if (DirLight)
		{
			// Fixed Pitch, rotate Yaw for preview
			FRotator NewRotation(-45.0f, AzimuthAngle, 0.0f);
			DirLight->SetActorRotation(NewRotation);
			break;
		}
	}
}
