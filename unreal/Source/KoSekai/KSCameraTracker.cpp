#include "KSCameraTracker.h"
#include "HAL/RunnableThread.h"
#include "HAL/PlatformProcess.h"
#include "NNERuntimeCPU.h"
#include "NNETypes.h"

#if WITH_OPENCV
#include "PreOpenCVHeaders.h"
#include "opencv2/core.hpp"
#include "opencv2/imgproc.hpp"
#include "opencv2/videoio.hpp"
#include "opencv2/imgcodecs.hpp"
#include "PostOpenCVHeaders.h"
#endif

DEFINE_LOG_CATEGORY_STATIC(LogKSCamera, Log, All);

namespace
{
	const int32 ModelSize = 256;          // the pose model looks at a 256 x 256 crop
	const int32 NumModelPoints = 39;      // 33 body landmarks plus 6 helpers; 33 and 34 give the next crop

	/** One Euro filter: steady when the value is still, quick to follow when it moves (the same filter MediaPipe uses). */
	struct FOneEuro
	{
		bool bInit = false;
		float X = 0.f, DX = 0.f;
		static float Alpha(float Cutoff, float Dt) { const float Tau = 1.f / (2.f * PI * Cutoff); return 1.f / (1.f + Tau / Dt); }
		float Filter(float Value, float Dt, float MinCutoff, float Beta, float VelocityScale)
		{
			if (!bInit || Dt <= 0.f) { bInit = true; X = Value; DX = 0.f; return X; }
			const float Raw = (Value - X) / Dt * VelocityScale;
			DX = FMath::Lerp(DX, Raw, Alpha(1.f, Dt));
			X = FMath::Lerp(X, Value, Alpha(MinCutoff + Beta * FMath::Abs(DX), Dt));
			return X;
		}
	};
}

FKSCameraTracker::FKSCameraTracker(TSharedPtr<UE::NNE::IModelInstanceCPU> InModel, int32 InCameraIndex)
	: Model(MoveTemp(InModel)), CameraIndex(InCameraIndex)
{
	Shared.Status = TEXT("starting camera");
	Thread = FRunnableThread::Create(this, TEXT("KSCameraTracker"), 0, TPri_AboveNormal);
}

FKSCameraTracker::~FKSCameraTracker()
{
	bStop = true;
	if (Thread)
	{
		Thread->WaitForCompletion();
		delete Thread;
		Thread = nullptr;
	}
}

void FKSCameraTracker::Take(FKSTrackerState& Out)
{
	FScopeLock Scope(&Lock);
	Out = Shared;
	Shared.Punches.Reset();
	Shared.StrokePeaks.Reset();
}

void FKSCameraTracker::Mark(const FString& Text)
{
	FScopeLock Scope(&Lock);
	Marks.Add(Text);
}

void FKSCameraTracker::SetStatus(const FString& Text)
{
	UE_LOG(LogKSCamera, Log, TEXT("%s"), *Text);
	FScopeLock Scope(&Lock);
	Shared.Status = Text;
}

uint32 FKSCameraTracker::Run()
{
	// -KSReplay=<recording>: run the move detector over a saved session as fast as possible and log what it decides.
	// Recordings are written by every webcam session (joint positions only, no pictures) to Saved/KSTrack.
	FString ReplayPath;
	if (FParse::Value(FCommandLine::Get(), TEXT("KSReplay="), ReplayPath))
	{
		TArray<FString> Lines;
		FFileHelper::LoadFileToStringArray(Lines, *ReplayPath);
		FKSDetector Replay;
		Replay.Trace = [](const FString& Line) { UE_LOG(LogKSCamera, Log, TEXT("REPLAY %s"), *Line); };
		int32 Frames = 0, Count = 0;
		for (const FString& Line : Lines)
		{
			TArray<FString> V;
			Line.ParseIntoArray(V, TEXT(","));
			if (V.Num() < 2 + 33 * 6) continue;
			FKSPoseFrame Pose;
			Pose.TimeMs = FCString::Atod(*V[0]); Pose.bSeen = V[1] == TEXT("1"); Pose.Aspect = 640.f / 480.f;
			for (int32 i = 0; i < 33; i++)
			{
				const int32 B = 2 + i * 6;
				Pose.Image[i] = FVector2f(FCString::Atof(*V[B]), FCString::Atof(*V[B + 1])); Pose.Visibility[i] = FCString::Atof(*V[B + 2]);
				Pose.World[i] = FVector3f(FCString::Atof(*V[B + 3]), FCString::Atof(*V[B + 4]), FCString::Atof(*V[B + 5]));
			}
			TArray<FKSDetectedPunch> Punches;
			Replay.Process(Pose, Punches);
			Frames++; Count += Punches.Num();
		}
		UE_LOG(LogKSCamera, Log, TEXT("REPLAY done: %d frames, %d punches"), Frames, Count);
		if (FParse::Param(FCommandLine::Get(), TEXT("KSReplayExit"))) FPlatformMisc::RequestExit(false);
		SetStatus(TEXT("replay finished"));
		return 0;
	}
#if WITH_OPENCV
	/* ---- open the camera ---- */
	cv::VideoCapture Camera;
	cv::Mat Frame;
	const int32 First = CameraIndex >= 0 ? CameraIndex : 0, Last = CameraIndex >= 0 ? CameraIndex : 3;
	int32 Opened = -1;
	// -KSCameraImage=<file>: run on one still picture instead of the camera, to test the tracker without a person.
	cv::Mat Still;
	FString StillPath;
	if (FParse::Value(FCommandLine::Get(), TEXT("KSCameraImage="), StillPath))
	{
		Still = cv::imread(TCHAR_TO_UTF8(*StillPath));
		if (!Still.empty()) { Frame = Still.clone(); Opened = 99; }
	}
	for (int32 Index = First; Index <= Last && Opened < 0 && !bStop; Index++)
	{
		if (!Camera.open(Index, cv::CAP_ANY)) continue;
		Camera.set(cv::CAP_PROP_FRAME_WIDTH, 640);
		Camera.set(cv::CAP_PROP_FRAME_HEIGHT, 480);
		// No MJPG: the engine's OpenCV build has no JPEG decoder, so compressed frames arrive black. Plain YUYV gives 30 frames/s at 640 x 480.
		Camera.set(cv::CAP_PROP_FPS, 30);
		// Two buffers: one being filled while one is being read. (A single buffer halves the frame rate, because the
		// camera has nowhere to put the next frame while this one is in use; more than two lets old frames pile up.)
		Camera.set(cv::CAP_PROP_BUFFERSIZE, 2);
		// Some devices list a second, picture-less node (metadata) and the first frames can be black while exposure settles.
		for (int32 Try = 0; Try < 15 && Opened < 0; Try++)
		{
			if (Camera.read(Frame) && !Frame.empty() && cv::mean(Frame)[0] + cv::mean(Frame)[1] + cv::mean(Frame)[2] > 6.0) Opened = Index;
		}
		if (Opened < 0) Camera.release();
	}
	if (Opened < 0)
	{
		SetStatus(TEXT("no camera found (is another program using it?)"));
		return 0;
	}
	SetStatus(FString::Printf(TEXT("camera %d open, %d x %d"), Opened, Frame.cols, Frame.rows));
	{
		FScopeLock Scope(&Lock);
		Shared.bCameraOpen = true;
	}

	/* ---- model buffers ---- */
	const UE::NNE::FTensorShape InputShape = UE::NNE::FTensorShape::Make({ 1u, (uint32)ModelSize, (uint32)ModelSize, 3u });
	if (Model->SetInputTensorShapes({ InputShape }) != UE::NNE::EResultStatus::Ok)
	{
		SetStatus(TEXT("the pose model rejected its input shape"));
		return 0;
	}
	const TConstArrayView<UE::NNE::FTensorShape> OutShapes = Model->GetOutputTensorShapes();
	TArray<TArray<float>> OutData;
	TArray<UE::NNE::FTensorBindingCPU> OutBindings;
	int32 LandmarksOut = -1, FlagOut = -1, WorldOut = -1;
	OutData.SetNum(OutShapes.Num());
	for (int32 i = 0; i < OutShapes.Num(); i++)
	{
		const int32 Count = (int32)OutShapes[i].Volume();
		OutData[i].SetNumZeroed(Count);
		OutBindings.Add({ OutData[i].GetData(), (uint64)Count * sizeof(float) });
		if (Count == NumModelPoints * 5) LandmarksOut = i;
		else if (Count == 1) FlagOut = i;
		else if (Count == NumModelPoints * 3) WorldOut = i;
	}
	if (LandmarksOut < 0 || FlagOut < 0 || WorldOut < 0)
	{
		SetStatus(TEXT("the pose model does not have the expected outputs"));
		return 0;
	}

	/* ---- the loop: one model run per camera frame ---- */
	FKSDetector Detector;
	// Every session is recorded as numbers (time, then 33 joints: image x, y, visibility, world x, y, z) with the
	// detector's decisions in between, so recognition can be tuned afterwards from what the player really did.
	IFileManager::Get().MakeDirectory(*(FPaths::ProjectSavedDir() / TEXT("KSTrack")), true);
	const FString RecPath = FPaths::ProjectSavedDir() / TEXT("KSTrack") / FString::Printf(TEXT("rec_%s.csv"), *FDateTime::Now().ToString(TEXT("%Y%m%d_%H%M%S")));
	TUniquePtr<FArchive> Rec(IFileManager::Get().CreateFileWriter(*RecPath));
	FString RecBuffer;
	Detector.Trace = [&RecBuffer](const FString& Line) { UE_LOG(LogKSCamera, Log, TEXT("%s"), *Line); RecBuffer += TEXT("# ") + Line + TEXT("\n"); };
	FOneEuro ImageFilter[33][2], WorldFilter[33][3];
	float SmoothVis[33];
	for (float& V : SmoothVis) V = 0.f;
	cv::Mat Crop, Rgb, Input;
	const float W = (float)Frame.cols, H = (float)Frame.rows;
	float RoiX = W / 2.f, RoiY = H, RoiSize = 1.4f * FMath::Max(W, H);   // first look: a body whose hips are at the bottom edge
	bool bLocked = false;
	int32 Misses = 0, FrameCount = 0, SearchFrames = 0, PunchCount = 0;
	float LowFraction = 0.f;      // how much of the recent time the arms were at or below the bottom edge
	float LastScore = 0.f;
	double LastT = 0.0, FpsFrom = FPlatformTime::Seconds(), ReadMs = 0.0, ModelMs = 0.0, LastLog = FPlatformTime::Seconds();
	float Fps = 0.f;

	while (!bStop)
	{
		const double ReadFrom = FPlatformTime::Seconds();
		if (!Still.empty()) { FPlatformProcess::Sleep(0.033f); Frame = Still.clone(); }
		else if (!Camera.read(Frame) || Frame.empty())
		{
			FPlatformProcess::Sleep(0.01f);
			continue;
		}
		// A frame that came back instantly was already waiting, so it is old: take the next one instead.
		for (int32 Skip = 0; Still.empty() && Skip < 2 && FPlatformTime::Seconds() - ReadFrom < 0.003; Skip++)
		{
			const double Again = FPlatformTime::Seconds();
			if (!Camera.read(Frame) || Frame.empty()) break;
			if (FPlatformTime::Seconds() - Again > 0.003) break;
		}
		const double NowSeconds = FPlatformTime::Seconds();
		ReadMs = FMath::Lerp(ReadMs, (NowSeconds - ReadFrom) * 1000.0, 0.1);
		const double T = NowSeconds * 1000.0;
		const float Dt = LastT > 0.0 ? (float)((T - LastT) / 1000.0) : 0.f;
		LastT = T;

		// Crop the region of interest to 256 x 256, as RGB floats in 0..1.
		const float Scale = ModelSize / RoiSize;
		const cv::Mat Warp = (cv::Mat_<float>(2, 3) << Scale, 0.f, ModelSize / 2.f - RoiX * Scale, 0.f, Scale, ModelSize / 2.f - RoiY * Scale);
		cv::warpAffine(Frame, Crop, Warp, cv::Size(ModelSize, ModelSize), cv::INTER_LINEAR, cv::BORDER_CONSTANT, cv::Scalar(0, 0, 0));
		cv::cvtColor(Crop, Rgb, cv::COLOR_BGR2RGB);
		Rgb.convertTo(Input, CV_32FC3, 1.0 / 255.0);
		const UE::NNE::FTensorBindingCPU InBinding = { Input.data, (uint64)ModelSize * ModelSize * 3 * sizeof(float) };
		if (Model->RunSync({ InBinding }, OutBindings) != UE::NNE::EResultStatus::Ok)
		{
			SetStatus(TEXT("the pose model failed to run"));
			break;
		}

		ModelMs = FMath::Lerp(ModelMs, (FPlatformTime::Seconds() - NowSeconds) * 1000.0, 0.1);
		if (NowSeconds - LastLog > 3.0)
		{
			LastLog = NowSeconds;
			UE_LOG(LogKSCamera, Log, TEXT("%.0f frames/s, waiting for a frame %.0f ms, crop + model %.0f ms, body %s (score %.2f), calibrated %d, punches so far %d"),
				Fps, ReadMs, ModelMs, bLocked ? TEXT("found") : TEXT("not found"), LastScore, Detector.IsCalibrated() ? 1 : 0, PunchCount);
			UE_LOG(LogKSCamera, Log, TEXT("  view %.0f %.0f %.0f, input mean %.3f, type %d, continuous %d, outputs %d/%d/%d, raw flag %g, shoulder %.0f %.0f"), RoiX, RoiY, RoiSize,
				(float)cv::mean(Input)[0], Input.type(), Input.isContinuous() ? 1 : 0, LandmarksOut, FlagOut, WorldOut, OutData[FlagOut][0], OutData[LandmarksOut][55], OutData[LandmarksOut][56]);
		}
		const float* Lm = OutData[LandmarksOut].GetData();
		const float* Wd = OutData[WorldOut].GetData();
		const float Score = OutData[FlagOut][0];
		LastScore = Score;
		// Lock on above 0.5; once locked, keep going down to 0.3 so a blurred punch does not drop the body.
		const bool bSeen = Score > (bLocked ? 0.3f : 0.5f);

		FKSPoseFrame Pose;
		Pose.TimeMs = T;
		Pose.Aspect = W / H;
		Pose.bSeen = bSeen;
		if (bSeen)
		{
			bLocked = true; Misses = 0;
			const float ObjectScale = RoiSize / FMath::Max(W, H);
			for (int32 i = 0; i < 33; i++)
			{
				const float Px = RoiX + (Lm[i * 5] - ModelSize / 2.f) / Scale, Py = RoiY + (Lm[i * 5 + 1] - ModelSize / 2.f) / Scale;
				Pose.Image[i].X = ImageFilter[i][0].Filter(Px / W, Dt, 0.05f, 80.f, 1.f / ObjectScale);
				Pose.Image[i].Y = ImageFilter[i][1].Filter(Py / H, Dt, 0.05f, 80.f, 1.f / ObjectScale);
				const float Vis = 1.f / (1.f + FMath::Exp(-Lm[i * 5 + 3]));
				SmoothVis[i] = Dt > 0.f ? FMath::Lerp(SmoothVis[i], Vis, 0.35f) : Vis;
				Pose.Visibility[i] = SmoothVis[i];
				Pose.World[i].X = WorldFilter[i][0].Filter(Wd[i * 3], Dt, 0.1f, 40.f, 1.f);
				Pose.World[i].Y = WorldFilter[i][1].Filter(Wd[i * 3 + 1], Dt, 0.1f, 40.f, 1.f);
				Pose.World[i].Z = WorldFilter[i][2].Filter(Wd[i * 3 + 2], Dt, 0.1f, 40.f, 1.f);
			}
			// Next frame's crop, MediaPipe's own rule: centred on helper point 33 (between the hips), sized from helper 34.
			const FVector2f Centre(RoiX + (Lm[33 * 5] - ModelSize / 2.f) / Scale, RoiY + (Lm[33 * 5 + 1] - ModelSize / 2.f) / Scale);
			const FVector2f Edge(RoiX + (Lm[34 * 5] - ModelSize / 2.f) / Scale, RoiY + (Lm[34 * 5 + 1] - ModelSize / 2.f) / Scale);
			const float Wanted = FMath::Clamp(FVector2f::Distance(Centre, Edge) * 2.f * 1.25f, 0.4f * FMath::Max(W, H), 3.f * FMath::Max(W, H));
			RoiX = FMath::Lerp(RoiX, Centre.X, 0.6f); RoiY = FMath::Lerp(RoiY, Centre.Y, 0.6f); RoiSize = FMath::Lerp(RoiSize, Wanted, 0.6f);
		}
		else
		{
			if (bLocked && ++Misses > 6)
			{
				// Lost: forget the old motion and search again.
				bLocked = false; SearchFrames = 0;
				for (int32 i = 0; i < 33; i++) { ImageFilter[i][0].bInit = ImageFilter[i][1].bInit = false; WorldFilter[i][0].bInit = WorldFilter[i][1].bInit = WorldFilter[i][2].bInit = false; }
			}
			if (!bLocked)
			{
				// Searching. The model is only sure of a body when its view is centred on the hips, and a player close to
				// a laptop camera has the hips below the picture: a look at the whole frame scores near zero. So try a few
				// starting views in turn, and from each follow the model's own guess of the hips for a few frames.
				const float Big = FMath::Max(W, H);
				static const float Guess[4][3] = { { 0.5f, 1.0f, 1.4f }, { 0.5f, 0.5f, 1.0f }, { 0.5f, 1.25f, 1.8f }, { 0.5f, 0.75f, 1.1f } };
				if (SearchFrames % 5 == 0)
				{
					const float* G = Guess[(SearchFrames / 5) % 4];
					RoiX = G[0] * W; RoiY = G[1] * H; RoiSize = G[2] * Big;
				}
				else if (Score > 0.05f)
				{
					const FVector2f Centre(RoiX + (Lm[33 * 5] - ModelSize / 2.f) / Scale, RoiY + (Lm[33 * 5 + 1] - ModelSize / 2.f) / Scale);
					const FVector2f Edge(RoiX + (Lm[34 * 5] - ModelSize / 2.f) / Scale, RoiY + (Lm[34 * 5 + 1] - ModelSize / 2.f) / Scale);
					RoiX = Centre.X; RoiY = Centre.Y;
					RoiSize = FMath::Clamp(FVector2f::Distance(Centre, Edge) * 2.f * 1.25f, 0.4f * Big, 3.f * Big);
				}
				SearchFrames++;
			}
		}

		if (bSeen)
		{
			// Good framing: shoulders in the upper half, elbows and fists inside the picture. A player too close to a
			// laptop camera shows only head and shoulders, and no detector can read punches from arms it cannot see.
			const float ShoulderY = (Pose.Image[11].Y + Pose.Image[12].Y) * 0.5f, ElbowY = FMath::Max(Pose.Image[13].Y, Pose.Image[14].Y);
			const bool bLow = ShoulderY > 0.55f || ElbowY > 0.97f;
			LowFraction = FMath::Lerp(LowFraction, bLow ? 1.f : 0.f, 0.03f);
		}
		if (Rec)
		{
			RecBuffer += FString::Printf(TEXT("%.1f,%d"), T, bSeen ? 1 : 0);
			for (int32 i = 0; i < 33; i++) RecBuffer += FString::Printf(TEXT(",%.4f,%.4f,%.2f,%.4f,%.4f,%.4f"), Pose.Image[i].X, Pose.Image[i].Y, Pose.Visibility[i], Pose.World[i].X, Pose.World[i].Y, Pose.World[i].Z);
			RecBuffer += TEXT("\n");
			if (RecBuffer.Len() > 30000) { const FTCHARToUTF8 Utf8(*RecBuffer); Rec->Serialize((void*)Utf8.Get(), Utf8.Length()); Rec->Flush(); RecBuffer.Reset(); }
		}
		if (bWantCalibrate.Exchange(false)) Detector.Calibrate();
		TArray<FKSDetectedPunch> Punches;
		Detector.Process(Pose, Punches, Sensitivity.Load());
		PunchCount += Punches.Num();

		FrameCount++;
		if (NowSeconds - FpsFrom >= 1.0) { Fps = FrameCount / (float)(NowSeconds - FpsFrom); FrameCount = 0; FpsFrom = NowSeconds; }

		FScopeLock Scope(&Lock);
		Shared.bSeen = bSeen;
		Shared.Image.Reset();
		if (bSeen) for (int32 i = 0; i < 33; i++) Shared.Image.Add(Pose.Image[i]);
		for (float Peak : Detector.EndedPeaks) if (Shared.StrokePeaks.Num() < 64) Shared.StrokePeaks.Add(Peak);
		Detector.EndedPeaks.Reset();
		for (const FString& M : Marks) RecBuffer += TEXT("# MARK ") + M + TEXT("\n");
		Marks.Reset();
		Shared.bCalibrated = Detector.IsCalibrated();
		Shared.bHandsOutOfView = LowFraction > (Shared.bHandsOutOfView ? 0.35f : 0.6f);
		Shared.CalProgress = Detector.CalibrationProgress();
		for (int32 i = 0; i < 5; i++) Shared.Held[i] = bSeen && Detector.IsHeld((EKSDefense)i);
		for (const FKSDetectedPunch& P : Punches) if (Shared.Punches.Num() < 16) Shared.Punches.Add(P);
		Shared.World.Reset();
		if (bSeen) for (int32 i = 0; i < 33; i++) Shared.World.Add(FVector(Pose.World[i].X, Pose.World[i].Y, Pose.World[i].Z));
		Shared.LastFrameSeconds = NowSeconds;
		Shared.FramesPerSecond = Fps;
	}
	if (Rec) { const FTCHARToUTF8 Utf8(*RecBuffer); Rec->Serialize((void*)Utf8.Get(), Utf8.Length()); Rec->Close(); }
	Camera.release();
	{
		FScopeLock Scope(&Lock);
		Shared.bCameraOpen = false;
	}
#else
	SetStatus(TEXT("this build has no camera support"));
#endif
	return 0;
}
