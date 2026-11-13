#include "KSWorld.h"
#include "KSDummy.h"
#include "KSTypes.h"
#include "Engine/World.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/StaticMesh.h"
#include "Engine/PointLight.h"
#include "Engine/SpotLight.h"
#include "Components/SpotLightComponent.h"
#include "Engine/SkyLight.h"
#include "Engine/TextRenderActor.h"
#include "Engine/ExponentialHeightFog.h"
#include "Engine/TextureCube.h"
#include "Components/StaticMeshComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/SkyLightComponent.h"
#include "Components/TextRenderComponent.h"
#include "Components/ExponentialHeightFogComponent.h"
#include "Materials/MaterialInstanceDynamic.h"

/** For an asset cached in a static: tells the garbage collector it is in use for as long as the game runs. */
static UStaticMesh* Keep(UStaticMesh* Mesh) { if (Mesh) Mesh->AddToRoot(); return Mesh; }

UMaterialInstanceDynamic* FKSBuilder::Shape(UStaticMesh* Mesh, const FVector& Center, const FVector& Size, const FLinearColor& Color, float Glow, const FRotator& Rot, bool bShadow)
{
	AStaticMeshActor* Actor = World->SpawnActor<AStaticMeshActor>(Center, Rot);
	if (!Actor) return nullptr;
	UStaticMeshComponent* Comp = Actor->GetStaticMeshComponent();
	Comp->SetMobility(EComponentMobility::Movable);
	Comp->SetStaticMesh(Mesh);
	Comp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Comp->SetCastShadow(bShadow);
	Actor->SetActorScale3D(Size / 100.f);   // engine shapes are 100 cm across
	UMaterialInstanceDynamic* MID = Solid ? UMaterialInstanceDynamic::Create(Solid, Actor) : nullptr;
	if (MID)
	{
		MID->SetVectorParameterValue(TEXT("Color"), Color);
		MID->SetScalarParameterValue(TEXT("Glow"), Glow);
		Comp->SetMaterial(0, MID);
	}
	Out->Add(Actor);
	return MID;
}

UMaterialInstanceDynamic* FKSBuilder::Image(const TCHAR* Texture, bool bLit, const FVector& Center, const FVector& Across, const FVector& Down, float Width, float Height, const FVector4& UV, float Glow, const TCHAR* Material)
{
	// Kept for the whole run (AddToRoot): a pointer held in a static is invisible to the garbage collector, so on the
	// street, where nothing uses the mesh, it was freed and the second walk into the arena crashed in SetStaticMesh.
	static UStaticMesh* Plane = Keep(LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Plane.Plane")));
	UMaterialInterface* Base = Material ? LoadObject<UMaterialInterface>(nullptr, Material) : LoadObject<UMaterialInterface>(nullptr, bLit ? TEXT("/Game/KS/Arena/M_KS_ImageLit.M_KS_ImageLit") : TEXT("/Game/KS/Arena/M_KS_Image.M_KS_Image"));
	UTexture* Tex = LoadObject<UTexture>(nullptr, Texture);
	if (!Plane || !Base || !Tex) return nullptr;
	AStaticMeshActor* Actor = World->SpawnActor<AStaticMeshActor>(Center, FRotationMatrix::MakeFromXY(Across, Down).Rotator());
	if (!Actor) return nullptr;
	UStaticMeshComponent* Comp = Actor->GetStaticMeshComponent();
	Comp->SetMobility(EComponentMobility::Movable);
	Comp->SetStaticMesh(Plane);
	Comp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Comp->SetCastShadow(false);
	Actor->SetActorScale3D(FVector(Width / 100.f, Height / 100.f, 1.f));
	UMaterialInstanceDynamic* MID = UMaterialInstanceDynamic::Create(Base, Actor);
	MID->SetTextureParameterValue(TEXT("Tex"), Tex);
	MID->SetVectorParameterValue(TEXT("UV"), FLinearColor(UV.X, UV.Y, 0, 0));
	MID->SetVectorParameterValue(TEXT("Offset"), FLinearColor(UV.Z, UV.W, 0, 0));
	MID->SetScalarParameterValue(TEXT("Glow"), Glow);
	Comp->SetMaterial(0, MID);
	Out->Add(Actor);
	return MID;
}

APointLight* FKSBuilder::Light(const FVector& Pos, const FLinearColor& Color, float Candelas, float Radius, bool bShadows)
{
	APointLight* Actor = World->SpawnActor<APointLight>(Pos, FRotator::ZeroRotator);
	if (!Actor) return nullptr;
	UPointLightComponent* Comp = Actor->PointLightComponent;
	Comp->SetMobility(EComponentMobility::Movable);
	Comp->SetIntensityUnits(ELightUnits::Candelas);
	Comp->SetIntensity(Candelas * 0.22f);   // global trim, tuned by eye against the fixed camera exposure
	Comp->SetLightColor(Color);
	Comp->SetAttenuationRadius(Radius);
	Comp->SetCastShadows(bShadows);
	Out->Add(Actor);
	return Actor;
}

ASpotLight* FKSBuilder::Spot(const FVector& Pos, const FVector& Aim, const FLinearColor& Color, float Candelas, float ConeDegrees, float Radius, float Beam)
{
	ASpotLight* Actor = World->SpawnActor<ASpotLight>(Pos, FRotator::ZeroRotator);
	if (!Actor) return nullptr;
	USpotLightComponent* Comp = Actor->SpotLightComponent;
	Comp->SetMobility(EComponentMobility::Movable);
	Comp->SetWorldRotation((Aim - Pos).Rotation());   // the component, not the actor: a spot light actor carries its lamp tilted 90 degrees down
	Comp->SetIntensityUnits(ELightUnits::Candelas);
	Comp->SetIntensity(Candelas * 0.22f);   // the same trim as Light()
	Comp->SetLightColor(Color);
	Comp->SetAttenuationRadius(Radius);
	Comp->SetOuterConeAngle(ConeDegrees);
	Comp->SetInnerConeAngle(ConeDegrees * 0.6f);
	Comp->SetCastShadows(false);
	Comp->SetVolumetricScatteringIntensity(Beam);
	Out->Add(Actor);
	return Actor;
}

UMaterialInstanceDynamic* FKSBuilder::Beam(const FLinearColor& Color, float Glow, UStaticMesh* Shape)
{
	static UStaticMesh* EngineCone = Keep(LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cone.Cone")));
	UStaticMesh* Cone = Shape ? Shape : EngineCone;
	UMaterialInterface* Base = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/KS/Arena/M_KS_Beam.M_KS_Beam"));
	if (!Cone || !Base) return nullptr;
	AStaticMeshActor* Actor = World->SpawnActor<AStaticMeshActor>(FVector::ZeroVector, FRotator::ZeroRotator);
	if (!Actor) return nullptr;
	UStaticMeshComponent* Comp = Actor->GetStaticMeshComponent();
	Comp->SetMobility(EComponentMobility::Movable);
	Comp->SetStaticMesh(Cone);
	Comp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Comp->SetCastShadow(false);
	UMaterialInstanceDynamic* MID = UMaterialInstanceDynamic::Create(Base, Actor);
	MID->SetVectorParameterValue(TEXT("Color"), Color);
	MID->SetScalarParameterValue(TEXT("Glow"), Glow);
	Comp->SetMaterial(0, MID);
	Out->Add(Actor);
	return MID;
}

void KSWorld::AimBeam(UMaterialInstanceDynamic* Cone, const FVector& From, const FVector& Aim, float Length, float Radius)
{
	AActor* Actor = Cone ? Cast<AActor>(Cone->GetOuter()) : nullptr;
	if (!Actor) return;
	// The engine's cone is 100 across and 100 tall with its point up (+Z): the point goes to the lamp.
	const FVector Dir = (Aim - From).GetSafeNormal();
	Actor->SetActorTransform(FTransform(FRotationMatrix::MakeFromZ(-Dir).Rotator(), From + Dir * Length * 0.5f, FVector(Radius / 50.f, Radius / 50.f, Length / 100.f)));
	Cone->SetVectorParameterValue(TEXT("Tip"), FLinearColor(From.X, From.Y, From.Z, 0.f));
	// The material shades the beam from its own axis: bright down the middle, nothing at the edge (make_beam_material.py).
	Cone->SetVectorParameterValue(TEXT("Axis"), FLinearColor(Dir.X, Dir.Y, Dir.Z, 0.f));
	Cone->SetScalarParameterValue(TEXT("Length"), Length);
}

// How a Meshy model has to be turned to stand the way Prop() promises: its depth along X with its front towards -X,
// its width along Y. Meshy models face the viewer of their thumbnail, which Unreal's import makes +Y: a quarter turn
// for all of them. The stage lamp is not stood but aimed: its lens has to look along +X.
static FQuat PropFix(const TCHAR* Name)
{
	return FRotator(0.f, FCString::Stricmp(Name, TEXT("Lamp")) ? 90.f : -90.f, 0.f).Quaternion();
}

static UStaticMesh* PropMesh(const TCHAR* Name)
{
	return LoadObject<UStaticMesh>(nullptr, *FString::Printf(TEXT("/Game/KS/Props/%s/SM_%s.SM_%s"), Name, Name, Name), nullptr, LOAD_NoWarn | LOAD_Quiet);
}

AActor* FKSBuilder::Prop(const TCHAR* Name, const FVector& Base, const FVector& Size, float Yaw)
{
	UStaticMesh* Mesh = PropMesh(Name);
	if (!Mesh) { PropsMissing++; return nullptr; }
	const FQuat Fix = PropFix(Name);
	const FBox Turned = Mesh->GetBoundingBox().TransformBy(FTransform(Fix));
	const FVector Extent = Turned.GetSize().ComponentMax(FVector(1.f));
	float Follow = BIG_NUMBER;
	for (int32 i = 0; i < 3; i++) if (Size[i] > 0.f) Follow = FMath::Min(Follow, (float)(Size[i] / Extent[i]));
	if (Follow == BIG_NUMBER) Follow = 1.f;
	FVector Scale;
	for (int32 i = 0; i < 3; i++) Scale[i] = Size[i] > 0.f ? Size[i] / Extent[i] : Follow;
	const FQuat Turn = FRotator(0.f, Yaw, 0.f).Quaternion();
	const FVector Foot(Turned.GetCenter().X, Turned.GetCenter().Y, Turned.Min.Z);
	AStaticMeshActor* Actor = World->SpawnActor<AStaticMeshActor>(Base - Turn.RotateVector(Foot * Scale), (Turn * Fix).Rotator());
	if (!Actor) return nullptr;
	UStaticMeshComponent* Comp = Actor->GetStaticMeshComponent();
	Comp->SetMobility(EComponentMobility::Movable);
	Comp->SetStaticMesh(Mesh);
	Comp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Comp->SetCastShadow(false);
	// The scale is asked for in the turned frame and set in the model's own: the turns are quarter turns, so it is the same numbers in another order.
	Actor->SetActorScale3D(Fix.UnrotateVector(Scale).GetAbs());
	Out->Add(Actor);
	Props++;
	return Actor;
}

void KSWorld::AimLamp(const FKSArena::FBeam& Beam, const FVector& From, const FVector& Aim)
{
	const FVector Dir = (Aim - From).GetSafeNormal();
	if (AActor* Body = Beam.Body.Get())
	{
		const FQuat Rot = Dir.ToOrientationQuat() * Beam.BodyFix;
		Body->SetActorLocationAndRotation(From - Rot.RotateVector(Beam.BodyCentre), Rot);
	}
	if (AActor* Lens = Beam.Lens.Get()) Lens->SetActorLocation(From + Dir * Beam.LensOut);
}

ATextRenderActor* FKSBuilder::Text(const FVector& Pos, const FRotator& Rot, const FString& String, const FLinearColor& Color, float Size)
{
	ATextRenderActor* Actor = World->SpawnActor<ATextRenderActor>(Pos, Rot);
	if (!Actor) return nullptr;
	UTextRenderComponent* Comp = Actor->GetTextRender();
	Comp->SetMobility(EComponentMobility::Movable);
	Comp->SetText(FText::FromString(String));
	Comp->SetTextRenderColor(Color.ToFColor(true));
	Comp->SetWorldSize(Size);
	Comp->SetHorizontalAlignment(EHTA_Center);
	Comp->SetVerticalAlignment(EVRTA_TextCenter);
	Out->Add(Actor);
	return Actor;
}

void FKSBuilder::Ambient(const FLinearColor& Color, float Intensity, const FLinearColor& FogColor, float FogDensity, float Haze)
{
	if (ASkyLight* Sky = World->SpawnActor<ASkyLight>(FVector(0, 0, 500), FRotator::ZeroRotator))
	{
		USkyLightComponent* Comp = Sky->GetLightComponent();
		Comp->SetMobility(EComponentMobility::Movable);
		Comp->SourceType = SLS_SpecifiedCubemap;
		Comp->SetCubemap(SkyCube);
		Comp->SetLightColor(Color);
		Comp->SetIntensity(Intensity);
		Comp->RecaptureSky();
		Out->Add(Sky);
	}
	if (AExponentialHeightFog* Fog = World->SpawnActor<AExponentialHeightFog>(FVector(0, 0, 0), FRotator::ZeroRotator))
	{
		UExponentialHeightFogComponent* Comp = Fog->GetComponent();
		Comp->SetMobility(EComponentMobility::Movable);
		Comp->SetFogDensity(FogDensity);
		Comp->SetFogHeightFalloff(Haze > 0.f ? 0.07f : 0.05f);
		Comp->SetFogInscatteringColor(FogColor);
		if (Haze > 0.f)
		{
			// Smoke-machine haze: it has no light of its own, the lamps that shine through it show as beams.
			Comp->SetVolumetricFog(true);
			Comp->SetVolumetricFogExtinctionScale(Haze);
			Comp->SetVolumetricFogAlbedo(FColor(70, 60, 95));
			Comp->SetVolumetricFogScatteringDistribution(0.35f);
			Comp->SetVolumetricFogDistance(5000.f);
		}
		Out->Add(Fog);
	}
}

void KSWorld::BuildStreet(FKSBuilder& B, FKSStreet& Street, bool bDead)
{
	using namespace KSColors;
	FRandomStream Rng(1987);
	const FLinearColor Asphalt(0.012f, 0.012f, 0.016f), Kerb(0.035f, 0.035f, 0.045f), Wall(0.022f, 0.018f, 0.03f);
	const float X0 = -500.f, X1 = FKSStreet::CabinetX + 260.f, Len = X1 - X0, Mid = (X0 + X1) * 0.5f;

	B.Ambient(FLinearColor(0.35f, 0.4f, 0.8f), 0.5f, FLinearColor(0.02f, 0.012f, 0.05f), 0.02f);
	B.Box(FVector(Mid, 0, -10), FVector(Len, 520, 20), Asphalt);
	for (float Side : { -1.f, 1.f })
	{
		B.Box(FVector(Mid, Side * 350, -4), FVector(Len, 180, 32), Kerb);
		// Buildings, with a few lit windows.
		for (float X = X0; X < X1; )
		{
			const float W = Rng.FRandRange(380, 620), H = Rng.FRandRange(700, 1700);
			B.Box(FVector(X + W * 0.5f, Side * 690, H * 0.5f), FVector(W - 12, 500, H), Wall * Rng.FRandRange(0.7f, 1.5f));
			for (int32 i = 0; i < 3; i++)
			{
				const FLinearColor Lit = Rng.FRand() < 0.5f ? FLinearColor(1.0f, 0.62f, 0.22f) : FLinearColor(0.35f, 0.6f, 1.0f);
				B.Box(FVector(X + Rng.FRandRange(60, W - 60), Side * 439, Rng.FRandRange(260, H - 80)), FVector(46, 4, 64), Lit, Rng.FRandRange(0.6f, 2.5f));
			}
			X += W;
		}
	}
	// Dashed centre line.
	for (float X = X0 + 100; X < X1 - 500; X += 320) B.Box(FVector(X, 0, 0.5f), FVector(140, 10, 1), FLinearColor(0.25f, 0.25f, 0.2f));

	// Street lamps and neon signs, alternating sides.
	int32 Index = 0;
	for (float X = 100; X < FKSStreet::CabinetX - 300; X += 620, Index++)
	{
		const float Side = Index % 2 ? 1.f : -1.f;
		B.Shape(B.Cylinder, FVector(X, Side * 275, 210), FVector(9, 9, 420), Kerb * 2.f);
		B.Box(FVector(X, Side * 250, 418), FVector(22, 60, 8), FLinearColor(1.0f, 0.8f, 0.5f), 6.f);
		B.Light(FVector(X, Side * 235, 390), FLinearColor(1.0f, 0.72f, 0.42f), 420.f, 900.f);

		const FLinearColor Neon = Index % 2 ? Magenta : Cyan;
		const float SignX = X + 300;
		B.Box(FVector(SignX, -Side * 436, 300), FVector(210, 6, 46), Neon, 9.f);
		B.Box(FVector(SignX + 60, -Side * 436, 242), FVector(90, 6, 12), Index % 2 ? Cyan : Yellow, 9.f);
		B.Light(FVector(SignX, -Side * 380, 290), Neon, 160.f, 700.f);
	}

	// The dead end: a shuttered arcade with one cabinet left in the doorway.
	const float CX = FKSStreet::CabinetX;
	B.Box(FVector(X1 + 100, 0, 700), FVector(200, 1400, 1400), Wall);
	B.Box(FVector(CX + 120, 0, 330), FVector(40, 520, 60), FLinearColor(0.05f, 0.02f, 0.06f));
	B.Text(FVector(CX + 98, 0, 330), FRotator(0, 180, 0), TEXT("A R C A D E"), bDead ? Muted * 0.5f : Magenta, 44.f);
	for (float Y = -230; Y <= 230; Y += 46) if (FMath::Abs(Y) > 70) B.Box(FVector(CX + 110, Y, 150), FVector(6, 40, 300), FLinearColor(0.03f, 0.03f, 0.035f));

	const FLinearColor Shell(0.03f, 0.01f, 0.05f);
	B.Box(FVector(CX, 0, 95), FVector(84, 72, 190), Shell, 0.f, FRotator::ZeroRotator, true);
	B.Box(FVector(CX - 40, 0, 181), FVector(14, 72, 26), bDead ? Shell : Magenta, bDead ? 0.f : 5.f);
	B.Text(FVector(CX - 48.5f, 0, 181), FRotator(0, 180, 0), TEXT("KO-SEKAI"), bDead ? Muted * 0.4f : Yellow, 15.f);
	B.Box(FVector(CX - 52, 0, 90), FVector(34, 72, 9), Shell * 2.f, 0.f, FRotator(-12, 0, 0));
	B.Shape(B.Sphere, FVector(CX - 58, -14, 99), FVector(7), Red, bDead ? 0.f : 1.5f);
	B.Shape(B.Sphere, FVector(CX - 58, 10, 97), FVector(5), Cyan, bDead ? 0.f : 1.5f);
	B.Shape(B.Sphere, FVector(CX - 58, 20, 97), FVector(5), Magenta, bDead ? 0.f : 1.5f);
	B.Box(FVector(CX - 43, 0, 52), FVector(4, 8, 12), Yellow, bDead ? 0.f : 3.f);

	Street.Screen = B.Box(Street.ScreenPos, FVector(4, 56, 44), bDead ? FLinearColor(0.01f, 0.01f, 0.012f) : Cyan, bDead ? 0.f : 4.f);
	Street.ScreenText = B.Text(Street.ScreenPos + FVector(-2.5f, 0, 0), FRotator(0, 180, 0), bDead ? TEXT("") : TEXT("INSERT\nYOURSELF"), FLinearColor(0.02f, 0.0f, 0.06f), 8.f);
	// The camera above the screen.
	B.Box(FVector(CX - 44, 0, 160), FVector(8, 14, 9), Shell * 3.f);
	Street.Eye = B.Shape(B.Sphere, FVector(CX - 49, 0, 160), FVector(6), FLinearColor(0.05f, 0.0f, 0.0f), 0.f);
	if (!bDead)
	{
		Street.ScreenLight = B.Light(Street.ScreenPos + FVector(-70, 0, 0), Cyan, 260.f, 800.f, true);
		Street.EyeLight = B.Light(FVector(CX - 62, 0, 160), Red, 0.f, 500.f);
	}
	else
	{
		B.Light(FVector(CX - 260, 120, 320), FLinearColor(1.0f, 0.72f, 0.42f), 260.f, 900.f, true);
	}
}

void KSWorld::BuildArena(FKSBuilder& B, FKSArena& Arena, const FKSArenaDef& Def)
{
	// Every arena is this one hall in other colours: the arena's three neon colours stand in for the hall's cyan,
	// magenta and yellow in everything below, and its own pictures go on the canvas and on the wall.
	const bool bHall = !Def.Key[0];
	const FLinearColor Cyan = Def.A, Magenta = Def.B, Yellow = Def.C, Green = bHall ? KSColors::Green : Def.C;
	const FString MatPath = bHall ? FString(TEXT("/Game/KS/Arena/T_KS_Mat.T_KS_Mat")) : FString::Printf(TEXT("/Game/KS/Arena/T_KS_Mat_%s.T_KS_Mat_%s"), Def.Key, Def.Key);
	const FString WallPath = bHall ? FString(TEXT("/Game/KS/Arena/T_KS_Stands.T_KS_Stands")) : FString::Printf(TEXT("/Game/KS/Arena/T_KS_Bg_%s.T_KS_Bg_%s"), Def.Key, Def.Key);
	B.Ambient(Def.Ambient, 0.8f, Def.Fog, Def.FogDensity, 1.f);
	B.Box(FVector(0, 0, -10), FVector(6000, 6000, 20), Def.Floor);
	// Neon floor grid, like the inside of a 1987 attract screen.
	for (float P = -2500; P <= 2500; P += 250)
	{
		B.Box(FVector(P, 0, 0.6f), FVector(3, 5000, 1.2f), Magenta, 3.f);
		B.Box(FVector(0, P, 0.6f), FVector(5000, 3, 1.2f), Cyan, 3.f);
	}
	// The ring the two fighters stand in.
	for (int32 i = 0; i < 4; i++)
	{
		const float A = i * 90.f, R = 330.f;
		const FVector Corner(FMath::Cos(FMath::DegreesToRadians(A + 45)) * R * 1.414f, FMath::Sin(FMath::DegreesToRadians(A + 45)) * R * 1.414f, 0);
		// A steel corner post with its turnbuckles (Meshy), and a lit cap in the arena's colour.
		if (B.Prop(TEXT("Post"), Corner, FVector(0, 0, 150), A + 45.f)) B.Shape(B.Sphere, Corner + FVector(0, 0, 153), FVector(17), Yellow, 6.f);
		else B.Shape(B.Cylinder, Corner + FVector(0, 0, 70), FVector(14, 14, 140), Yellow, 2.f);
		const FVector MidSide(FMath::Cos(FMath::DegreesToRadians(A)) * R, FMath::Sin(FMath::DegreesToRadians(A)) * R, 0);
		for (float Z : { 55.f, 95.f, 130.f })
		{
			if (UMaterialInstanceDynamic* Rope = B.Box(MidSide + FVector(0, 0, Z), i % 2 ? FVector(2 * R, 3, 3) : FVector(3, 2 * R, 3), i % 2 ? Cyan : Magenta, 4.f))
				Arena.Ropes.Add(Cast<AActor>(Rope->GetOuter()));
		}
	}
	// The ring canvas, and the stands full of people around the far side (both painted with Dreamina).
	B.Image(*MatPath, true, FVector(0, 0, 2.f), FVector(0, 1, 0), FVector(-1, 0, 0), 700.f, 700.f);
	{
		const int32 Panels = 16;
		const float Radius = 1500.f, From = -115.f, To = 115.f, Step = (To - From) / Panels;
		const float PanelWidth = 2.f * Radius * FMath::Tan(FMath::DegreesToRadians(Step * 0.5f)), WallHeight = 1300.f;
		for (int32 i = 0; i < Panels; i++)
		{
			const float A = FMath::DegreesToRadians(From + (i + 0.5f) * Step);
			const FVector Out(FMath::Cos(A), FMath::Sin(A), 0.f), Across(-FMath::Sin(A), FMath::Cos(A), 0.f);
			// The hall's picture is spread once round the whole wall. An arena's backdrop is a 21:9 painting, which is
			// the shape of the far wall alone (the middle half of the panels): it is shown whole there and runs on
			// mirrored along the two side walls, so nothing is stretched and there is no seam.
			FVector4 UV(1.f / Panels, 1.f, (float)i / Panels, 0.f);
			const int32 Q = Panels / 4;
			if (!bHall) UV = (i >= Q && i < Panels - Q) ? FVector4(2.f / Panels, 1.f, 2.f * (i - Q) / Panels, 0.f)
				: FVector4(-2.f / Panels, 1.f, 2.f * ((i < Q ? Q : Panels + Q) - i) / Panels, 0.f);
			Arena.WallGlow = bHall ? 0.55f : 0.8f;
			// A backdrop's horizon is a third of the way up the painting: sunk until that is near the floor, so the sky and
			// the landmarks stand above the crowd's heads and not out of the camera's sight.
			if (UMaterialInstanceDynamic* Panel = B.Image(*WallPath, false, Out * Radius + FVector(0, 0, WallHeight * 0.5f - (bHall ? 40.f : 330.f)), Across, FVector(0, 0, -1), PanelWidth + 2.f, WallHeight, UV, Arena.WallGlow))
				Arena.Wall.Add(Panel);
		}
	}
	// Tall bars on the horizon, the edge of the screen.
	for (int32 i = 0; i < 28; i++)
	{
		const float A = FMath::DegreesToRadians(i * 360.f / 28.f), H = 500.f + 420.f * FMath::Abs(FMath::Sin(i * 1.7f));
		B.Box(FVector(FMath::Cos(A) * 2300, FMath::Sin(A) * 2300, H * 0.5f), FVector(30, 30, H), i % 2 ? Magenta : Cyan, 2.5f, FRotator(0, FMath::RadiansToDegrees(A), 0));
	}
	// The ring's own lights must not turn the haze above the canvas into a white ball: they barely light it.
	auto Dry = [](APointLight* L, float Beam) { if (L) L->PointLightComponent->SetVolumetricScatteringIntensity(Beam); return L; };
	Dry(B.Light(FVector(40, 0, 520), FLinearColor(1.0f, 0.9f, 1.0f), 5200.f, 1600.f, true), 0.04f);
	Dry(B.Light(FVector(-420, -380, 260), Cyan, 900.f, 1500.f), 0.3f);
	Dry(B.Light(FVector(-420, 380, 260), Magenta, 900.f, 1500.f), 0.3f);
	Dry(B.Light(FVector(700, 0, 300), Magenta, 1600.f, 1800.f), 0.3f);

	// The stands (KSStands in KSWorld.h; AKSGameMode::SeatCrowd fills them): rows of stage risers (Meshy's Podium), each
	// row a step higher than the one in front and with a strip of neon along its edge, a wide top platform at the back
	// of each end and each side, a stepped block in every corner, and steel barriers between the front row and the ring.
	using namespace KSStands;
	const FLinearColor Step = bHall ? FLinearColor(0.022f, 0.014f, 0.045f) : FLinearColor(Def.Floor.R * 1.6f + 0.004f, Def.Floor.G * 1.6f + 0.004f, Def.Floor.B * 1.6f + 0.004f);
	// The riser's deck is this far up the model; the rest is the mesh panel at its back, which becomes the rail in
	// front of the next row's shins.
	const float Deck = 0.375f;
	// One step: the rectangle Lo..Hi with its top at Top. Yaw says where its back is (0: +X, 180: -X, 90: +Y, -90: -Y).
	auto Tier = [&](FVector2D Lo, FVector2D Hi, float Top, float Yaw, const FLinearColor& Edge)
	{
		const FVector2D Mid = (Lo + Hi) * 0.5f, Span = Hi - Lo;
		if (Top > Rise + 1.f) B.Box(FVector(Mid.X, Mid.Y, (Top - Rise) * 0.5f), FVector(Span.X, Span.Y, Top - Rise), Step);
		const bool bAlongY = FMath::Abs(FMath::Cos(FMath::DegreesToRadians(Yaw))) > 0.5f;      // the row runs along Y, its depth along X
		const float Long = bAlongY ? Span.Y : Span.X, Deep = bAlongY ? Span.X : Span.Y;
		const int32 Across = FMath::Max(1, FMath::RoundToInt(Long / 230.f)), Rows = FMath::Max(1, FMath::RoundToInt(Deep / 135.f));
		bool bModel = true;
		for (int32 r = 0; r < Rows && bModel; r++) for (int32 c = 0; c < Across && bModel; c++)
		{
			const float U = (c + 0.5f) / Across - 0.5f, V = (r + 0.5f) / Rows - 0.5f;
			const FVector Base(Mid.X + (bAlongY ? V * Span.X : U * Span.X), Mid.Y + (bAlongY ? U * Span.Y : V * Span.Y), Top - Rise);
			bModel = B.Prop(TEXT("Podium"), Base, FVector(Deep / Rows, Long / Across, Rise / Deck), Yaw) != nullptr;
		}
		if (!bModel) B.Box(FVector(Mid.X, Mid.Y, Top - Rise * 0.5f), FVector(Span.X, Span.Y, Rise), Step);
		// The neon along the front edge.
		const FVector2D Back(FMath::Cos(FMath::DegreesToRadians(Yaw)), FMath::Sin(FMath::DegreesToRadians(Yaw)));
		B.Box(FVector(Mid.X - Back.X * Span.X * 0.5f, Mid.Y - Back.Y * Span.Y * 0.5f, Top + 1.f), bAlongY ? FVector(5, Span.Y, 5) : FVector(Span.X, 5, 5), Edge, 5.f);
	};
	// A run of crowd barriers from P to Q, their fronts towards the ring.
	auto Fence = [&](FVector2D P, FVector2D Q, float Yaw)
	{
		const float Long = (Q - P).Size();
		const int32 Count = FMath::Max(1, FMath::RoundToInt(Long / 125.f));
		for (int32 c = 0; c < Count; c++)
		{
			const FVector2D At = FMath::Lerp(P, Q, (c + 0.5f) / Count);
			B.Prop(TEXT("Barrier"), FVector(At.X, At.Y, 0), FVector(26, Long / Count, 62), Yaw);
		}
	};
	const float E1 = EndFront + Depth * 0.5f, E2 = E1 + Depth, E3 = E2 + Depth, EBack = 1040.f;      // where each end's steps begin, and where the top platform ends
	const float S1 = SideFront + Depth * 0.5f, S2 = S1 + Depth, SBack = 915.f;
	const float EndHalf = 595.f, CornerFrom = 448.f, CornerSide = 603.f, Cell = 104.f;
	for (int32 End = -1; End <= 1; End += 2)                 // +1 behind the opponent, -1 behind the player
	{
		const float Yaw = End > 0 ? 0.f : 180.f;
		auto X = [End](float From, float To) { return FVector2D(End > 0 ? From : -To, End > 0 ? To : -From); };   // (lo, hi) of an X range on this end
		// The near end is open in the middle: the wide shot that opens a bout looks along that aisle.
		const float Halves[2][2] = { { -EndHalf, End > 0 ? EndHalf : -Aisle }, { Aisle, EndHalf } };
		for (int32 Half = 0; Half < (End > 0 ? 1 : 2); Half++)
		{
			const float Y0 = Halves[Half][0], Y1 = Halves[Half][1], Wide = (Y1 > 0 ? SBack : Y1) , WideLo = (Y0 < 0 ? -SBack : Y0);
			Tier(FVector2D(X(E1, E2).X, Y0), FVector2D(X(E1, E2).Y, Y1), Rise, Yaw, Cyan);
			Tier(FVector2D(X(E2, E3).X, Y0), FVector2D(X(E2, E3).Y, Y1), 2.f * Rise, Yaw, Magenta);
			Tier(FVector2D(X(E3, EBack).X, End > 0 ? -SBack : WideLo), FVector2D(X(E3, EBack).Y, End > 0 ? SBack : Wide), 3.f * Rise, Yaw, Yellow);
			Fence(FVector2D(End * 436.f, FMath::Max(Y0, -480.f)), FVector2D(End * 436.f, FMath::Min(Y1, 480.f)), Yaw);
		}
		for (int32 Side = -1; Side <= 1; Side += 2)
		{
			// A stepped block in each corner, rising towards the corner of the hall.
			for (int32 i = 0; i < 3; i++) for (int32 k = 0; k < 3; k++)
			{
				const FVector2D C(End * (CornerFrom + Cell * (i + 0.5f)), Side * (CornerSide + Cell * (k + 0.5f)));
				Tier(C - FVector2D(Cell * 0.5f), C + FVector2D(Cell * 0.5f), Corner(i, k), Yaw, (i + k) % 2 ? Cyan : Magenta);
			}
			// A round podium on each back corner of the top platform: the biggest of the crowd stand on them.
			B.Prop(TEXT("Tower"), FVector(End * TowerX, Side * TowerY, 3.f * Rise), FVector(230, 230, TowerTop), Yaw);
			B.Shape(B.Cylinder, FVector(End * TowerX, Side * TowerY, 3.f * Rise + TowerTop + 1.f), FVector(150, 150, 2), Yellow, 3.f);
		}
	}
	for (int32 Side = -1; Side <= 1; Side += 2)
	{
		const float Yaw = Side * 90.f;
		auto Y = [Side](float From, float To) { return FVector2D(Side > 0 ? From : -To, Side > 0 ? To : -From); };
		Tier(FVector2D(-375.f, Y(S1, S2).X), FVector2D(445.f, Y(S1, S2).Y), Rise, Yaw, Cyan);
		Tier(FVector2D(-375.f, Y(S2, SBack).X), FVector2D(445.f, Y(S2, SBack).Y), 2.f * Rise, Yaw, Yellow);
		Fence(FVector2D(-375.f, Side * 441.f), FVector2D(445.f, Side * 441.f), Yaw);
	}
	// Loudspeaker towers at the back of the near end, and in the plain hall at the far end too, where the other
	// arenas have their landmark.
	for (float Y : { -830.f, 830.f })
	{
		B.Prop(TEXT("Speakers"), FVector(-1130.f, Y, 0), FVector(0, 0, 640), 180.f);
		if (bHall) B.Prop(TEXT("Speakers"), FVector(1230.f, Y * 0.84f, 0), FVector(0, 0, 780), 0.f);
	}
	// The place's landmark (Meshy, one model per arena, named like the arena): a pair behind the far stands, or one
	// in the middle, tall enough to stand over the giants' heads, and lit from the front.
	static const struct { const TCHAR* Key; float Height; bool bPair; } Landmarks[] = {
		{ TEXT("Space"), 980.f, true }, { TEXT("Beach"), 660.f, true }, { TEXT("Bar"), 640.f, true }, { TEXT("Rooftop"), 900.f, true },
		{ TEXT("Dojo"), 860.f, false }, { TEXT("Castle"), 900.f, true }, { TEXT("Gym"), 580.f, true }, { TEXT("Scrapyard"), 820.f, true },
		{ TEXT("Volcano"), 860.f, true }, { TEXT("Studio"), 620.f, true } };
	for (const auto& L : Landmarks)
	{
		if (FCString::Stricmp(L.Key, Def.Key)) continue;
		for (float Y : { -690.f, 690.f })
		{
			const FVector At(L.bPair ? 1230.f : 1300.f, L.bPair ? Y : 0.f, 0.f);
			if (!B.Prop(L.Key, At, FVector(0, 0, L.Height), L.bPair ? (Y > 0 ? 25.f : -25.f) : 0.f)) break;
			if (APointLight* Lit = B.Light(At + FVector(-330.f, L.bPair ? -Y * 0.25f : 0.f, L.Height * 0.55f), FLinearColor(1.f, 0.93f, 0.85f), 1300.f, 1100.f))
				Lit->PointLightComponent->SetVolumetricScatteringIntensity(0.05f);
			if (!L.bPair) break;
		}
	}
	// Soft house lights low over the stands, so the steps and the shadows at the crowd's feet can be seen.
	Dry(B.Light(FVector(620, -380, 330), FLinearColor(0.55f, 0.45f, 1.f), 420.f, 1100.f), 0.15f);
	Dry(B.Light(FVector(620, 380, 330), FLinearColor(1.f, 0.45f, 0.8f), 420.f, 1100.f), 0.15f);
	Dry(B.Light(FVector(100, -640, 300), FLinearColor(0.45f, 0.7f, 1.f), 340.f, 1000.f), 0.15f);
	Dry(B.Light(FVector(100, 640, 300), FLinearColor(1.f, 0.5f, 0.75f), 340.f, 1000.f), 0.15f);
	Dry(B.Light(FVector(-620, -420, 330), FLinearColor(1.f, 0.45f, 0.8f), 420.f, 1100.f), 0.15f);
	Dry(B.Light(FVector(-620, 420, 330), FLinearColor(0.55f, 0.45f, 1.f), 420.f, 1100.f), 0.15f);

	// The lighting rig: four truss gantries (Meshy's kit) standing round the ring and meeting at its corners, lamps hung
	// under their beams, a scoreboard cube in the middle, and a wider gantry across the far end behind the stands that
	// carries the two big white lamps.
	const float RigZ = 640.f, RigHalf = 430.f;
	const FLinearColor Metal(0.02f, 0.02f, 0.03f);
	for (int32 i = 0; i < 4; i++)
	{
		const float A = FMath::DegreesToRadians(i * 90.f);
		const FVector Mid(FMath::Cos(A) * RigHalf + 60.f, FMath::Sin(A) * RigHalf, 0.f);
		if (!B.Prop(TEXT("Truss"), Mid, FVector(46, 2 * RigHalf + 46, RigZ + 24), i * 90.f))
		{
			B.Box(Mid + FVector(0, 0, RigZ), i % 2 ? FVector(2 * RigHalf + 24, 24, 24) : FVector(24, 2 * RigHalf + 24, 24), Metal);
			B.Box(Mid + FVector(0, 0, RigZ + 400), FVector(6, 6, 800), Metal);
		}
	}
	const float FarX = 1085.f, FarZ = 790.f;
	if (!B.Prop(TEXT("Truss"), FVector(FarX, 0, 0), FVector(60, 1640, FarZ + 30), 0.f)) B.Box(FVector(FarX, 0, FarZ), FVector(24, 1640, 24), Metal);
	if (B.Prop(TEXT("Scoreboard"), FVector(60, 0, 700), FVector(0, 0, 250), 45.f))
	{
		B.Box(FVector(60, 0, 950 + 300), FVector(8, 8, 600), Metal);
		// Lit from below by the ring, and by a lamp of its own so that its screens are not black on black.
		Dry(B.Light(FVector(60, 0, 640), Yellow, 700.f, 500.f), 0.02f);
	}
	const FLinearColor White(1.f, 0.95f, 0.9f);
	const struct { FVector Pos, Aim; FLinearColor Color; float Candelas; } Lamps[] = {
		{ FVector(60 + RigHalf, -300, RigZ - 52), FVector(700, -520, 120), Cyan,    9000.f },
		{ FVector(60 + RigHalf, -100, RigZ - 52), FVector(820, -150, 150), Magenta, 9000.f },
		{ FVector(60 + RigHalf,  100, RigZ - 52), FVector(820,  150, 150), Yellow,  8000.f },
		{ FVector(60 + RigHalf,  300, RigZ - 52), FVector(700,  520, 120), Cyan,    9000.f },
		{ FVector(100, -RigHalf, RigZ - 52),      FVector(120, -700, 100), Magenta, 8000.f },
		{ FVector(100,  RigHalf, RigZ - 52),      FVector(120,  700, 100), Cyan,    8000.f },
		// Two from high behind the stands, shining over the crowd's heads towards the ring: the shafts the crowd is seen against.
		{ FVector(FarX - 10, -520, FarZ - 50),    FVector(250, -120, 60),  White,   11000.f },
		{ FVector(FarX - 10,  520, FarZ - 50),    FVector(250,  120, 60),  White,   11000.f },
		// Six on the floor between the ropes and the front row, pointing up and out over the crowd: these are the
		// beams the fight camera sees, crossing in front of the stands.
		{ FVector(408, -340, 50),                 FVector(700, -520, 900), Magenta, 3000.f },
		{ FVector(408, -115, 50),                 FVector(720, -170, 900), Cyan,    3000.f },
		{ FVector(408,  115, 50),                 FVector(720,  170, 900), Yellow,  3000.f },
		{ FVector(408,  340, 50),                 FVector(700,  520, 900), Magenta, 3000.f },
		{ FVector(160, -408, 50),                 FVector(200, -720, 900), Cyan,    3000.f },
		{ FVector(160,  408, 50),                 FVector(200,  720, 900), Yellow,  3000.f },
	};
	for (const auto& L : Lamps)
	{
		const bool bFloor = L.Pos.Z < 100.f;                                          // the up-lights at ringside
		// The lamp itself is weak on surfaces and only tints the haze; the beam you see is the glowing cone.
		FKSArena::FBeam Beam; Beam.Lamp = B.Spot(L.Pos, L.Aim, L.Color, L.Candelas, 10.f, 2600.f, 25.f);
		Beam.Aim = L.Aim; Beam.Candelas = L.Candelas;
		// The housing (Meshy): a moving head standing on the floor, whose glowing lens alone follows the beam, or a
		// stage lamp hung under the truss that turns with it. Without the models: the old tin can.
		UStaticMesh* Can = bFloor ? nullptr : PropMesh(TEXT("Lamp"));
		if (bFloor && B.Prop(TEXT("MovingHead"), FVector(L.Pos.X, L.Pos.Y, 0), FVector(0, 0, 58), (L.Aim - L.Pos).Rotation().Yaw)) Beam.LensOut = 4.f;
		else if (Can)
		{
			const FBox Box = Can->GetBoundingBox();
			const float Scale = 62.f / FMath::Max(1.f, (float)Box.GetSize().GetMax());
			if (AStaticMeshActor* Body = B.World->SpawnActor<AStaticMeshActor>(L.Pos, FRotator::ZeroRotator))
			{
				Body->GetStaticMeshComponent()->SetMobility(EComponentMobility::Movable);
				Body->GetStaticMeshComponent()->SetStaticMesh(Can);
				Body->GetStaticMeshComponent()->SetCollisionEnabled(ECollisionEnabled::NoCollision);
				Body->GetStaticMeshComponent()->SetCastShadow(false);
				Body->SetActorScale3D(FVector(Scale));
				B.Out->Add(Body); B.Props++;
				Beam.Body = Body; Beam.BodyFix = PropFix(TEXT("Lamp")); Beam.BodyCentre = Box.GetCenter() * Scale; Beam.LensOut = 16.f;
			}
		}
		else
		{
			if (!bFloor) B.PropsMissing++;
			B.Shape(B.Cylinder, L.Pos, FVector(34, 34, 40), Metal, 0.f, (L.Aim - L.Pos).Rotation() + FRotator(90, 0, 0));
			Beam.LensOut = 22.f;
		}
		if (UMaterialInstanceDynamic* Lens = B.Shape(B.Sphere, L.Pos, FVector(Beam.Body.IsValid() ? 20.f : 26.f), L.Color, 30.f)) Beam.Lens = Cast<AActor>(Lens->GetOuter());      // the glowing lens
		KSWorld::AimLamp(Beam, L.Pos, L.Aim);
		Beam.Length = bFloor ? 950.f : (L.Aim - L.Pos).Size() * 1.05f; Beam.Radius = Beam.Length * (bFloor ? 0.09f : 0.07f); Beam.Glow = bFloor ? 1.1f : 0.4f;
		Beam.Cone = B.Beam(L.Color, Beam.Glow);
		KSWorld::AimBeam(Beam.Cone, L.Pos, L.Aim, Beam.Length, Beam.Radius);
		Arena.Beams.Add(Beam);
	}
	// A laser show from the floor in front of the far stands: thin rods of light fanned up and out over the crowd.
	Arena.LaserFrom = FVector(425, 0, 14);
	B.Box(Arena.LaserFrom, FVector(40, 70, 26), Metal);
	for (int32 i = 0; i < 8; i++)
	{
		// A laser is the same glowing air as a lamp's beam, on a thin box: soft-edged, shimmering in the haze. (They were
		// solid glowing rods.) AKSGameMode::TickArenaFx turns them and tells the material where they point.
		UMaterialInstanceDynamic* MID = B.Beam(i % 3 == 0 ? Green : i % 3 == 1 ? Magenta : Cyan, 9.f, B.Cube);
		AActor* Rod = MID ? Cast<AActor>(MID->GetOuter()) : nullptr;
		if (Rod) { Rod->SetActorScale3D(FVector(FKSArena::LaserLength, 5.f, 5.f) / 100.f); MID->SetScalarParameterValue(TEXT("Length"), FKSArena::LaserLength * 1.6f); MID->SetScalarParameterValue(TEXT("Soft"), 1.f); MID->SetScalarParameterValue(TEXT("Haze"), 0.7f); }
		Arena.Lasers.Add(Rod);
	}

	Arena.NameSign = B.Text(FVector(1100, 0, 520), FRotator(0, 180, 0), TEXT(""), Yellow, 150.f);
	B.Text(FVector(1100, 0, 690), FRotator(0, 180, 0), Def.Title, Magenta, bHall ? 46.f : 64.f);
	// The place's own light: a storm overhead, or lava glowing up from behind the stands.
	if (Def.Pulse == EKSPulse::Lightning) Arena.PulseLights.Add(B.Light(FVector(900, 0, 1000), FLinearColor(0.75f, 0.85f, 1.f), 0.f, 4200.f));
	if (Def.Pulse == EKSPulse::Lava) for (float Y : { -520.f, 520.f }) Arena.PulseLights.Add(B.Light(FVector(1050, Y, 80), Def.B, 3000.f, 1600.f));

	Arena.Props = B.Props; Arena.PropsMissing = B.PropsMissing;
	// The rest of the cut roster watches from outside the ring.
	for (int32 i = 0; i < 9; i++)
	{
		const float A = FMath::DegreesToRadians(-64.f + i * 16.f), R = 760.f + 90.f * (i % 2);
		const FVector Pos(FMath::Cos(A) * R + 120.f, FMath::Sin(A) * R, 0);
		AKSDummy* Dummy = B.World->SpawnActor<AKSDummy>(Pos, FRotator(0, FMath::RadiansToDegrees(A) + 180.f, 0));
		if (!Dummy) continue;
		Dummy->Setup(FLinearColor(0.05f, 0.04f, 0.08f), FLinearColor(0.12f, 0.1f, 0.2f), false, 0.f);
		Dummy->SetActorScale3D(FVector(0.9f + 0.05f * (i % 4)));
		Dummy->Snap(KSPoses::Idle());
		B.Out->Add(Dummy);
		Arena.Crowd.Add(Dummy);
	}
}
