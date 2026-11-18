using UnrealBuildTool;
using System.IO;

public class MetaBrainCore : ModuleRules
{
	public MetaBrainCore(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		CppStandard = CppStandardVersion.Cpp20;
		bEnableExceptions = true;

		PublicIncludePaths.AddRange(new string[] {
			Path.Combine(ModuleDirectory, "Public"),
			Path.Combine(ModuleDirectory, "../../include")
		});

		// Strictly no UObject or Engine dependencies in MetaBrainCore
		PublicDependencyModuleNames.AddRange(new string[] {
			"Core"
		});
	}
}
