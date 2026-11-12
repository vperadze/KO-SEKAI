#include "KSPawn.h"
#include "Camera/CameraComponent.h"

AKSPawn::AKSPawn()
{
	Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("Camera"));
	SetRootComponent(Camera);
	Camera->bUsePawnControlRotation = false;
	// Keep the night dark: the default auto exposure would brighten it to mid grey.
	Camera->PostProcessSettings.bOverride_AutoExposureMinBrightness = true;
	Camera->PostProcessSettings.bOverride_AutoExposureMaxBrightness = true;
	Camera->PostProcessSettings.AutoExposureMinBrightness = 3.0f;
	Camera->PostProcessSettings.AutoExposureMaxBrightness = 3.0f;
	Camera->PostProcessSettings.bOverride_BloomIntensity = true;
	Camera->PostProcessSettings.BloomIntensity = 0.9f;
	Camera->PostProcessSettings.bOverride_MotionBlurAmount = true;
	Camera->PostProcessSettings.MotionBlurAmount = 0.f;
	Camera->PostProcessSettings.bOverride_VignetteIntensity = true;
	Camera->PostProcessSettings.VignetteIntensity = 0.7f;
	bUseControllerRotationPitch = bUseControllerRotationYaw = bUseControllerRotationRoll = false;
}

void AKSPawn::SetView(const FVector& Location, const FRotator& Rotation, float FieldOfView)
{
	SetActorLocationAndRotation(Location, Rotation);
	Camera->SetFieldOfView(FieldOfView);
}
