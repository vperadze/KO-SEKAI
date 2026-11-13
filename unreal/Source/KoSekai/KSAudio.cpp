#include "KSAudio.h"
#include "Sound/SoundWaveProcedural.h"
#include "Sound/SoundWave.h"
#include "Components/AudioComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/World.h"
#include "TimerManager.h"

namespace
{
	const int32 SampleRate = 22050;
	enum EWave { Square, Sine, Saw, Triangle, Noise };

	struct FVoice
	{
		EWave Wave; float Freq; float Dur; float Vol; float Slide = 0.f; float Delay = 0.f;
	};

	void Render(TArray<float>& Mix, const FVoice& V)
	{
		const int32 Start = FMath::RoundToInt(V.Delay * SampleRate), Count = FMath::RoundToInt(V.Dur * SampleRate);
		if (Mix.Num() < Start + Count) Mix.SetNumZeroed(Start + Count);
		float Phase = 0.f, Low = 0.f;
		// For noise, Freq is the low-pass cutoff.
		const float Cut = FMath::Clamp(2.f * PI * V.Freq / SampleRate, 0.01f, 1.f);
		for (int32 i = 0; i < Count; i++)
		{
			const float U = (float)i / Count;
			const float Amp = V.Vol * FMath::Pow(0.001f / V.Vol, U);
			float Sample;
			if (V.Wave == Noise)
			{
				Low += Cut * (FMath::FRandRange(-1.f, 1.f) - Low);
				Sample = Low * 1.6f;
			}
			else
			{
				Phase += FMath::Max(20.f, V.Freq + V.Slide * U) / SampleRate;
				const float F = FMath::Frac(Phase);
				Sample = V.Wave == Square ? (F < 0.5f ? 1.f : -1.f) : V.Wave == Sine ? FMath::Sin(2.f * PI * F)
					: V.Wave == Saw ? 2.f * F - 1.f : 4.f * FMath::Abs(F - 0.5f) - 1.f;
			}
			Mix[Start + i] += Sample * Amp;
		}
	}

	TArray<FVoice> Voices(EKSSfx Sfx)
	{
		switch (Sfx)
		{
		case EKSSfx::Hit:     return { { Noise, 1200, 0.12f, 0.35f }, { Sine, 120, 0.16f, 0.35f, -70 } };
		case EKSSfx::Counter: return { { Noise, 1200, 0.12f, 0.35f }, { Sine, 120, 0.16f, 0.35f, -70 }, { Square, 880, 0.12f, 0.07f, 0, 0.03f }, { Square, 1320, 0.16f, 0.07f, 0, 0.1f } };
		case EKSSfx::Blocked: return { { Square, 740, 0.05f, 0.06f }, { Square, 520, 0.07f, 0.05f, 0, 0.03f } };
		case EKSSfx::Hurt:    return { { Noise, 600, 0.22f, 0.4f }, { Saw, 180, 0.28f, 0.12f, -110 } };
		case EKSSfx::Whoosh:  return { { Noise, 3000, 0.16f, 0.18f } };
		case EKSSfx::Tell:    return { { Square, 1250, 0.05f, 0.05f } };
		case EKSSfx::Dodge:   return { { Triangle, 420, 0.1f, 0.14f, 420 } };
		case EKSSfx::Bell1:   return { { Sine, 1046, 1.1f, 0.18f }, { Sine, 1568, 0.8f, 0.07f } };
		case EKSSfx::Bell2:   return { { Sine, 1046, 1.1f, 0.18f }, { Sine, 1568, 0.8f, 0.07f }, { Sine, 1046, 1.1f, 0.18f, 0, 0.3f }, { Sine, 1568, 0.8f, 0.07f, 0, 0.3f } };
		case EKSSfx::Bell3:   return { { Sine, 1046, 1.1f, 0.18f }, { Sine, 1046, 1.1f, 0.18f, 0, 0.3f }, { Sine, 1046, 1.1f, 0.18f, 0, 0.6f }, { Sine, 1568, 0.8f, 0.07f, 0, 0.6f } };
		case EKSSfx::KO:      return { { Square, 660, 0.14f, 0.08f }, { Square, 580, 0.14f, 0.08f, 0, 0.1f }, { Square, 500, 0.14f, 0.08f, 0, 0.2f }, { Square, 420, 0.14f, 0.08f, 0, 0.3f }, { Square, 340, 0.14f, 0.08f, 0, 0.4f }, { Square, 260, 0.14f, 0.08f, 0, 0.5f } };
		case EKSSfx::PullIn:  return { { Saw, 60, 3.0f, 0.16f, 900 }, { Noise, 2500, 3.0f, 0.10f }, { Sine, 220, 3.0f, 0.12f, 1500 } };
		case EKSSfx::Coin:    return { { Square, 988, 0.08f, 0.08f }, { Square, 1319, 0.35f, 0.08f, 0, 0.08f } };
		case EKSSfx::Step:    return { { Noise, 500, 0.07f, 0.10f } };
		}
		return {};
	}
}

namespace
{
	/** The recorded variants of one sound, loaded the first time it is asked for and kept for the rest of the run. */
	const TArray<USoundBase*>& Recorded(EKSSfx Sfx)
	{
		static const TCHAR* Names[] = { TEXT("Hit"), TEXT("Counter"), TEXT("Blocked"), TEXT("Hurt"), TEXT("Whoosh"), TEXT("Tell"), TEXT("Dodge"), TEXT("Bell1"), TEXT("Bell2"), TEXT("Bell3"),
			TEXT("KO"), TEXT("PullIn"), TEXT("Coin"), TEXT("Step"), TEXT("Footstep"), TEXT("Fall"), TEXT("Win"), TEXT("Lose"),
			TEXT("Cheer"), TEXT("Roar"), TEXT("Gasp"), TEXT("Boo"), TEXT("Shout"), TEXT("Beast"),
			TEXT("Pop"), TEXT("Menu"), TEXT("Select"), TEXT("Score"), TEXT("Slam") };
		static TMap<EKSSfx, TArray<USoundBase*>> Loaded;
		if (TArray<USoundBase*>* Found = Loaded.Find(Sfx)) return *Found;
		TArray<USoundBase*>& List = Loaded.Add(Sfx);
		if ((int32)Sfx < UE_ARRAY_COUNT(Names))
		{
			for (int32 i = 1; i <= 6; i++)
			{
				const FString Path = FString::Printf(TEXT("/Game/KS/Audio/S_%s_%d.S_%s_%d"), Names[(int32)Sfx], i, Names[(int32)Sfx], i);
				USoundBase* Sound = LoadObject<USoundBase>(nullptr, *Path, nullptr, LOAD_NoWarn | LOAD_Quiet);
				if (!Sound) break;
				Sound->AddToRoot();   // held in a static, so keep it from being collected
				List.Add(Sound);
			}
		}
		// One line per sound for tools/check.sh: 0 variants means the synthesised fallback is in use.
		if ((int32)Sfx < UE_ARRAY_COUNT(Names)) UE_LOG(LogTemp, Display, TEXT("KSTEST sound=%s recorded_variants=%d"), Names[(int32)Sfx], List.Num());
		return List;
	}
}

void KSAudio::Play(UObject* WorldContext, EKSSfx Sfx, float Volume)
{
	UWorld* World = WorldContext ? WorldContext->GetWorld() : nullptr;
	if (!World || World->GetNetMode() == NM_DedicatedServer) return;

	const TArray<USoundBase*>& Variants = Recorded(Sfx);
	if (Variants.Num())
	{
		// A different take and a slightly different pitch each time, so repeated punches do not sound stamped out.
		UGameplayStatics::PlaySound2D(World, Variants[FMath::RandRange(0, Variants.Num() - 1)], Volume, FMath::FRandRange(0.94f, 1.06f));
		return;
	}

	TArray<float> Mix;
	for (const FVoice& V : Voices(Sfx)) Render(Mix, V);
	if (Mix.Num() == 0) return;
	TArray<int16> Pcm;
	Pcm.SetNumUninitialized(Mix.Num());
	for (int32 i = 0; i < Mix.Num(); i++) Pcm[i] = (int16)(FMath::Clamp(Mix[i] * 1.6f, -1.f, 1.f) * 32000.f);

	const float Seconds = (float)Mix.Num() / SampleRate;
	USoundWaveProcedural* Wave = NewObject<USoundWaveProcedural>(GetTransientPackage());
	Wave->SetSampleRate(SampleRate);
	Wave->NumChannels = 1;
	Wave->Duration = Seconds;
	Wave->SoundGroup = SOUNDGROUP_Effects;
	Wave->bLooping = false;
	Wave->QueueAudio((const uint8*)Pcm.GetData(), Pcm.Num() * sizeof(int16));

	UAudioComponent* Player = UGameplayStatics::CreateSound2D(World, Wave, Volume);
	if (!Player) return;
	Player->Play();
	// A procedural wave never ends by itself (it plays silence once its data runs out), so stop it explicitly.
	FTimerHandle Handle;
	TWeakObjectPtr<UAudioComponent> Weak(Player);
	World->GetTimerManager().SetTimer(Handle, [Weak]() { if (Weak.IsValid()) { Weak->Stop(); Weak->DestroyComponent(); } }, Seconds + 0.1f, false);
}

void KSAudio::Music(UObject* WorldContext, TObjectPtr<UAudioComponent>& Player, float Volume, float FadeSeconds, const TCHAR* TrackName)
{
	UWorld* World = WorldContext ? WorldContext->GetWorld() : nullptr;
	if (!World || World->GetNetMode() == NM_DedicatedServer) return;

	// Each arena has its own track; every one is loaded the first time it is asked for and kept.
	static TMap<FString, USoundWave*> Tracks;
	USoundWave* Track = nullptr;
	if (USoundWave** Found = Tracks.Find(TrackName)) Track = *Found;
	else
	{
		const FString Path = FString::Printf(TEXT("/Game/KS/Audio/M_%s.M_%s"), TrackName, TrackName);
		Track = LoadObject<USoundWave>(nullptr, *Path, nullptr, LOAD_NoWarn | LOAD_Quiet);
		if (Track) Track->AddToRoot();
		Tracks.Add(TrackName, Track);
		// One line for tools/check.sh. A track that does not loop would fall silent 75 s into a bout.
		UE_LOG(LogTemp, Display, TEXT("KSTEST music track=%s loaded=%d looping=%d seconds=%d"), TrackName, Track ? 1 : 0, Track && Track->bLooping ? 1 : 0, Track ? FMath::RoundToInt(Track->Duration) : 0);
	}
	if (!Track) return;

	if (Volume <= 0.f)
	{
		if (Player && Player->IsPlaying()) Player->FadeOut(1.2f, 0.f);
		return;
	}
	if (!Player)
	{
		Player = UGameplayStatics::CreateSound2D(World, Track, 1.f, 1.f, 0.f, nullptr, false, false);
		if (!Player) return;   // no audio device (-nosound)
	}
	else if (Player->Sound != Track) { Player->Stop(); Player->SetSound(Track); }
	if (Player->IsPlaying()) Player->AdjustVolume(FadeSeconds, Volume);
	else
	{
		Player->FadeIn(0.6f, Volume);
		UE_LOG(LogTemp, Display, TEXT("KSTEST music started track=%s volume=%.2f playing=%d"), TrackName, Volume, Player->IsPlaying() ? 1 : 0);
	}
}

void KSAudio::Crowd(UObject* WorldContext, TObjectPtr<UAudioComponent>& Player, float Volume)
{
	UWorld* World = WorldContext ? WorldContext->GetWorld() : nullptr;
	if (!World || World->GetNetMode() == NM_DedicatedServer) return;

	static USoundWave* Track = nullptr;
	static bool bLooked = false;
	if (!bLooked)
	{
		bLooked = true;
		Track = LoadObject<USoundWave>(nullptr, TEXT("/Game/KS/Audio/M_Crowd.M_Crowd"), nullptr, LOAD_NoWarn | LOAD_Quiet);
		if (Track) Track->AddToRoot();
		UE_LOG(LogTemp, Display, TEXT("KSTEST crowd_murmur loaded=%d looping=%d seconds=%d"), Track ? 1 : 0, Track && Track->bLooping ? 1 : 0, Track ? FMath::RoundToInt(Track->Duration) : 0);
	}
	if (!Track) return;
	if (Volume <= 0.f)
	{
		if (Player && Player->IsPlaying()) Player->FadeOut(1.5f, 0.f);
		return;
	}
	if (!Player)
	{
		Player = UGameplayStatics::CreateSound2D(World, Track, 1.f, 1.f, 0.f, nullptr, false, false);
		if (!Player) return;   // no audio device (-nosound)
	}
	if (Player->IsPlaying()) Player->AdjustVolume(1.f, Volume);
	else Player->FadeIn(1.f, Volume);
}
