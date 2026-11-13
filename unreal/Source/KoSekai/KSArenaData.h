// The arenas (docs/ARENAS.md). Every arena is the same ring with its own colours, backdrop, ring canvas, crowd,
// music and weather. Arena 0 is the original neon hall. Pictures come from art/arena/gen_arenas.py and
// art/tools/import_arenas.py, the music from art/tools/music_arenas.py.
#pragma once

#include "CoreMinimal.h"

/** What floats in the air of an arena. */
enum class EKSMote : uint8 { None, Drift, Rise, Fall, Rain, Sparks };
/** What the place itself does to the light. */
enum class EKSPulse : uint8 { None, Lightning, Lava };

struct FKSArenaDef
{
	const TCHAR* Key;        // pictures /Game/KS/Arena/T_KS_Bg_<Key> and T_KS_Mat_<Key>; empty: the original hall's pictures
	const TCHAR* Title;      // on the sign over the stands
	const TCHAR* Music;      // /Game/KS/Audio/M_<Music>
	FLinearColor A, B, C;    // the three neon colours of the ropes, grid, lamps and stand edges
	FLinearColor Floor, Ambient, Fog;
	float FogDensity;
	FLinearColor Tint;       // the camera's colour cast
	EKSMote Mote; int32 Motes; FLinearColor MoteColor; float MoteGlow;
	EKSPulse Pulse;
	int32 Home;              // roster index of the fighter who lives here, -1: nobody (used for the last bout)
	// Which spectator sheets sit here (names from KSCrowdData.h, separated by spaces); empty: everyone.
	// Every list needs at least one Giant and one Flyer, or those seats stay empty and tools/check.sh fails.
	const TCHAR* Crowd;
};

static const FKSArenaDef KSArenas[] = {
	{ TEXT(""), TEXT("K O - S E K A I   1 9 8 7   U N R E L E A S E D"), TEXT("Fight"),
	  FLinearColor(0.02f, 0.80f, 1.0f), FLinearColor(1.0f, 0.06f, 0.45f), FLinearColor(1.0f, 0.76f, 0.08f),
	  FLinearColor(0.012f, 0.004f, 0.03f), FLinearColor(0.5f, 0.3f, 0.9f), FLinearColor(0.035f, 0.005f, 0.075f), 0.010f,
	  FLinearColor(1.f, 1.f, 1.f), EKSMote::None, 0, FLinearColor::White, 0.f, EKSPulse::None, -1, TEXT("") },

	{ TEXT("Space"), TEXT("O R B I T A L   R I N G"), TEXT("Space"),
	  FLinearColor(0.10f, 0.70f, 1.0f), FLinearColor(0.55f, 0.25f, 1.0f), FLinearColor(0.80f, 0.90f, 1.0f),
	  FLinearColor(0.004f, 0.006f, 0.03f), FLinearColor(0.3f, 0.4f, 1.0f), FLinearColor(0.010f, 0.015f, 0.07f), 0.008f,
	  FLinearColor(0.92f, 0.96f, 1.10f), EKSMote::Drift, 120, FLinearColor(0.8f, 0.9f, 1.0f), 20.f, EKSPulse::None, 2,
	  TEXT("astro alien ufo robot cyclops slime nerd prof kid unicorn fairy stitch newwave boss") },

	{ TEXT("Beach"), TEXT("S U N S E T   P I E R"), TEXT("Beach"),
	  FLinearColor(1.0f, 0.45f, 0.10f), FLinearColor(1.0f, 0.10f, 0.50f), FLinearColor(0.10f, 0.90f, 0.80f),
	  FLinearColor(0.04f, 0.015f, 0.02f), FLinearColor(1.0f, 0.5f, 0.4f), FLinearColor(0.09f, 0.03f, 0.04f), 0.009f,
	  FLinearColor(1.08f, 0.98f, 0.90f), EKSMote::Drift, 80, FLinearColor(1.0f, 0.7f, 0.3f), 18.f, EKSPulse::None, 4,
	  TEXT("mermaid kraken skater hoops cheer kid mom granny oldman boombox bboy sumo ape phoenix glam chef luchador") },

	{ TEXT("Bar"), TEXT("T H E   R U S T Y   T A P"), TEXT("Bar"),
	  FLinearColor(1.0f, 0.60f, 0.10f), FLinearColor(0.20f, 1.0f, 0.30f), FLinearColor(1.0f, 0.12f, 0.10f),
	  FLinearColor(0.02f, 0.012f, 0.006f), FLinearColor(1.0f, 0.7f, 0.4f), FLinearColor(0.05f, 0.03f, 0.015f), 0.012f,
	  FLinearColor(1.05f, 1.0f, 0.90f), EKSMote::Drift, 70, FLinearColor(1.0f, 0.8f, 0.5f), 7.f, EKSPulse::None, 0,
	  TEXT("cowboy mullet rocker punk boss chef oldman luchador werewolf minotaur ape gnome glam granny ghost stitch") },

	{ TEXT("Rooftop"), TEXT("N E O N   R O O F T O P"), TEXT("Rooftop"),
	  FLinearColor(1.0f, 0.10f, 0.60f), FLinearColor(0.05f, 0.80f, 1.0f), FLinearColor(0.25f, 0.35f, 1.0f),
	  FLinearColor(0.006f, 0.008f, 0.02f), FLinearColor(0.4f, 0.45f, 1.0f), FLinearColor(0.015f, 0.02f, 0.06f), 0.010f,
	  FLinearColor(0.95f, 0.97f, 1.08f), EKSMote::Rain, 150, FLinearColor(0.6f, 0.8f, 1.0f), 5.f, EKSPulse::Lightning, -1,
	  TEXT("bboy skater punk newwave boombox ninja glam rocker robot kid alien hoops cyclops ufo fairy nerd") },

	{ TEXT("Dojo"), TEXT("M O U N T A I N   D O J O"), TEXT("Dojo"),
	  FLinearColor(1.0f, 0.12f, 0.08f), FLinearColor(1.0f, 0.70f, 0.15f), FLinearColor(1.0f, 0.55f, 0.70f),
	  FLinearColor(0.03f, 0.015f, 0.012f), FLinearColor(1.0f, 0.75f, 0.7f), FLinearColor(0.07f, 0.04f, 0.05f), 0.008f,
	  FLinearColor(1.04f, 1.0f, 0.97f), EKSMote::Fall, 120, FLinearColor(1.0f, 0.45f, 0.7f), 7.f, EKSPulse::None, -1,
	  TEXT("ninja sumo dragon oldman luchador wizard kid granny genie phoenix yeti gnome chef prof mom minotaur") },

	{ TEXT("Castle"), TEXT("C A S T L E   H A I R S P R A Y"), TEXT("Castle"),
	  FLinearColor(0.60f, 0.15f, 1.0f), FLinearColor(0.30f, 1.0f, 0.20f), FLinearColor(1.0f, 0.50f, 0.10f),
	  FLinearColor(0.01f, 0.006f, 0.02f), FLinearColor(0.5f, 0.4f, 0.9f), FLinearColor(0.015f, 0.04f, 0.035f), 0.011f,
	  FLinearColor(0.95f, 1.02f, 1.02f), EKSMote::Rise, 70, FLinearColor(0.4f, 1.0f, 0.3f), 14.f, EKSPulse::Lightning, 6,
	  TEXT("vampire ghost skeleton mummy werewolf reaper medusa stitch wizard gnome slime cyclops minotaur fairy granny") },

	{ TEXT("Gym"), TEXT("S W E A T   S T U D I O"), TEXT("Gym"),
	  FLinearColor(1.0f, 0.10f, 0.55f), FLinearColor(1.0f, 0.90f, 0.15f), FLinearColor(0.10f, 1.0f, 0.85f),
	  FLinearColor(0.03f, 0.01f, 0.03f), FLinearColor(1.0f, 0.7f, 0.95f), FLinearColor(0.07f, 0.03f, 0.07f), 0.008f,
	  FLinearColor(1.03f, 1.0f, 1.03f), EKSMote::Rise, 80, FLinearColor(1.0f, 0.4f, 0.8f), 10.f, EKSPulse::None, 3,
	  TEXT("aerobics cheer hoops boombox glam newwave mom luchador sumo kid granny bboy ape fairy unicorn") },

	{ TEXT("Scrapyard"), TEXT("S C R A P Y A R D"), TEXT("Scrapyard"),
	  FLinearColor(1.0f, 0.45f, 0.05f), FLinearColor(1.0f, 0.80f, 0.10f), FLinearColor(0.40f, 0.60f, 1.0f),
	  FLinearColor(0.015f, 0.01f, 0.006f), FLinearColor(1.0f, 0.65f, 0.35f), FLinearColor(0.05f, 0.03f, 0.012f), 0.012f,
	  FLinearColor(1.06f, 1.0f, 0.92f), EKSMote::Sparks, 110, FLinearColor(1.0f, 0.5f, 0.1f), 30.f, EKSPulse::None, 5,
	  TEXT("robot punk rocker stitch cyclops trex mullet skater boss alien slime ufo werewolf oldman nerd") },

	{ TEXT("Volcano"), TEXT("V O L C A N O"), TEXT("Volcano"),
	  FLinearColor(1.0f, 0.10f, 0.03f), FLinearColor(1.0f, 0.40f, 0.03f), FLinearColor(1.0f, 0.80f, 0.10f),
	  FLinearColor(0.03f, 0.004f, 0.002f), FLinearColor(1.0f, 0.35f, 0.2f), FLinearColor(0.09f, 0.015f, 0.005f), 0.012f,
	  FLinearColor(1.10f, 0.96f, 0.90f), EKSMote::Rise, 130, FLinearColor(1.0f, 0.35f, 0.05f), 25.f, EKSPulse::Lava, -1,
	  TEXT("dragon phoenix trex minotaur cyclops genie reaper skeleton werewolf medusa gnome slime ninja wizard oldman") },

	{ TEXT("Studio"), TEXT("S T U D I O   0"), TEXT("Studio"),
	  FLinearColor(1.0f, 0.80f, 0.08f), FLinearColor(1.0f, 0.10f, 0.15f), FLinearColor(0.15f, 0.40f, 1.0f),
	  FLinearColor(0.012f, 0.01f, 0.04f), FLinearColor(0.8f, 0.7f, 1.0f), FLinearColor(0.04f, 0.02f, 0.07f), 0.009f,
	  FLinearColor(1.0f, 1.0f, 1.0f), EKSMote::Fall, 120, FLinearColor(1.0f, 0.8f, 0.2f), 12.f, EKSPulse::None, 1,
	  TEXT("nerd prof mom kid granny boss cheer glam chef newwave aerobics hoops ape ufo fairy cowboy astro") },
};
static const int32 KSArenaCount = UE_ARRAY_COUNT(KSArenas);
