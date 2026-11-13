#pragma once

#include "CoreMinimal.h"
#include "UObject/StrongObjectPtr.h"

class UNiagaraSystem;
class UNiagaraComponent;
class UWorld;

/** Plays the particle effects (Niagara systems from the Fab packs under Content) that the special abilities are made
 *  of. An effect is named by its package path, "/Game/MixedVFX/Particles/Fires/NS_BigFire". The packs are listed in
 *  docs/VFX.md; which effect belongs to which special is in KSGameMode.cpp (SpecialLaunch, SpecialLand). */
class FKSVfx
{
public:
	using FSetup = TFunction<void(UNiagaraComponent*)>;

	/** Starts an effect Delay seconds from now. Life 0: it is a burst and ends by itself; otherwise it is a loop and is
	 *  switched off after Life seconds. Vel carries it along (a projectile). Setup runs before it starts, for the
	 *  effect's own settings (its colour, where a beam ends). */
	void Play(UWorld* World, const TCHAR* Path, const FVector& Pos, const FRotator& Rot = FRotator::ZeroRotator, float Scale = 1.f, float Life = 0.f,
		const FVector& Vel = FVector::ZeroVector, float Delay = 0.f, FSetup Setup = nullptr);
	void Tick(UWorld* World, float Dt);
	/** Removes everything at once (leaving the ring). */
	void Clear();
	UNiagaraSystem* Load(const TCHAR* Path);
	/** The effect's own settings by name and type, for the log. */
	FString Describe(const TCHAR* Path);
	/** Every effect worth looking at, for -KSVfxTour. */
	static const TArray<FString>& TourList();

	int32 Made = 0, Missing = 0;   // for the self-check

private:
	struct FLive { TWeakObjectPtr<UNiagaraComponent> Comp; FVector Pos = FVector::ZeroVector, Vel = FVector::ZeroVector; float Age = 0.f, Life = 0.f; bool bOff = false; };
	struct FLater { FString Path; FVector Pos = FVector::ZeroVector, Vel = FVector::ZeroVector; FRotator Rot = FRotator::ZeroRotator; float Scale = 1.f, Life = 0.f, Delay = 0.f; FSetup Setup; };
	void Start(UWorld* World, const FLater& What);
	TMap<FString, TStrongObjectPtr<UNiagaraSystem>> Systems;   // kept alive here: nothing else holds them between specials
	TArray<FLive> Live;
	TArray<FLater> Later;
};
