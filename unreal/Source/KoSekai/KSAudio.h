// Sound effects. Each one plays a recorded sound from /Game/KS/Audio when there is one (S_<Name>_<n>, a random
// variant each time), and otherwise falls back to a tone synthesised in code, as in tracking-test/fight.html.
#pragma once

#include "CoreMinimal.h"

enum class EKSSfx : uint8 { Hit, Counter, Blocked, Hurt, Whoosh, Tell, Dodge, Bell1, Bell2, Bell3, KO, PullIn, Coin, Step, Footstep, Fall, Win, Lose };

namespace KSAudio
{
	void Play(UObject* WorldContext, EKSSfx Sfx, float Volume = 1.f);
}
