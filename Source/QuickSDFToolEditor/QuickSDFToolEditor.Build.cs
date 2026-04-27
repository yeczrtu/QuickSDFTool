using UnrealBuildTool;

public class QuickSDFToolEditor : ModuleRules
{
	public QuickSDFToolEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;
		
		PublicIncludePaths.AddRange(new string[] { });
		PrivateIncludePaths.AddRange(new string[] { });

		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				"QuickSDFTool",
				"InteractiveToolsFramework",
				"EditorInteractiveToolsFramework",
				"GeometryCore",
				"DynamicMesh",
				"ModelingComponents",
				"MeshConversion",
				"EditorSubsystem",
				"UMG"
			}
		);

		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"CoreUObject",
				"Engine",
				"Slate",
				"SlateCore",
				"InputCore",
				"ToolMenus",
				"EditorFramework",
				"RenderCore",
				"RHI",
				"UnrealEd",
				"ApplicationCore",
				"LevelEditor",
				"PropertyEditor",
				"Projects",
				"EditorStyle"
			}
		);
	}
}
