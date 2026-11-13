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
		// The HUD draws its text with two imported fonts (KSHUD).
		PrivateDependencyModuleNames.AddRange(new string[] { "SlateCore" });
		// The microphone (KSMic): a shout sets off a special ability.
		PrivateDependencyModuleNames.AddRange(new string[] { "AudioCaptureCore" });
		// The particle effects of the special abilities (KSVfx): Niagara systems from the Fab packs, docs/VFX.md.
		PrivateDependencyModuleNames.AddRange(new string[] { "Niagara" });
		// Its back end is loaded by name in KSMic.cpp; named here so a packaged build carries it.
		DynamicallyLoadedModuleNames.Add(Target.Platform == UnrealTargetPlatform.Win64 ? "AudioCaptureWasapi" : "AudioCaptureRtAudio");
	}
}
