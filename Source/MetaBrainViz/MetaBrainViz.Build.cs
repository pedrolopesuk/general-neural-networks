using UnrealBuildTool;

public class MetaBrainViz : ModuleRules
{
	public MetaBrainViz(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		CppStandard = CppStandardVersion.Cpp20;

		PublicDependencyModuleNames.AddRange(new string[] {
			"Core",
			"CoreUObject",
			"Engine",
			"InputCore",
			"EnhancedInput",
			"Niagara",
			"UMG",
			"Slate",
			"SlateCore",
			"RenderCore",
			"RHI",
			"MetaBrainCore"
		});
	}
}
