#include "QuickSDFEditorMode.h"
#include "Editor.h"
#include "QuickSDFEditorModeToolkit.h"
#include "EngineUtils.h"
#include "Engine/DirectionalLight.h"
#include "Components/DirectionalLightComponent.h"
#include "InteractiveToolManager.h"
#include "QuickSDFEditorModeCommands.h"
#include "QuickSDFPaintToolBuilder.h"
#include "QuickSDFToolSubsystem.h"
#include "Tools/EdModeInteractiveToolsContext.h"
#include "QuickSDFSelectTool.h"
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

	MuteLights();

	// Register save delegates to prevent permanent lighting changes
	FEditorDelegates::PreSaveWorldWithContext.AddUObject(this, &UQuickSDFEditorMode::OnPreSaveWorld);
	FEditorDelegates::PostSaveWorldWithContext.AddUObject(this, &UQuickSDFEditorMode::OnPostSaveWorld);

	// Auto-select target if something is already selected
	ActorSelectionChangeNotify();
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

	RestoreLights();

	// Unregister save delegates
	FEditorDelegates::PreSaveWorldWithContext.RemoveAll(this);
	FEditorDelegates::PostSaveWorldWithContext.RemoveAll(this);

	// Destroy preview light
	if (PreviewLight)
	{
		PreviewLight->Destroy();
		PreviewLight = nullptr;
	}

	Super::Exit();
	// Clean up tools
}

void UQuickSDFEditorMode::MuteLights()
{
	if (OriginalLightStates.Num() > 0) return;

	if (UWorld* World = GetWorld())
	{
		for (TActorIterator<ADirectionalLight> It(World); It; ++It)
		{
			if (ADirectionalLight* Light = *It)
			{
				// Skip the preview light itself
				if (Light == PreviewLight) continue;

				FQuickSDFLightState State;
				State.Light = Light;
				State.Intensity = Light->GetLightComponent()->Intensity;
				OriginalLightStates.Add(State);
				
				Light->GetLightComponent()->SetIntensity(0.0f);
			}
		}

		// Spawn Preview Light if it doesn't exist yet
		if (!PreviewLight)
		{
			FActorSpawnParameters SpawnParams;
			SpawnParams.ObjectFlags |= RF_Transient;
			PreviewLight = World->SpawnActor<ADirectionalLight>(ADirectionalLight::StaticClass(), SpawnParams);
			if (PreviewLight)
			{
				PreviewLight->SetActorLabel(TEXT("QuickSDF_PreviewLight"));
				PreviewLight->GetLightComponent()->SetIntensity(10.0f);
				PreviewLight->GetLightComponent()->SetMobility(EComponentMobility::Movable);
				PreviewLight->SetActorRotation(FRotator(-45.0f, 0.0f, 0.0f));
			}
		}
	}
}

void UQuickSDFEditorMode::RestoreLights()
{
	for (const FQuickSDFLightState& State : OriginalLightStates)
	{
		if (State.Light.IsValid())
		{
			State.Light->GetLightComponent()->SetIntensity(State.Intensity);
		}
	}
	OriginalLightStates.Empty();
}

void UQuickSDFEditorMode::OnPreSaveWorld(UWorld* InWorld, FObjectPreSaveContext InContext)
{
	if (InWorld == GetWorld())
	{
		RestoreLights();
	}
}

void UQuickSDFEditorMode::OnPostSaveWorld(UWorld* InWorld, FObjectPostSaveContext InContext)
{
	if (InWorld == GetWorld())
	{
		MuteLights();
	}
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

bool UQuickSDFEditorMode::IsSelectionAllowed(AActor* InActor, bool bInSelection) const
{
	if (UInteractiveTool* ActiveTool = GetToolManager()->GetActiveTool(EToolSide::Mouse))
	{
		if (ActiveTool->IsA<UQuickSDFSelectTool>())
		{
			return true;
		}
	}
	return false;
}

void UQuickSDFEditorMode::CreateToolkit()
{
	Toolkit = MakeShared<FQuickSDFEditorModeToolkit>();
}

void UQuickSDFEditorMode::SetPreviewLightAngle(float AzimuthAngle)
{
	if (!PreviewLight) return;

	UQuickSDFToolSubsystem* Subsystem = GEditor->GetEditorSubsystem<UQuickSDFToolSubsystem>();
	if (!Subsystem) return;

	UMeshComponent* MeshComp = Subsystem->GetTargetMeshComponent();
	if (!MeshComp)
	{
		// Fallback to absolute yaw if no mesh
		FRotator NewRotation(-45.0f, AzimuthAngle, 0.0f);
		PreviewLight->SetActorRotation(NewRotation);
		return;
	}

	// Map AzimuthAngle (0-180) back to direction relative to mesh
	// 0 = Left (ProjY=1), 90 = Front (ProjX=-1), 180 = Right (ProjY=-1)
	float Alpha = FMath::DegreesToRadians(AzimuthAngle - 90.0f);
	float ProjX = -FMath::Cos(Alpha);
	float ProjY = -FMath::Sin(Alpha);

	FVector MeshForward = MeshComp->GetForwardVector();
	FVector MeshRight = MeshComp->GetRightVector();
	FVector HorizontalDir = ProjX * MeshForward + ProjY * MeshRight;

	// Preserve current pitch
	FRotator CurrentRot = PreviewLight->GetActorRotation();
	float PitchRad = FMath::DegreesToRadians(CurrentRot.Pitch);

	FVector FinalDir = HorizontalDir * FMath::Cos(PitchRad);
	FinalDir.Z = FMath::Sin(PitchRad);

	FRotator NewRot = FinalDir.Rotation();
	NewRot.Roll = 0.0f;
	PreviewLight->SetActorRotation(NewRot);
}
