// Builds the two places in the game out of engine cubes, lights and text: the night street with the last
// cabinet, and the arena inside it. Everything is spawned from code, so there are no level files to merge.
#pragma once

#include "CoreMinimal.h"
#include "KSArenaData.h"

class UStaticMesh;
class UMaterialInterface;
class UMaterialInstanceDynamic;
class UTextureCube;
class APointLight;
class ASpotLight;
class ATextRenderActor;
class AKSDummy;

struct FKSBuilder
{
	UWorld* World = nullptr;
	TArray<TObjectPtr<AActor>>* Out = nullptr;
	UStaticMesh* Cube = nullptr;
	UStaticMesh* Sphere = nullptr;
	UStaticMesh* Cylinder = nullptr;
	UMaterialInterface* Solid = nullptr;
	UTextureCube* SkyCube = nullptr;

	UMaterialInstanceDynamic* Shape(UStaticMesh* Mesh, const FVector& Center, const FVector& Size, const FLinearColor& Color, float Glow = 0.f, const FRotator& Rot = FRotator::ZeroRotator, bool bShadow = false);
	UMaterialInstanceDynamic* Box(const FVector& Center, const FVector& Size, const FLinearColor& Color, float Glow = 0.f, const FRotator& Rot = FRotator::ZeroRotator, bool bShadow = false)
	{
		return Shape(Cube, Center, Size, Color, Glow, Rot, bShadow);
	}
	/** A flat picture: Across and Down are the world directions of the picture's width and height. UV = (scale U, scale V, offset U, offset V). */
	UMaterialInstanceDynamic* Image(const TCHAR* Texture, bool bLit, const FVector& Center, const FVector& Across, const FVector& Down, float Width, float Height, const FVector4& UV = FVector4(1, 1, 0, 0), float Glow = 1.f, const TCHAR* Material = nullptr);
	APointLight* Light(const FVector& Pos, const FLinearColor& Color, float Candelas, float Radius, bool bShadows = false);
	/** A stage lamp: a cone of light that shows as a beam in the arena's haze. Beam is how strongly the haze lights up in it. */
	ASpotLight* Spot(const FVector& Pos, const FVector& Aim, const FLinearColor& Color, float Candelas, float ConeDegrees, float Radius, float Beam);
	/** The visible body of a lamp's beam: a see-through glowing cone (M_KS_Beam). Place it with KSWorld::AimBeam. */
	UMaterialInstanceDynamic* Beam(const FLinearColor& Color, float Glow);
	ATextRenderActor* Text(const FVector& Pos, const FRotator& Rot, const FString& String, const FLinearColor& Color, float Size);
	/** Haze > 0 turns the fog into haze that lamps light up (volumetric fog), denser near the floor. */
	void Ambient(const FLinearColor& Color, float Intensity, const FLinearColor& FogColor, float FogDensity, float Haze = 0.f);
};

struct FKSStreet
{
	static constexpr float CabinetX = 3300.f;
	FVector ScreenPos = FVector(CabinetX - 42.f, 0.f, 128.f);
	TObjectPtr<UMaterialInstanceDynamic> Screen;
	TObjectPtr<UMaterialInstanceDynamic> Eye;
	TObjectPtr<APointLight> ScreenLight;
	TObjectPtr<APointLight> EyeLight;
	TObjectPtr<ATextRenderActor> ScreenText;
};

struct FKSArena
{
	TObjectPtr<ATextRenderActor> NameSign;
	TArray<TObjectPtr<AKSDummy>> Crowd;
	// The light show, moved every frame by AKSGameMode::TickArenaFx.
	struct FBeam
	{
		TWeakObjectPtr<ASpotLight> Lamp; FVector Aim = FVector::ZeroVector; float Candelas = 0.f;
		TObjectPtr<UMaterialInstanceDynamic> Cone;        // the beam you can see; its actor is the material's outer
		float Length = 800.f, Radius = 120.f, Glow = 0.35f;
	};
	TArray<FBeam> Beams;                                  // stage lamps sweeping over the stands
	TArray<TWeakObjectPtr<AActor>> Lasers;                // thin glowing rods fanned out from LaserFrom
	FVector LaserFrom = FVector::ZeroVector;
	// The painted wall behind the crowd and the lights of the place itself (lightning, lava), for AKSGameMode::TickArenaFx.
	TArray<TObjectPtr<UMaterialInstanceDynamic>> Wall;
	float WallGlow = 0.55f;
	TArray<TWeakObjectPtr<APointLight>> PulseLights;
	static constexpr float LaserLength = 2600.f;
};

namespace KSWorld
{
	/** bDead builds the street after the ending: the cabinet is switched off. */
	void BuildStreet(FKSBuilder& B, FKSStreet& Street, bool bDead);
	void BuildArena(FKSBuilder& B, FKSArena& Arena, const FKSArenaDef& Def);
	/** Stretches a beam cone from the lamp at From towards Aim. */
	void AimBeam(UMaterialInstanceDynamic* Cone, const FVector& From, const FVector& Aim, float Length, float Radius);
}
