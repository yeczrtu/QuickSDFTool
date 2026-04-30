#pragma once

#include "CoreMinimal.h"
#include "Tools/UEdMode.h"
#include "Tools/LegacyEdModeInterfaces.h"
#include "UObject/ObjectSaveContext.h"
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
	virtual bool IsSelectionAllowed(AActor* InActor, bool bInSelection) const override;
	virtual bool InputKey(FEditorViewportClient* ViewportClient, FViewport* Viewport, FKey Key, EInputEvent Event) override;
	// Set the main directional light rotation for previewing
	void SetPreviewLightAngle(float AzimuthAngle);
	void SetTimelineSeekAngle(float AzimuthAngle);
	class ADirectionalLight* GetPreviewLight() const { return PreviewLight; }
	
protected:
	virtual void CreateToolkit() override;
	virtual void BindCommands() override;

private:
	void AttachTimelineToActiveViewport();
	void DetachTimelineFromViewport();
	bool CanSelectRelativeFrame() const;
	void SelectRelativeFrame(int32 Direction);
	static bool IsArrowNavigationKey(FKey Key);
	static bool IsFrameNavigationKey(FKey Key);
	void CacheViewportViewState(FEditorViewportClient* ViewportClient, FViewport* Viewport);
	void RestoreViewportViewState(FEditorViewportClient* ViewportClient);
	void EndViewportNavigationSuppression();
	void MuteLights();
	void RestoreLights();
	void OnPreSaveWorld(UWorld* InWorld, FObjectPreSaveContext InContext);
	void OnPostSaveWorld(UWorld* InWorld, FObjectPostSaveContext InContext);

	TSharedPtr<class SQuickSDFTimeline> TimelineWidget;
	TWeakPtr<class SLevelViewport> TimelineViewport;

	UPROPERTY()
	TObjectPtr<class ADirectionalLight> PreviewLight;

	struct FQuickSDFLightState
	{
		TWeakObjectPtr<class ADirectionalLight> Light;
		float Intensity;
	};
	TArray<FQuickSDFLightState> OriginalLightStates;

	struct FViewportViewState
	{
		FVector Location = FVector::ZeroVector;
		FRotator Rotation = FRotator::ZeroRotator;
		FVector LookAtLocation = FVector::ZeroVector;
		float OrthoZoom = 0.0f;
		bool bPerspective = true;
		bool bValid = false;
	};

	FViewportViewState CachedViewportViewState;
	FEditorViewportClient* SuppressedViewportClient = nullptr;
	FViewport* SuppressedViewport = nullptr;
};
