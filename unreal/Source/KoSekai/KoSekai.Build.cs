using UnrealBuildTool;

public class KoSekai : ModuleRules
{
	public KoSekai(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "InputCore" });
		// HTTPServer + Json: receive pose data from the webcam tracker (tracking-test/unreal-bridge.html).
		PrivateDependencyModuleNames.AddRange(new string[] { "HTTPServer", "HTTP", "Json" });
	}
}
