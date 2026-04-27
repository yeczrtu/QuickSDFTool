#include "QuickSDFToolEditorModule.h"
#include "QuickSDFEditorModeCommands.h"
#include "PropertyEditorModule.h"
#include "QuickSDFPaintTool.h"
#include "QuickSDFToolPropertiesDetails.h"

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
	FQuickSDFEditorModeCommands::Unregister();
	
	if (FModuleManager::Get().IsModuleLoaded("PropertyEditor"))
	{
		FPropertyEditorModule& PropertyModule = FModuleManager::GetModuleChecked<FPropertyEditorModule>("PropertyEditor");
		PropertyModule.UnregisterCustomClassLayout(UQuickSDFToolProperties::StaticClass()->GetFName());
	}
}

void FQuickSDFToolEditorModule::OnPostEngineInit()
{
	FQuickSDFEditorModeCommands::Register();
}

#undef LOCTEXT_NAMESPACE
	
IMPLEMENT_MODULE(FQuickSDFToolEditorModule, QuickSDFToolEditor)
