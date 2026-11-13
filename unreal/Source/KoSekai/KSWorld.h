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
	UMaterialInstanceDynamic* Beam(const FLinearColor& Color, float Glow, UStaticMesh* Shape = nullptr);      // Shape: the engine cone unless given (a laser is a thin box)
	/** A Meshy set piece, /Game/KS/Props/<Name>/SM_<Name> (art/arena/gen_props.py), stood with the middle of its
	    underside on Base and turned by Yaw. Size is the box it is fitted to: X its depth, Y its width, Z its height,
	    before the turn. A side given as zero follows the others in proportion. Null if the model is not in the game. */
	AActor* Prop(const TCHAR* Name, const FVector& Base, const FVector& Size, float Yaw = 0.f);
	int32 Props = 0, PropsMissing = 0;
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
		// The lamp's housing and its glowing lens turn with the beam (KSWorld::AimLamp). Centre: the middle of the
		// housing's model, already scaled; Fix turns the model so that its lens looks along +X.
		TWeakObjectPtr<AActor> Body, Lens;
		FQuat BodyFix = FQuat::Identity; FVector BodyCentre = FVector::ZeroVector; float LensOut = 0.f;
	};
	TArray<FBeam> Beams;                                  // stage lamps sweeping over the stands
	TArray<TWeakObjectPtr<AActor>> Lasers;                // thin glowing rods fanned out from LaserFrom
	FVector LaserFrom = FVector::ZeroVector;
	TArray<TWeakObjectPtr<AActor>> Ropes;                 // the ring's ropes, so the select screen can take them down
	// The painted wall behind the crowd and the lights of the place itself (lightning, lava), for AKSGameMode::TickArenaFx.
	TArray<TObjectPtr<UMaterialInstanceDynamic>> Wall;
	float WallGlow = 0.55f;
	TArray<TWeakObjectPtr<APointLight>> PulseLights;
	static constexpr float LaserLength = 2600.f;
	int32 Props = 0, PropsMissing = 0;                    // Meshy set pieces standing here, and those whose model was not found
};

/** The shape of the stands, shared by KSWorld::BuildArena (which builds them) and AKSGameMode::SeatCrowd (which fills them). */
namespace KSStands
{
	constexpr float Rise = 55.f;          // each row stands this much higher than the one in front of it
	constexpr float Depth = 118.f;        // from one row to the next
	constexpr float EndFront = 465.f;     // the front row behind each end of the ring (|X|); the row behind is Depth further
	constexpr float SideFront = 470.f;    // the front row along each side (|Y|)
	constexpr float Aisle = 185.f;        // the near end is open in the middle, |Y| below this
	constexpr float TowerTop = 84.f;      // the round podiums at the back corners: how far their tops are above the top platform
	constexpr float TowerX = 925.f, TowerY = 700.f;
	/** The corner blocks: cell (i, k) counts steps away from the ring along X and along Y. */
	inline float Corner(int32 i, int32 k) { return Rise * (1 + FMath::Min(i, k)); }
}

namespace KSWorld
{
	/** bDead builds the street after the ending: the cabinet is switched off. */
	void BuildStreet(FKSBuilder& B, FKSStreet& Street, bool bDead);
	void BuildArena(FKSBuilder& B, FKSArena& Arena, const FKSArenaDef& Def);
	/** Stretches a beam cone from the lamp at From towards Aim. */
	void AimBeam(UMaterialInstanceDynamic* Cone, const FVector& From, const FVector& Aim, float Length, float Radius);
	/** Turns a lamp's housing and lens towards Aim. */
	void AimLamp(const FKSArena::FBeam& Beam, const FVector& From, const FVector& Aim);
}
