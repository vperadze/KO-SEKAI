using UnrealBuildTool;

public class KoSekai : ModuleRules
{
	public KoSekai(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "InputCore" });
		// HTTPServer + Json: receive pose data from the webcam tracker (tracking-test/unreal-bridge.html).
		PrivateDependencyModuleNames.AddRange(new string[] { "HTTPServer", "HTTP", "Json" });
		// The built-in webcam tracker (KSCameraTracker): OpenCV reads the camera, NNE runs the pose model.
		PrivateDependencyModuleNames.AddRange(new string[] { "NNE", "OpenCV", "OpenCVHelper" });
		// Online play (KSNet): one TCP connection to the other player; the lobby's address box can paste from the clipboard.
		PrivateDependencyModuleNames.AddRange(new string[] { "Sockets", "ApplicationCore" });
		// The HUD draws its text with two imported fonts (KSHUD).
		PrivateDependencyModuleNames.AddRange(new string[] { "SlateCore" });
	}
}
