// Sound effects. Each one plays a recorded sound from /Game/KS/Audio when there is one (S_<Name>_<n>, a random
// variant each time), and otherwise falls back to a tone synthesised in code, as in tracking-test/fight.html.
#pragma once

#include "CoreMinimal.h"

enum class EKSSfx : uint8 { Hit, Counter, Blocked, Hurt, Whoosh, Tell, Dodge, Bell1, Bell2, Bell3, KO, PullIn, Coin, Step, Footstep, Fall, Win, Lose,
	Cheer, Roar, Gasp, Boo, Shout, Beast,     // the crowd (art/tools/prep_crowd.py); these have no synthesised fallback
	Pop, Menu, Select, Score, Slam };         // the interface (art/tools/prep_audio.py); no synthesised fallback either

namespace KSAudio
{
	void Play(UObject* WorldContext, EKSSfx Sfx, float Volume = 1.f);

	/** Fades the looping music /Game/KS/Audio/M_<TrackName> (art/tools/music.py, music_arenas.py) to Volume; a new
	    track name replaces the one playing; 0 stops it,
	    and the next call above 0 starts it from the top. Player is the caller's to keep alive (a UPROPERTY). */
	void Music(UObject* WorldContext, TObjectPtr<class UAudioComponent>& Player, float Volume, float FadeSeconds = 0.8f, const TCHAR* TrackName = TEXT("Fight"));

	/** The looping murmur of the hall (/Game/KS/Audio/M_Crowd), faded to Volume in the same way as Music. */
	void Crowd(UObject* WorldContext, TObjectPtr<class UAudioComponent>& Player, float Volume);
}
