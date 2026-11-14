#include "KSKeys.h"
#include "Misc/ConfigCacheIni.h"

namespace
{
	struct FKSKeyDef { const TCHAR* Id; const TCHAR* Name; const TCHAR* Webcam; FKey Default; };

	const FKSKeyDef& Def(int32 Index)
	{
		static const FKSKeyDef Defs[KSKeys::Num] = {
			{ TEXT("JabLeft"),       TEXT("LEFT JAB"),        TEXT("punch straight with your left hand"),   EKeys::A },
			{ TEXT("HookLeft"),      TEXT("LEFT HOOK"),       TEXT("swing your left hand round, sideways"), EKeys::Q },
			{ TEXT("UppercutLeft"),  TEXT("LEFT UPPERCUT"),   TEXT("punch upwards with your left hand"),    EKeys::Z },
			{ TEXT("JabRight"),      TEXT("RIGHT JAB"),       TEXT("punch straight with your right hand"),  EKeys::D },
			{ TEXT("HookRight"),     TEXT("RIGHT HOOK"),      TEXT("swing your right hand round, sideways"), EKeys::E },
			{ TEXT("UppercutRight"), TEXT("RIGHT UPPERCUT"),  TEXT("punch upwards with your right hand"),   EKeys::C },
			{ TEXT("Block"),         TEXT("BLOCK"),           TEXT("hold both fists up in front of your face"), EKeys::SpaceBar },
			{ TEXT("EvadeLeft"),     TEXT("EVADE LEFT"),      TEXT("lean your body to the left"),           EKeys::Left },
			{ TEXT("EvadeRight"),    TEXT("EVADE RIGHT"),     TEXT("lean your body to the right"),          EKeys::Right },
			{ TEXT("Duck"),          TEXT("DUCK"),            TEXT("bend your knees and go down"),          EKeys::Down },
			{ TEXT("Special"),       TEXT("SPECIAL ABILITY"), TEXT("throw both hands up or down, or shout (meter full)"), EKeys::S },
			{ TEXT("Pause"),         TEXT("PAUSE"),           TEXT("keyboard only"),                        EKeys::P },
		};
		return Defs[FMath::Clamp(Index, 0, KSKeys::Num - 1)];
	}

	const TCHAR* const Section = TEXT("KoSekai.Keys");

	TArray<FKey>& Keys()
	{
		static TArray<FKey> Now;
		if (Now.Num() == 0)
		{
			for (int32 i = 0; i < KSKeys::Num; i++)
			{
				FKey Key = Def(i).Default;
				FString Saved;
				if (GConfig && GConfig->GetString(Section, Def(i).Id, Saved, GGameUserSettingsIni) && FKey(*Saved).IsValid()) Key = FKey(*Saved);
				Now.Add(Key);
			}
		}
		return Now;
	}

	void Save()
	{
		if (!GConfig) return;
		for (int32 i = 0; i < KSKeys::Num; i++) GConfig->SetString(Section, Def(i).Id, *Keys()[i].GetFName().ToString(), GGameUserSettingsIni);
		GConfig->Flush(false, GGameUserSettingsIni);
	}
}

const FKey& KSKeys::Get(EKSKey Action) { return Keys()[FMath::Clamp((int32)Action, 0, Num - 1)]; }
const TCHAR* KSKeys::Name(EKSKey Action) { return Def((int32)Action).Name; }
const TCHAR* KSKeys::WebcamMove(EKSKey Action) { return Def((int32)Action).Webcam; }
FString KSKeys::Label(const FKey& Key) { return Key.GetDisplayName(false).ToString().ToUpper(); }
bool KSKeys::IsChanged(EKSKey Action) { return Get(Action) != Def((int32)Action).Default; }

EKSKey KSKeys::Bind(EKSKey Action, const FKey& Key)
{
	TArray<FKey>& Now = Keys();
	const int32 Mine = FMath::Clamp((int32)Action, 0, Num - 1);
	EKSKey Moved = EKSKey::Num;
	for (int32 i = 0; i < Num; i++)
	{
		if (i != Mine && Now[i] == Key) { Now[i] = Now[Mine]; Moved = (EKSKey)i; }
	}
	Now[Mine] = Key;
	Save();
	return Moved;
}

void KSKeys::Reset()
{
	TArray<FKey>& Now = Keys();
	for (int32 i = 0; i < Num; i++) Now[i] = Def(i).Default;
	Save();
}
