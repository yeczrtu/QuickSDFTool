#include "QuickSDFToolEditorModule.h"
#include "QuickSDFEditorModeCommands.h"
#include "PropertyEditorModule.h"
#include "QuickSDFToolProperties.h"
#include "QuickSDFToolPropertiesDetails.h"
#include "QuickSDFToolStyle.h"

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
}

#undef LOCTEXT_NAMESPACE
	
IMPLEMENT_MODULE(FQuickSDFToolEditorModule, QuickSDFToolEditor)
