#include "QuickSDFToolEditorModule.h"
#include "QuickSDFEditorModeCommands.h"
#include "PropertyEditorModule.h"
#include "QuickSDFToolProperties.h"
#include "QuickSDFToolPropertiesDetails.h"
#include "QuickSDFToolStyle.h"
#include "SQuickSDFPaintCanvas.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Docking/TabManager.h"
#include "Widgets/Docking/SDockTab.h"

#define LOCTEXT_NAMESPACE "FQuickSDFToolEditorModule"

void FQuickSDFToolEditorModule::StartupModule()
{
	FCoreDelegates::OnPostEngineInit.AddRaw(this, &FQuickSDFToolEditorModule::OnPostEngineInit);
	FPropertyEditorModule& PropertyModule = FModuleManager::LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");
	PropertyModule.RegisterCustomClassLayout(
		UQuickSDFToolProperties::StaticClass()->GetFName(),
		FOnGetDetailCustomizationInstance::CreateStatic(&FQuickSDFToolPropertiesDetails::MakeInstance)
	);
}

void FQuickSDFToolEditorModule::ShutdownModule()
{
	FCoreDelegates::OnPostEngineInit.RemoveAll(this);
	UnregisterPaintCanvasTab();
	FQuickSDFEditorModeCommands::Unregister();
	FQuickSDFToolStyle::Shutdown();
	
	if (FModuleManager::Get().IsModuleLoaded("PropertyEditor"))
	{
		FPropertyEditorModule& PropertyModule = FModuleManager::GetModuleChecked<FPropertyEditorModule>("PropertyEditor");
		PropertyModule.UnregisterCustomClassLayout(UQuickSDFToolProperties::StaticClass()->GetFName());
	}
}

void FQuickSDFToolEditorModule::OnPostEngineInit()
{
	FQuickSDFToolStyle::Initialize();
	FQuickSDFEditorModeCommands::Register();
	RegisterPaintCanvasTab();
}

void FQuickSDFToolEditorModule::RegisterPaintCanvasTab()
{
	FGlobalTabmanager::Get()->RegisterNomadTabSpawner(
		QuickSDFPaintCanvas::GetTabId(),
		FOnSpawnTab::CreateRaw(this, &FQuickSDFToolEditorModule::SpawnPaintCanvasTab))
		.SetDisplayName(LOCTEXT("QuickSDFPaintCanvasTab", "Quick SDF 2D Canvas"))
		.SetTooltipText(LOCTEXT("QuickSDFPaintCanvasTabTooltip", "Paint the active Quick SDF mask in a dedicated 2D canvas."))
		.SetIcon(FSlateIcon(FQuickSDFToolStyle::GetStyleSetName(), "QuickSDF.PaintTextureColor"));
}

void FQuickSDFToolEditorModule::UnregisterPaintCanvasTab()
{
	if (FSlateApplication::IsInitialized())
	{
		FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(QuickSDFPaintCanvas::GetTabId());
	}
}

TSharedRef<SDockTab> FQuickSDFToolEditorModule::SpawnPaintCanvasTab(const FSpawnTabArgs& Args)
{
	return SNew(SDockTab)
		.TabRole(ETabRole::NomadTab)
		.Label(LOCTEXT("QuickSDFPaintCanvasTabLabel", "Quick SDF 2D Canvas"))
		[
			SNew(SQuickSDFPaintCanvas)
		];
}

#undef LOCTEXT_NAMESPACE
	
IMPLEMENT_MODULE(FQuickSDFToolEditorModule, QuickSDFToolEditor)
