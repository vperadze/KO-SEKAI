#include "KSTracking.h"
#include "HttpServerModule.h"
#include "IHttpRouter.h"
#include "HttpPath.h"
#include "HttpServerRequest.h"
#include "HttpServerResponse.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

DEFINE_LOG_CATEGORY_STATIC(LogKSTracking, Log, All);

void UKSTrackingSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
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

void UKSTrackingSubsystem::Deinitialize()
{
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
