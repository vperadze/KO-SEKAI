#include "KSWorld.h"
#include "KSDummy.h"
#include "KSTypes.h"
#include "Engine/World.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/StaticMesh.h"
#include "Engine/PointLight.h"
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
	static UStaticMesh* Plane = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Plane.Plane"));
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

void FKSBuilder::Ambient(const FLinearColor& Color, float Intensity, const FLinearColor& FogColor, float FogDensity)
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
		Comp->SetFogHeightFalloff(0.05f);
		Comp->SetFogInscatteringColor(FogColor);
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

void KSWorld::BuildArena(FKSBuilder& B, FKSArena& Arena)
{
	using namespace KSColors;
	B.Ambient(FLinearColor(0.5f, 0.3f, 0.9f), 0.8f, FLinearColor(0.03f, 0.0f, 0.06f), 0.006f);
	B.Box(FVector(0, 0, -10), FVector(6000, 6000, 20), FLinearColor(0.012f, 0.004f, 0.03f));
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
		B.Shape(B.Cylinder, Corner + FVector(0, 0, 70), FVector(14, 14, 140), Yellow, 2.f);
		const FVector MidSide(FMath::Cos(FMath::DegreesToRadians(A)) * R, FMath::Sin(FMath::DegreesToRadians(A)) * R, 0);
		for (float Z : { 55.f, 95.f, 130.f })
		{
			B.Box(MidSide + FVector(0, 0, Z), i % 2 ? FVector(2 * R, 3, 3) : FVector(3, 2 * R, 3), i % 2 ? Cyan : Magenta, 4.f);
		}
	}
	// The ring canvas, and the stands full of people around the far side (both painted with Dreamina).
	B.Image(TEXT("/Game/KS/Arena/T_KS_Mat.T_KS_Mat"), true, FVector(0, 0, 2.f), FVector(0, 1, 0), FVector(-1, 0, 0), 700.f, 700.f);
	{
		const int32 Panels = 16;
		const float Radius = 1500.f, From = -115.f, To = 115.f, Step = (To - From) / Panels;
		const float PanelWidth = 2.f * Radius * FMath::Tan(FMath::DegreesToRadians(Step * 0.5f)), WallHeight = 1300.f;
		for (int32 i = 0; i < Panels; i++)
		{
			const float A = FMath::DegreesToRadians(From + (i + 0.5f) * Step);
			const FVector Out(FMath::Cos(A), FMath::Sin(A), 0.f), Across(-FMath::Sin(A), FMath::Cos(A), 0.f);
			B.Image(TEXT("/Game/KS/Arena/T_KS_Stands.T_KS_Stands"), false, Out * Radius + FVector(0, 0, WallHeight * 0.5f - 40.f), Across, FVector(0, 0, -1), PanelWidth + 2.f, WallHeight,
				FVector4(1.f / Panels, 1.f, (float)i / Panels, 0.f), 0.55f);
		}
	}
	// Tall bars on the horizon, the edge of the screen.
	for (int32 i = 0; i < 28; i++)
	{
		const float A = FMath::DegreesToRadians(i * 360.f / 28.f), H = 500.f + 420.f * FMath::Abs(FMath::Sin(i * 1.7f));
		B.Box(FVector(FMath::Cos(A) * 2300, FMath::Sin(A) * 2300, H * 0.5f), FVector(30, 30, H), i % 2 ? Magenta : Cyan, 2.5f, FRotator(0, FMath::RadiansToDegrees(A), 0));
	}
	B.Light(FVector(40, 0, 520), FLinearColor(1.0f, 0.9f, 1.0f), 5200.f, 1600.f, true);
	B.Light(FVector(-420, -380, 260), Cyan, 900.f, 1500.f);
	B.Light(FVector(-420, 380, 260), Magenta, 900.f, 1500.f);
	B.Light(FVector(700, 0, 300), Magenta, 1600.f, 1800.f);

	Arena.NameSign = B.Text(FVector(1100, 0, 520), FRotator(0, 180, 0), TEXT(""), Yellow, 150.f);
	B.Text(FVector(1100, 0, 690), FRotator(0, 180, 0), TEXT("K O - S E K A I   1 9 8 7   U N R E L E A S E D"), Magenta, 46.f);

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
