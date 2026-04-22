#include "QuickSDFToolShadersModule.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/Paths.h"
#include "ShaderCore.h"

#define LOCTEXT_NAMESPACE "FQuickSDFToolShadersModule"

void FQuickSDFToolShadersModule::StartupModule()
{
	// Get the base directory of this plugin
	FString PluginShaderDir = FPaths::Combine(IPluginManager::Get().FindPlugin(TEXT("QuickSDFTool"))->GetBaseDir(), TEXT("Shaders"));
	AddShaderSourceDirectoryMapping(TEXT("/Plugin/QuickSDFTool"), PluginShaderDir);
}

void FQuickSDFToolShadersModule::ShutdownModule()
{
	ResetAllShaderSourceDirectoryMappings();
}

#undef LOCTEXT_NAMESPACE
	
IMPLEMENT_MODULE(FQuickSDFToolShadersModule, QuickSDFToolShaders)
