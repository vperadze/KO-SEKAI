#include "KSVfx.h"
#include "NiagaraSystem.h"
#include "NiagaraComponent.h"
#include "NiagaraFunctionLibrary.h"
#include "Engine/World.h"

namespace
{
	// Nothing stays on screen for long (player, 2026-11-13: "not covering the whole screen for too long, 1-2 seconds").
	// A burst stops making particles after BurstSeconds, a loop after its Life; what is in the air then gets
	// FadeSeconds more and is taken away.
	const float BurstSeconds = 1.1f, FadeSeconds = 0.8f;
}

UNiagaraSystem* FKSVfx::Load(const TCHAR* Path)
{
	if (const TStrongObjectPtr<UNiagaraSystem>* Have = Systems.Find(Path)) return Have->Get();
	// "/Game/A/NS_X" is the package; the object in it is "/Game/A/NS_X.NS_X".
	FString Full(Path);
	int32 Slash = INDEX_NONE;
	if (!Full.Contains(TEXT(".")) && Full.FindLastChar(TEXT('/'), Slash)) Full += TEXT(".") + Full.Mid(Slash + 1);
	UNiagaraSystem* System = LoadObject<UNiagaraSystem>(nullptr, *Full);
	if (!System) { Missing++; UE_LOG(LogTemp, Warning, TEXT("KSTEST vfx missing %s"), Path); }
#if WITH_EDITOR
	// Run from the editor's binaries an effect is compiled when it is first loaded, on other threads. Two compiling
	// at once crashed the engine (an assertion in its Niagara type registry, 2026-11-13: a special that starts two
	// effects in one frame). So each one is finished here before the next is touched; AKSGameMode loads them all
	// when the game starts. A packaged game has them compiled already.
	else System->WaitForCompilationComplete(true, false);
#endif
	Systems.Add(Path, TStrongObjectPtr<UNiagaraSystem>(System));
	return System;
}

FString FKSVfx::Describe(const TCHAR* Path)
{
	UNiagaraSystem* System = Load(Path);
	if (!System) return TEXT("missing");
	TArray<FNiagaraVariable> Vars;
	System->GetExposedParameters().GetUserParameters(Vars);
	FString Out = System->IsLooping() ? TEXT("loop") : TEXT("burst");
	for (const FNiagaraVariable& Var : Vars) Out += FString::Printf(TEXT(" [%s:%s]"), *Var.GetName().ToString(), *Var.GetType().GetName());
	return Out;
}

void FKSVfx::Play(UWorld* World, const TCHAR* Path, const FVector& Pos, const FRotator& Rot, float Scale, float Life, const FVector& Vel, float Delay, FSetup Setup)
{
	FLater What;
	What.Path = Path; What.Pos = Pos; What.Rot = Rot; What.Scale = Scale; What.Life = Life; What.Vel = Vel; What.Delay = Delay; What.Setup = MoveTemp(Setup);
	if (Delay > 0.f) Later.Add(MoveTemp(What));
	else Start(World, What);
}

void FKSVfx::Start(UWorld* World, const FLater& What)
{
	UNiagaraSystem* System = Load(*What.Path);
	if (!System || !World) return;
	// Not started yet: its settings go in first, so the first particles already have them.
	UNiagaraComponent* Comp = UNiagaraFunctionLibrary::SpawnSystemAtLocation(World, System, What.Pos, What.Rot, FVector(What.Scale), true, false, ENCPoolMethod::None, false);
	if (!Comp) return;
	if (What.Setup) What.Setup(Comp);
	Comp->Activate(true);
	FLive L;
	L.Comp = Comp; L.Pos = What.Pos; L.Vel = What.Vel; L.Life = What.Life;
	Live.Add(L);
	Made++;
}

void FKSVfx::Tick(UWorld* World, float Dt)
{
	for (int32 i = Later.Num() - 1; i >= 0; i--)
	{
		Later[i].Delay -= Dt;
		if (Later[i].Delay > 0.f) continue;
		const FLater What = Later[i];
		Later.RemoveAt(i);
		Start(World, What);
	}
	for (int32 i = Live.Num() - 1; i >= 0; i--)
	{
		FLive& L = Live[i];
		UNiagaraComponent* Comp = L.Comp.Get();
		if (!Comp) { Live.RemoveAtSwap(i); continue; }
		L.Age += Dt;
		if (!L.bOff && !L.Vel.IsZero()) { L.Pos += L.Vel * Dt; Comp->SetWorldLocation(L.Pos); }
		const float Off = L.Life > 0.f ? L.Life : BurstSeconds;
		if (!L.bOff && L.Age >= Off) { L.bOff = true; Comp->Deactivate(); }
		if (L.Age >= Off + (Off < 0.5f ? 0.4f : FadeSeconds)) { Comp->DestroyComponent(); Live.RemoveAtSwap(i); }      // a punch's sparks go quicker still
	}
}

void FKSVfx::Clear()
{
	for (FLive& L : Live) if (UNiagaraComponent* Comp = L.Comp.Get()) Comp->DestroyComponent();
	Live.Reset();
	Later.Reset();
}

const TArray<FString>& FKSVfx::TourList()
{
	static const TArray<FString> List = []
	{
		TArray<FString> L;
		auto Add = [&L](const TCHAR* Folder, std::initializer_list<const TCHAR*> Names) { for (const TCHAR* Name : Names) L.Add(FString(Folder) / Name); };
		Add(TEXT("/Game/MixedVFX/Particles/Explosions"), { TEXT("NS_ExplosionGroundBig"), TEXT("NS_ExplosionGroundMedium"), TEXT("NS_ExplosionGroundSmall"), TEXT("NS_ExplosionMidAirBig"), TEXT("NS_ExplosionMidAirSmall") });
		Add(TEXT("/Game/MixedVFX/Particles/Fires"), { TEXT("NS_BigFire"), TEXT("NS_Campfire"), TEXT("NS_FireBarrier"), TEXT("NS_FireMeteorShower"), TEXT("NS_FlameThrower"), TEXT("NS_FlameTornado"), TEXT("NS_TorchFire") });
		Add(TEXT("/Game/MixedVFX/Particles/Mix"), { TEXT("NS_ElectricField"), TEXT("NS_HealingAura"), TEXT("NS_Mix_01"), TEXT("NS_Mix_02"), TEXT("NS_Mix_03"), TEXT("NS_Wind") });
		Add(TEXT("/Game/MixedVFX/Particles/Projectiles"), { TEXT("NS_CursedArrow"), TEXT("NS_FairyArrow"), TEXT("NS_FireArrow"), TEXT("NS_GalaxyArrow"), TEXT("NS_HolyArrow"), TEXT("NS_LightningArrow"), TEXT("NS_LightningBolt"),
			TEXT("NS_Projectile_01"), TEXT("NS_Projectile_02"), TEXT("NS_Projectile_03"), TEXT("NS_Projectile_04"), TEXT("NS_Projectile_05"), TEXT("NS_Shuriken_01"), TEXT("NS_Shuriken_02"), TEXT("NS_Shuriken_03"), TEXT("NS_Shuriken_04"), TEXT("NS_VampireArrow") });
		Add(TEXT("/Game/MixedVFX/Particles/Projectiles/Hits"), { TEXT("NS_CursedArrow_Hit"), TEXT("NS_FairyArrow_Hit"), TEXT("NS_FireArrow_Hit"), TEXT("NS_GalaxyArrow_Hit"), TEXT("NS_HolyArrow_Hit"), TEXT("NS_LightningArrow_Hit"), TEXT("NS_LightningBolt_Hit"),
			TEXT("NS_Projectile_01_Hit"), TEXT("NS_Projectile_02_Hit"), TEXT("NS_Projectile_03_Hit"), TEXT("NS_Projectile_04_Hit"), TEXT("NS_Projectile_05_Hit"), TEXT("NS_Shuriken_01_Hit"), TEXT("NS_Shuriken_02_Hit"), TEXT("NS_Shuriken_03_Hit"), TEXT("NS_Shuriken_04_Hit"), TEXT("NS_VampireArrow_Hit") });
		Add(TEXT("/Game/MixedVFX/Particles/Slashes"), { TEXT("NS_FireSlash"), TEXT("NS_GalaxySlash"), TEXT("NS_HolySlash"), TEXT("NS_LightningSlash"), TEXT("NS_VampireSlash") });
		Add(TEXT("/Game/FreeStylizedLaserBeamVFX/VFX"), { TEXT("NS_Laser_01"), TEXT("NS_Laser_02"), TEXT("NS_Laser_03"), TEXT("Hit/NS_Hit_01"), TEXT("Hit/NS_Hit_02"), TEXT("Hit/NS_Hit_03"),
			TEXT("MuzzleFlash/NS_MuzzleFlash_01"), TEXT("MuzzleFlash/NS_MuzzleFlash_02"), TEXT("MuzzleFlash/NS_MuzzleFlash_03") });
		Add(TEXT("/Game/PotaVFX_Smoke/VFX/System"), { TEXT("ColorSmoke/NS_SmokeGradient_Loop"), TEXT("ColorSmoke/NS_SmokeGradient_Loop_Masked"), TEXT("SmokeBurst/NS_MagicSmokeBurstAir"), TEXT("SmokeRing/NS_SmokeRing"), TEXT("SmokeRing/NS_SmokeRing_Masked") });
		Add(TEXT("/Game/Vefects/Zap_VFX/VFX/Zap/Particles"), { TEXT("NS_Zap_01_Red"), TEXT("NS_Zap_02_Blue"), TEXT("NS_Zap_03_Yellow"), TEXT("NS_Zap_04_Green"), TEXT("NS_Zap_05_Purple"), TEXT("NS_Zap_06_White"), TEXT("NS_Zap_07_Black") });
		Add(TEXT("/Game/Vefects/VFX_Distortion/VFX/Particles"), { TEXT("NS_Distortion_Flat_01"), TEXT("NS_Distortion_Flat_02"), TEXT("NS_Distortion_Flat_03"), TEXT("NS_Distortion_Flat_04"), TEXT("NS_Distortion_Flat_05"), TEXT("NS_Distortion_Flat_06"),
			TEXT("NS_Distortion_Flat_07"), TEXT("NS_Distortion_Flat_08"), TEXT("NS_Distortion_Flat_09"), TEXT("NS_Distortion_Flat_10"), TEXT("NS_Distortion_Flat_11"), TEXT("NS_Distortion_Flat_12"), TEXT("NS_Distortion_Flat_13") });
		Add(TEXT("/Game/Vefects/Easy_Impact_Frames/VFX"), { TEXT("Extras/Particles/NS_Extra_Lightning_Example_VFX"), TEXT("Frames/Particles/NS_Impact_Frame_Advanced_01_Always"), TEXT("Frames/Particles/NS_Impact_Frame_Advanced_02_Always"),
			TEXT("Frames/Particles/NS_Impact_Frame_Advanced_03_Always"), TEXT("Frames/Particles/NS_Impact_Frame_Advanced_04_Always"), TEXT("Frames/Particles/NS_Impact_Frame_Advanced_05_Always"), TEXT("Frames/Particles/NS_Impact_Frame_Advanced_06_Always"),
			TEXT("Frames/Particles/NS_Impact_Frame_Advanced_07_Always"), TEXT("Frames/Particles/NS_Impact_Frame_Advanced_08_Always"), TEXT("Frames/Particles/NS_Impact_Frame_Advanced_09_Always"), TEXT("Frames/Particles/NS_Impact_Frame_Advanced_10_Always") });
		Add(TEXT("/Game/NiagaraExamples"), { TEXT("FX_Explosions/NS_Dirt_Explosion"), TEXT("FX_Explosions/NS_Dirt_Explosion_Small"), TEXT("FX_Explosions/NS_Explosion"), TEXT("FX_Explosions/NS_Explosion_Medium"), TEXT("FX_Explosions/NS_Explosion_Small"),
			TEXT("FX_Misc/NS_Fire"), TEXT("FX_Misc/NS_FireworkBurst"), TEXT("FX_Misc/NS_Bubble_Burst"), TEXT("FX_Player/NS_Player_Buff_Looping"), TEXT("FX_Player/NS_Player_DeBuff_Looping"), TEXT("FX_Player/NS_Player_Electricity_Looping"),
			TEXT("FX_Player/NS_Player_Teleport_In"), TEXT("FX_Player/NS_Player_Teleport_Out"), TEXT("FX_Ribbons/NS_TeslaCoil"), TEXT("FX_Smoke/NS_Smoke_Plume"), TEXT("FX_Sparks/NS_Spark_Burst"), TEXT("FX_Sparks/NS_Spark_Continuous"),
			TEXT("FX_Weapons/Impacts/NS_Impact_Concrete"), TEXT("FX_Weapons/Impacts/NS_Impact_Metal"), TEXT("FX_Weapons/MuzzleFlashes/NS_MuzzleFlash"), TEXT("FX_Weapons/Trails/NS_RocketTrail") });
		return L;
	}();
	return List;
}
