#include "KSMic.h"
#include "AudioCaptureCore.h"
#include "Modules/ModuleManager.h"
#include "Misc/Paths.h"
#include "HAL/PlatformProcess.h"
#include "Misc/CoreDelegates.h"

FKSMic::FKSMic() {}
FKSMic::~FKSMic() { FCoreDelegates::OnEnginePreExit.RemoveAll(this); Close(); }

bool FKSMic::Open()
{
	Close();
#if PLATFORM_LINUX
	// 16 kHz, one channel, 16-bit samples written to a pipe.
	if (!FPaths::FileExists(TEXT("/usr/bin/arecord"))) { StatusText = TEXT("no arecord on this machine"); return false; }
	if (!FPlatformProcess::CreatePipe(PipeRead, PipeWrite)) { StatusText = TEXT("could not make a pipe for the microphone"); return false; }
	// A child process inherits every file the game has open. The first version started arecord directly: it held the
	// webcam (its light stayed on after the game had gone) and the reading end of its own pipe, so it never noticed
	// that nobody was listening and ran on for ever. The shell closes everything but the three standard streams
	// before it becomes arecord; with the pipe's other end gone, arecord's next write ends it.
	// (No quotes or backslashes inside the script: the engine's own splitting of the argument string knows double quotes only.)
	static const TCHAR* Start = TEXT("-c \"for f in /proc/$$/fd/*; do n=${f##*/}; [ $n -gt 2 ] && exec {n}>&-; done; exec /usr/bin/arecord -q -f S16_LE -r 16000 -c 1 -t raw --buffer-time=60000\"");
	if (!FPaths::FileExists(TEXT("/bin/bash"))) { FPlatformProcess::ClosePipe(PipeRead, PipeWrite); PipeRead = PipeWrite = nullptr; StatusText = TEXT("no bash on this machine"); return false; }
	Recorder = FPlatformProcess::CreateProc(TEXT("/bin/bash"), Start, false, true, true, nullptr, 0, nullptr, PipeWrite);
	if (!Recorder.IsValid()) { FPlatformProcess::ClosePipe(PipeRead, PipeWrite); PipeRead = PipeWrite = nullptr; StatusText = TEXT("arecord would not start"); return false; }
	bOpen = true;
	// Quitting does not run a game mode's destructor in time, if at all: stop the recorder when the engine shuts down.
	FCoreDelegates::OnEnginePreExit.AddRaw(this, &FKSMic::Close);
	StatusText = TEXT("microphone open (arecord, default device)");
	return true;
#else
	// The engine does not load a platform's capture back end by itself ("No Audio Capture implementations found").
#if PLATFORM_WINDOWS
	FModuleManager::Get().LoadModule(TEXT("AudioCaptureWasapi"));
#else
	FModuleManager::Get().LoadModule(TEXT("AudioCaptureRtAudio"));
#endif
	Capture = MakeUnique<Audio::FAudioCapture>();
	Audio::FCaptureDeviceInfo Info;
	if (!Capture->GetCaptureDeviceInfo(Info))
	{
		StatusText = TEXT("no microphone found");
		Capture.Reset();
		return false;
	}
	Audio::FAudioCaptureDeviceParams Params;
	// The callback is handed 32-bit float samples, all channels interleaved.
	Audio::FOnAudioCaptureFunction OnAudio = [this](const void* Audio, int32 Frames, int32 Channels, int32 SampleRate, double, bool)
	{
		Hear((const float*)Audio, Frames * FMath::Max(1, Channels), SampleRate * FMath::Max(1, Channels));
	};
	if (!Capture->OpenAudioCaptureStream(Params, MoveTemp(OnAudio), 1024) || !Capture->StartStream())
	{
		StatusText = FString::Printf(TEXT("microphone \"%s\" would not open"), *Info.DeviceName);
		Capture.Reset();
		return false;
	}
	bOpen = true;
	StatusText = FString::Printf(TEXT("microphone \"%s\" open"), *Info.DeviceName);
	return true;
#endif
}

void FKSMic::Poll()
{
	if (!PipeRead) return;
	if (!FPlatformProcess::IsProcRunning(Recorder)) { StatusText = TEXT("the microphone recorder stopped"); Close(); return; }
	TArray<uint8> Bytes;
	if (!FPlatformProcess::ReadPipeToArray(PipeRead, Bytes) || Bytes.Num() == 0) return;
	Left.Append(Bytes);
	const int32 Count = Left.Num() / 2;
	if (Count == 0) return;
	TArray<float> Samples; Samples.SetNumUninitialized(Count);
	const int16* Raw = (const int16*)Left.GetData();
	for (int32 i = 0; i < Count; i++) Samples[i] = Raw[i] / 32768.f;
	Hear(Samples.GetData(), Count, 16000);
	Left.RemoveAt(0, Count * 2, EAllowShrinking::No);
}

void FKSMic::Close()
{
	if (Capture) { Capture->AbortStream(); Capture.Reset(); }
	if (Recorder.IsValid()) { FPlatformProcess::TerminateProc(Recorder, true); FPlatformProcess::CloseProc(Recorder); }
	if (PipeRead) { FPlatformProcess::ClosePipe(PipeRead, PipeWrite); PipeRead = PipeWrite = nullptr; }
	Left.Reset();
	bOpen = false;
}

void FKSMic::Hear(const float* Samples, int32 Count, int32 SamplesPerSecond)
{
	if (!Samples || Count <= 0 || SamplesPerSecond <= 0) return;
	double Sum = 0.0;
	for (int32 i = 0; i < Count; i++) { const float S = FMath::Clamp(Samples[i], -1.f, 1.f); Sum += S * S; }
	const float Rms = FMath::Sqrt((float)(Sum / Count)), Seconds = (float)Count / SamplesPerSecond;
	Loud.store(FMath::Lerp(Loud.load(), Rms, FMath::Min(1.f, Seconds * 18.f)));
	// The room's own level (the game's music and crowd from the loudspeakers included) is followed slowly: quickly
	// downwards, slowly upwards, so a shout does not raise the bar it has to clear.
	Floor += (Rms - Floor) * FMath::Min(1.f, Seconds * (Rms < Floor ? 2.f : 0.25f));
	Floor = FMath::Max(Floor, 0.004f);
	const bool bLoud = Rms > FMath::Max(0.07f, Floor * 3.5f);
	if (bLoud) { LoudSeconds += Seconds; QuietSeconds = 0.f; }
	else { QuietSeconds += Seconds; if (QuietSeconds > 0.12f) LoudSeconds = 0.f; }
	// One shout, one event: it has to go quiet for a moment before the next one counts.
	if (!bLoud && QuietSeconds > 0.3f) bArmed = true;
	if (bArmed && LoudSeconds >= 0.2f) { bArmed = false; bShout.store(true); }
}
