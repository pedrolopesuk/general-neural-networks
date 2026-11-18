using UnrealBuildTool;
using System.Collections.Generic;

public class MetaBrainUEEditorTarget : TargetRules
{
	public MetaBrainUEEditorTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Editor;
		DefaultBuildSettings = BuildSettingsVersion.Latest;
		IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
		ExtraModuleNames.AddRange(new string[] { "MetaBrainCore", "MetaBrainViz" });
	}
}
