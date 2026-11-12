// Sound effects synthesised in code (square/sine tones and filtered noise), mirroring the ones in
// tracking-test/fight.html, so the game needs no audio files.
#pragma once

#include "CoreMinimal.h"

enum class EKSSfx : uint8 { Hit, Counter, Blocked, Hurt, Whoosh, Tell, Dodge, Bell1, Bell2, Bell3, KO, PullIn, Coin, Step };

namespace KSAudio
{
	void Play(UObject* WorldContext, EKSSfx Sfx, float Volume = 1.f);
}
