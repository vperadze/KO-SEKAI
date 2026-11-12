using UnrealBuildTool;
using System.Collections.Generic;

public class KoSekaiEditorTarget : TargetRules
{
	public KoSekaiEditorTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Editor;
		DefaultBuildSettings = BuildSettingsVersion.V7;
		IncludeOrderVersion = EngineIncludeOrderVersion.Unreal5_8;
		ExtraModuleNames.Add("KoSekai");
	}
}
