// Copyright MetaBrain Project. Unreal Engine 5.8 Module Build Rules.

using UnrealBuildTool;
using System.IO;

public class MetaBrain : ModuleRules
{
    public MetaBrain(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        CppStandard = CppStandardVersion.Cpp20;

        PublicIncludePaths.AddRange(new string[] {
            Path.Combine(ModuleDirectory, "../../include")
        });

        PublicDependencyModuleNames.AddRange(new string[] {
            "Core",
            "CoreUObject",
            "Engine",
            "InputCore",
            "Niagara",
            "NiagaraCore",
            "NiagaraShader",
            "RenderCore",
            "RHI",
            "Json",
            "JsonUtilities",
            "UMG"
        });
    }
}
