// Shared small types for KO-SEKAI.
#pragma once

#include "CoreMinimal.h"

/** The three punches, matching tracker.js ("straight", "hook", "uppercut"). */
enum class EKSPunch : uint8 { Straight, Hook, Uppercut };

/** Which arm. Left/Right are always the fighter's own left and right. */
enum class EKSArm : uint8 { Left, Right };

/** Held defensive stances, matching tracker.js STANCES. */
enum class EKSDefense : uint8 { None, Block, EvadeLeft, EvadeRight, Duck };

/** One fighter from the cut roster. */
struct FKSFighterDef
{
	const TCHAR* Name;
	const TCHAR* Tagline;      // shown on the VS card
	const TCHAR* DefeatLine;   // shown when you beat them
	FLinearColor Body;
	FLinearColor Gloves;
	float Health;              // player always has 100
	float WindupScale;         // < 1 = faster attacks
	float DamageScale;
	float FeintChance;         // chance of a fake once they are hurt
	float IdleScale;           // < 1 = less rest between attacks
	int32 MaxHitsOpen;         // punches you can land per opening
	float Height;              // model scale (camera aim for the real fighters, whose models are already the right size)
	const TCHAR* Asset;        // content folder under /Game/KoSekai/Characters, or nullptr for a placeholder dummy
};

namespace KSColors
{
	const FLinearColor Cyan(0.02f, 0.80f, 1.0f);
	const FLinearColor Magenta(1.0f, 0.06f, 0.45f);
	const FLinearColor Yellow(1.0f, 0.76f, 0.08f);
	const FLinearColor Green(0.13f, 1.0f, 0.33f);
	const FLinearColor Red(1.0f, 0.15f, 0.10f);
	const FLinearColor Lilac(0.86f, 0.78f, 1.0f);
	const FLinearColor Muted(0.40f, 0.31f, 0.62f);
}
