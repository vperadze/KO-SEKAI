// The microphone, listened to for one thing: a shout. A special ability is set off by shouting the fighter's battle
// cry; the game cannot tell words apart, it hears that somebody was suddenly much louder than the room (the music
// and the crowd included) for a fifth of a second.
#pragma once

#include "CoreMinimal.h"
#include <atomic>

namespace Audio { class FAudioCapture; }

class FKSMic
{
public:
	FKSMic();
	~FKSMic();
	/** Opens the default microphone. False if there is none or it cannot be opened; the S key still works then. */
	bool Open();
	void Close();
	bool IsOpen() const { return bOpen; }
	/** How loud the room is right now, 0..1 (for the little meter on the HUD). */
	float Level() const { return FMath::Clamp(Loud.load() * 4.f, 0.f, 1.f); }
	/** Call every frame. On Linux the sound arrives through a pipe that has to be emptied; elsewhere this does nothing. */
	void Poll();
	/** True once for each shout heard since the last call. */
	bool TakeShout() { return bShout.exchange(false); }
	const FString& Status() const { return StatusText; }

private:
	void Hear(const float* Samples, int32 Count, int32 SampleRate);

	TUniquePtr<Audio::FAudioCapture> Capture;
	// Linux: Unreal has no microphone back end there, so the samples come from the system's own recorder (arecord).
	FProcHandle Recorder; void* PipeRead = nullptr; void* PipeWrite = nullptr;
	TArray<uint8> Left;                                   // an odd byte left over from the last read
	bool bOpen = false;
	FString StatusText = TEXT("microphone not opened");
	std::atomic<float> Loud{ 0.f };
	std::atomic<bool> bShout{ false };
	// Only touched on the capture thread.
	float Floor = 0.01f, LoudSeconds = 0.f, QuietSeconds = 1.f;
	bool bArmed = true;
};
