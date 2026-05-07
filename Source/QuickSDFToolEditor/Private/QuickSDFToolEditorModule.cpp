#include "QuickSDFToolEditorModule.h"
#include "QuickSDFEditorModeCommands.h"
#include "PropertyEditorModule.h"
#include "QuickSDFToolProperties.h"
#include "QuickSDFToolPropertiesDetails.h"
#include "QuickSDFToolStyle.h"
#include "SQuickSDFPaintCanvas.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Docking/TabManager.h"
#include "ToolMenus.h"
#include "Widgets/Docking/SDockTab.h"

#define LOCTEXT_NAMESPACE "FQuickSDFToolEditorModule"

namespace
{
const FName QuickSDFToolEditorMenuOwnerName(TEXT("QuickSDFToolEditor"));
const FName QuickSDFOpen2DCanvasMenuEntryName(TEXT("QuickSDFOpen2DCanvas"));
}

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
	UnregisterMenus();
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
	if (UToolMenus::IsToolMenuUIEnabled())
	{
		RegisterMenus();
	}
}

void FQuickSDFToolEditorModule::RegisterPaintCanvasTab()
{
	FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(QuickSDFPaintCanvas::GetTabId());
	FGlobalTabmanager::Get()->RegisterNomadTabSpawner(
		QuickSDFPaintCanvas::GetTabId(),
		FOnSpawnTab::CreateRaw(this, &FQuickSDFToolEditorModule::SpawnPaintCanvasTab))
		.SetDisplayName(LOCTEXT("QuickSDFPaintCanvasTab", "Quick SDF 2D Canvas"))
		.SetTooltipText(LOCTEXT("QuickSDFPaintCanvasTabTooltip", "Paint the active Quick SDF mask in a dedicated 2D canvas."))
		.SetIcon(FSlateIcon(FQuickSDFToolStyle::GetStyleSetName(), "QuickSDF.PaintTextureColor"))
		.SetMenuType(ETabSpawnerMenuType::Hidden)
		.SetAutoGenerateMenuEntry(false);
}

void FQuickSDFToolEditorModule::UnregisterPaintCanvasTab()
{
	if (FSlateApplication::IsInitialized())
	{
		FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(QuickSDFPaintCanvas::GetTabId());
	}
}

void FQuickSDFToolEditorModule::RegisterMenus()
{
	UToolMenus* ToolMenus = UToolMenus::Get();
	ToolMenus->UnregisterOwnerByName(QuickSDFToolEditorMenuOwnerName);

	FToolMenuOwnerScoped OwnerScoped(QuickSDFToolEditorMenuOwnerName);
	UToolMenu* WindowMenu = ToolMenus->ExtendMenu("LevelEditor.MainMenu.Window");
	FToolMenuSection& WindowSection = WindowMenu->FindOrAddSection("WindowLayout");
	if (!WindowSection.FindEntry(QuickSDFOpen2DCanvasMenuEntryName))
	{
		WindowSection.AddMenuEntry(
			QuickSDFOpen2DCanvasMenuEntryName,
			LOCTEXT("QuickSDFOpen2DCanvasMenuLabel", "Quick SDF 2D Canvas"),
			LOCTEXT("QuickSDFOpen2DCanvasMenuTooltip", "Open the Quick SDF 2D Canvas."),
			FSlateIcon(FQuickSDFToolStyle::GetStyleSetName(), "QuickSDF.PaintTextureColor"),
			FUIAction(FExecuteAction::CreateStatic(&QuickSDFPaintCanvas::OpenTab)));
	}
	ToolMenus->RefreshMenuWidget("LevelEditor.MainMenu.Window");
}

void FQuickSDFToolEditorModule::UnregisterMenus()
{
	if (UToolMenus* ToolMenus = UToolMenus::TryGet())
	{
		ToolMenus->UnregisterOwnerByName(QuickSDFToolEditorMenuOwnerName);
		ToolMenus->RefreshMenuWidget("LevelEditor.MainMenu.Window");
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
