// The keys of a bout, which the player can change on the CONTROLS screen of the start menu (AKSGameMode::TickControls,
// AKSHUD::DrawControls). The choice is kept in the user's settings file. The keys that work the menus (arrows, Enter,
// Esc) are fixed, so nobody can lock himself out of the screen that would put it right.
#pragma once

#include "CoreMinimal.h"
#include "InputCoreTypes.h"

enum class EKSKey : uint8 { JabLeft, HookLeft, UppercutLeft, JabRight, HookRight, UppercutRight, Block, EvadeLeft, EvadeRight, Duck, Special, Pause, Num };

namespace KSKeys
{
	constexpr int32 Num = (int32)EKSKey::Num;

	/** The key an action is on now. */
	const FKey& Get(EKSKey Action);
	/** What the action is called on screen ("LEFT JAB"). */
	const TCHAR* Name(EKSKey Action);
	/** The same move done in front of the webcam ("jab with your left hand"). */
	const TCHAR* WebcamMove(EKSKey Action);
	/** A key's name as it is shown ("SPACE BAR"). */
	FString Label(const FKey& Key);
	inline FString Label(EKSKey Action) { return Label(Get(Action)); }
	/** Puts an action on a key and saves. If another action had that key, the two change places: the returned action
	 *  is the one that was moved (EKSKey::Num if none was). */
	EKSKey Bind(EKSKey Action, const FKey& Key);
	/** Back to the keys the game ships with. */
	void Reset();
	/** True if the action is not on its shipped key. */
	bool IsChanged(EKSKey Action);
}
