#include "QuickSDFEditorMode.h"
#include "QuickSDFEditorModeToolkit.h"
#include "EngineUtils.h"
#include "Engine/DirectionalLight.h"
#include "InteractiveToolManager.h"
#include "QuickSDFEditorModeCommands.h"
#include "QuickSDFPaintToolBuilder.h"
#include "QuickSDFSelectTool.h"
#include "QuickSDFToolSubsystem.h"
#include "Tools/EdModeInteractiveToolsContext.h"
#include "SQuickSDFTimeline.h"
#include "LevelEditor.h"
#include "SLevelViewport.h"


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
	
	FQuickSDFEditorModeCommands ToolManagerCommands = FQuickSDFEditorModeCommands::Get();
	
	RegisterTool(ToolManagerCommands.SelectTextureAsset, TEXT("QuickSDFSelectTool"), NewObject<UQuickSDFSelectToolBuilder>(this));
	RegisterTool(ToolManagerCommands.PaintTextureColor, TEXT("QuickSDFPaintTool"), NewObject<UQuickSDFPaintToolBuilder>(this));
	
	GetInteractiveToolsContext()->StartTool(TEXT("QuickSDFPaintTool"));
	GetToolManager()->ConfigureChangeTrackingMode(EToolChangeTrackingMode::NoChangeTracking);
	Toolkit->SetCurrentPalette(FName(TEXT("Default")));
	
	// Add Timeline UI to viewport overlay
	if (FModuleManager::Get().IsModuleLoaded("LevelEditor"))
	{
		FLevelEditorModule& LevelEditorModule = FModuleManager::GetModuleChecked<FLevelEditorModule>("LevelEditor");
		TSharedPtr<ILevelEditor> LevelEditor = LevelEditorModule.GetFirstLevelEditor();
		if (LevelEditor.IsValid())
		{
			TSharedPtr<SLevelViewport> ActiveViewport = LevelEditor->GetActiveViewportInterface();
			if (ActiveViewport.IsValid())
			{
				TimelineWidget = SNew(SQuickSDFTimeline);
				ActiveViewport->AddOverlayWidget(TimelineWidget.ToSharedRef());
			}
		}
	}
}

void UQuickSDFEditorMode::Exit()
{
	if (TimelineWidget.IsValid())
	{
		if (FModuleManager::Get().IsModuleLoaded("LevelEditor"))
		{
			FLevelEditorModule& LevelEditorModule = FModuleManager::GetModuleChecked<FLevelEditorModule>("LevelEditor");
			TSharedPtr<ILevelEditor> LevelEditor = LevelEditorModule.GetFirstLevelEditor();
			if (LevelEditor.IsValid())
			{
				TSharedPtr<SLevelViewport> ActiveViewport = LevelEditor->GetActiveViewportInterface();
				if (ActiveViewport.IsValid())
				{
					ActiveViewport->RemoveOverlayWidget(TimelineWidget.ToSharedRef());
				}
			}
		}
		TimelineWidget.Reset();
	}
	
	Super::Exit();
	// Clean up tools
}

void UQuickSDFEditorMode::Tick(FEditorViewportClient* ViewportClient, float DeltaTime)
{

}

TMap<FName, TArray<TSharedPtr<FUICommandInfo>>> UQuickSDFEditorMode::GetModeCommands() const
{
	return FQuickSDFEditorModeCommands::GetCommands();
}

void UQuickSDFEditorMode::ActorSelectionChangeNotify()
{
	if (UQuickSDFToolSubsystem* QuickSDFToolSubsystem = GEditor->GetEditorSubsystem<UQuickSDFToolSubsystem>())
	{
		FToolBuilderState SelectionState;
		GetToolManager()->GetContextQueriesAPI()->GetCurrentSelectionState(SelectionState);

		UMeshComponent* TargetComp = nullptr;
		
		if (SelectionState.SelectedComponents.Num() > 0)
		{
			TargetComp = Cast<UMeshComponent>(SelectionState.SelectedComponents[0]);
		}
		
		if (TargetComp == nullptr && SelectionState.SelectedActors.Num() > 0)
		{
			if (AActor* SelectedActor = Cast<AActor>(SelectionState.SelectedActors[0]))
			{
				TargetComp = SelectedActor->FindComponentByClass<UMeshComponent>();
			}
		}
		
		if (TargetComp)
		{
			QuickSDFToolSubsystem->SetTargetComponent(TargetComp);
		}
	}
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
