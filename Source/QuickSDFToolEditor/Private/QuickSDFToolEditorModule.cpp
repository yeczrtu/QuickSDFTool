#include "QuickSDFToolEditorModule.h"
#include "QuickSDFEditorModeCommands.h"

#define LOCTEXT_NAMESPACE "FQuickSDFToolEditorModule"

void FQuickSDFToolEditorModule::StartupModule()
{
	FCoreDelegates::OnPostEngineInit.AddRaw(this, &FQuickSDFToolEditorModule::OnPostEngineInit);
}

void FQuickSDFToolEditorModule::ShutdownModule()
{
	FCoreDelegates::OnPostEngineInit.RemoveAll(this);
	FQuickSDFEditorModeCommands::Unregister();
}

void FQuickSDFToolEditorModule::OnPostEngineInit()
{
	FQuickSDFEditorModeCommands::Register();
}

#undef LOCTEXT_NAMESPACE
	
IMPLEMENT_MODULE(FQuickSDFToolEditorModule, QuickSDFToolEditor)
