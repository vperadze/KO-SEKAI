#include "KSTracking.h"
#include "HttpServerModule.h"
#include "IHttpRouter.h"
#include "HttpPath.h"
#include "HttpServerRequest.h"
#include "HttpServerResponse.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "KSCameraTracker.h"
#include "NNE.h"
#include "NNERuntimeCPU.h"
#include "NNEModelData.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/CommandLine.h"

DEFINE_LOG_CATEGORY_STATIC(LogKSTracking, Log, All);

void UKSTrackingSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	StartBuiltInTracker();
	FHttpServerModule& Http = FHttpServerModule::Get();
	Router = Http.GetHttpRouter(Port);
	if (!Router.IsValid())
	{
		UE_LOG(LogKSTracking, Warning, TEXT("Could not open port %d; webcam tracking is unavailable (keyboard still works)."), Port);
		return;
	}
	Route = Router->BindRoute(FHttpPath(TEXT("/pose")), EHttpServerRequestVerbs::VERB_POST | EHttpServerRequestVerbs::VERB_OPTIONS,
		FHttpRequestHandler::CreateUObject(this, &UKSTrackingSubsystem::HandlePose));
	Http.StartAllListeners();
	UE_LOG(LogKSTracking, Log, TEXT("Listening for the webcam tracker on http://127.0.0.1:%d/pose"), Port);
}

void UKSTrackingSubsystem::StartBuiltInTracker()
{
	if (FParse::Param(FCommandLine::Get(), TEXT("KSNoCamera")))
	{
		CameraStatus = TEXT("built-in camera switched off (-KSNoCamera)");
		return;
	}
	// The pose model is MediaPipe's pose landmark model converted to ONNX; it ships as a plain file beside the content.
	// The full model places the joints more precisely than the lite one and still runs in about 5 ms; lite is the fallback.
	FString ModelPath = FPaths::ProjectContentDir() / TEXT("KS/Tracker/pose_full.onnx");
	if (!FPaths::FileExists(ModelPath)) ModelPath = FPaths::ProjectContentDir() / TEXT("KS/Tracker/pose_lite.onnx");
	TArray<uint8> Bytes;
	if (!FFileHelper::LoadFileToArray(Bytes, *ModelPath))
	{
		CameraStatus = TEXT("pose model file missing");
		UE_LOG(LogKSTracking, Warning, TEXT("Built-in tracker: could not read %s"), *ModelPath);
		return;
	}
	TWeakInterfacePtr<INNERuntimeCPU> Runtime = UE::NNE::GetRuntime<INNERuntimeCPU>(TEXT("NNERuntimeORTCpu"));
	if (!Runtime.IsValid())
	{
		CameraStatus = TEXT("ONNX Runtime plugin not available");
		UE_LOG(LogKSTracking, Warning, TEXT("Built-in tracker: the NNERuntimeORTCpu runtime is not available."));
		return;
	}
	PoseModelData = NewObject<UNNEModelData>(this);
	PoseModelData->Init(TEXT("onnx"), Bytes);
	PoseModel = Runtime->CreateModelCPU(PoseModelData);
	TSharedPtr<UE::NNE::IModelInstanceCPU> Instance = PoseModel.IsValid() ? PoseModel->CreateModelInstanceCPU() : nullptr;
	if (!Instance.IsValid())
	{
		CameraStatus = TEXT("pose model could not be loaded");
		UE_LOG(LogKSTracking, Warning, TEXT("Built-in tracker: the pose model could not be created."));
		return;
	}
	int32 CameraIndex = -1;
	FParse::Value(FCommandLine::Get(), TEXT("KSCamera="), CameraIndex);
	CameraTracker = MakeUnique<FKSCameraTracker>(Instance, CameraIndex);
	TickHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateUObject(this, &UKSTrackingSubsystem::PullBuiltInTracker));
	UE_LOG(LogKSTracking, Log, TEXT("Built-in webcam tracker started."));
}

/** Runs on the game thread every frame: copies what the camera thread has worked out. */
bool UKSTrackingSubsystem::PullBuiltInTracker(float DeltaSeconds)
{
	if (!CameraTracker.IsValid()) return false;
	FKSTrackerState State;
	CameraTracker->Take(State);
	const double Now = FPlatformTime::Seconds();
	bBuiltInLive = State.bCameraOpen && Now - State.LastFrameSeconds < 0.5;
	CameraStatus = bBuiltInLive ? FString::Printf(TEXT("built-in camera, %.0f fps"), State.FramesPerSecond) : State.Status;
	if (!bBuiltInLive) return true;

	LastPacketTime = Now;
	bSeen = State.bSeen;
	bCalibrated = State.bCalibrated;
	bHandsOutOfView = State.bHandsOutOfView;
	CalProgress = State.CalProgress;
	for (int32 i = 0; i < 5; i++) Held[i] = State.Held[i];
	for (const FKSDetectedPunch& P : State.Punches)
	{
		if (Pending.Num() < 16) Pending.Add({ P.Arm, P.Type });
	}
	World = MoveTemp(State.World);
	Image = MoveTemp(State.Image);
	for (float Peak : State.StrokePeaks) if (StrokePeaks.Num() < 64) StrokePeaks.Add(Peak);
	return true;
}

void UKSTrackingSubsystem::SetSensitivity(float Value)
{
	if (CameraTracker.IsValid()) CameraTracker->SetSensitivity(Value);
}

void UKSTrackingSubsystem::Mark(const FString& Text)
{
	if (CameraTracker.IsValid()) CameraTracker->Mark(Text);
}
void UKSTrackingSubsystem::Recalibrate()
{
	if (CameraTracker.IsValid()) CameraTracker->Recalibrate();
}

void UKSTrackingSubsystem::Deinitialize()
{
	if (TickHandle.IsValid()) FTSTicker::GetCoreTicker().RemoveTicker(TickHandle);
	CameraTracker.Reset();   // stops the camera thread and releases the camera
	PoseModel.Reset();
	if (Router.IsValid() && Route.IsValid())
	{
		Router->UnbindRoute(Route);
	}
	Router.Reset();
	Super::Deinitialize();
}

bool UKSTrackingSubsystem::IsConnected() const
{
	return FPlatformTime::Seconds() - LastPacketTime < 0.5;
}

bool UKSTrackingSubsystem::IsHeld(EKSDefense Stance) const
{
	return IsTracking() && Held[(int32)Stance];
}

TArray<FKSPunchEvent> UKSTrackingSubsystem::TakePunches()
{
	TArray<FKSPunchEvent> Out = MoveTemp(Pending);
	Pending.Reset();
	return Out;
}

bool UKSTrackingSubsystem::HandlePose(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
{
	auto Reply = [&OnComplete](const TCHAR* Text)
	{
		TUniquePtr<FHttpServerResponse> Response = FHttpServerResponse::Create(FString(Text), TEXT("text/plain"));
		Response->Headers.Add(TEXT("Access-Control-Allow-Origin"), { TEXT("*") });
		Response->Headers.Add(TEXT("Access-Control-Allow-Headers"), { TEXT("*") });
		OnComplete(MoveTemp(Response));
	};
	if (Request.Verb == EHttpServerRequestVerbs::VERB_OPTIONS || Request.Body.Num() == 0)
	{
		Reply(TEXT("ok"));
		return true;
	}

	const FUTF8ToTCHAR Text((const ANSICHAR*)Request.Body.GetData(), Request.Body.Num());
	TSharedPtr<FJsonObject> Json;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(FString(Text.Length(), Text.Get()));
	if (!FJsonSerializer::Deserialize(Reader, Json) || !Json.IsValid())
	{
		Reply(TEXT("bad json"));
		return true;
	}

	if (bBuiltInLive)
	{
		Reply(TEXT("ok"));   // the built-in tracker has the camera; ignore the browser page
		return true;
	}
	LastPacketTime = FPlatformTime::Seconds();
	bSeen = Json->GetBoolField(TEXT("seen"));
	bCalibrated = Json->GetBoolField(TEXT("ready"));
	double Cal = -1.0;
	CalProgress = Json->TryGetNumberField(TEXT("cal"), Cal) ? (float)Cal : -1.f;

	for (bool& H : Held) { H = false; }
	const TArray<TSharedPtr<FJsonValue>>* HeldNames = nullptr;
	if (Json->TryGetArrayField(TEXT("held"), HeldNames))
	{
		for (const TSharedPtr<FJsonValue>& V : *HeldNames)
		{
			const FString Name = V->AsString();
			if (Name == TEXT("Block")) Held[(int32)EKSDefense::Block] = true;
			else if (Name == TEXT("Evade left")) Held[(int32)EKSDefense::EvadeLeft] = true;
			else if (Name == TEXT("Evade right")) Held[(int32)EKSDefense::EvadeRight] = true;
			else if (Name == TEXT("Duck")) Held[(int32)EKSDefense::Duck] = true;
		}
	}

	const TArray<TSharedPtr<FJsonValue>>* Events = nullptr;
	if (Json->TryGetArrayField(TEXT("events"), Events))
	{
		for (const TSharedPtr<FJsonValue>& V : *Events)
		{
			const TSharedPtr<FJsonObject>* E = nullptr;
			if (!V->TryGetObject(E)) continue;
			const FString Arm = (*E)->GetStringField(TEXT("arm")), Type = (*E)->GetStringField(TEXT("type"));
			FKSPunchEvent Punch;
			Punch.Arm = Arm == TEXT("Left") ? EKSArm::Left : EKSArm::Right;
			Punch.Type = Type == TEXT("hook") ? EKSPunch::Hook : Type == TEXT("uppercut") ? EKSPunch::Uppercut : EKSPunch::Straight;
			if (Pending.Num() < 16) Pending.Add(Punch);
		}
	}

	const TArray<TSharedPtr<FJsonValue>>* Lm = nullptr;
	if (Json->TryGetArrayField(TEXT("lm"), Lm) && Lm->Num() == NumLandmarks * 3)
	{
		World.SetNum(NumLandmarks);
		for (int32 i = 0; i < NumLandmarks; i++)
		{
			World[i] = FVector((*Lm)[i * 3]->AsNumber(), (*Lm)[i * 3 + 1]->AsNumber(), (*Lm)[i * 3 + 2]->AsNumber());
		}
	}
	else if (!bSeen)
	{
		World.Reset();
	}

	Reply(TEXT("ok"));
	return true;
}
