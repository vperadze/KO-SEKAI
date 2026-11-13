#include "KSNet.h"
#include "Sockets.h"
#include "SocketSubsystem.h"
#include "SocketTypes.h"
#include "IPAddress.h"
#include "AddressInfoTypes.h"
#include "Async/Async.h"
#include "HAL/PlatformTime.h"

DEFINE_LOG_CATEGORY_STATIC(LogKSNet, Log, All);

namespace
{
	ISocketSubsystem* Sockets() { return ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM); }
	void Destroy(FSocket*& S)
	{
		if (!S) return;
		S->Close();
		if (ISocketSubsystem* SS = Sockets()) SS->DestroySocket(S);
		S = nullptr;
	}
}

FKSNet::~FKSNet() { Close(); }

void FKSNet::Close()
{
	// A dial still under way cannot be stopped; it is told to throw its socket away when it gets one.
	if (DialCancelled.IsValid()) { DialCancelled->store(true); DialCancelled.Reset(); }
	Dialling = TFuture<FDialled>();
	Destroy(Socket);
	Destroy(Listener);
	In.Reset(); Out.Reset();
	Now = EState::Off;
}

void FKSNet::Fail(const FString& Why)
{
	Close();
	Problem = Why;
	Now = EState::Failed;
	UE_LOG(LogKSNet, Display, TEXT("%s"), *Why);
}

bool FKSNet::Host(int32 Port)
{
	Close();
	Problem.Reset();
	ISocketSubsystem* SS = Sockets();
	Listener = SS ? SS->CreateSocket(NAME_Stream, TEXT("KSNet listener"), FName(FNetworkProtocolTypes::IPv4)) : nullptr;
	if (!Listener) { Fail(TEXT("This machine cannot open a network socket.")); return false; }
	TSharedRef<FInternetAddr> Addr = SS->CreateInternetAddr();
	Addr->SetAnyAddress();
	Addr->SetPort(Port);
	Listener->SetReuseAddr(true);
	Listener->SetNonBlocking(true);
	if (!Listener->Bind(*Addr) || !Listener->Listen(1))
	{
		Fail(FString::Printf(TEXT("Port %d is taken. Is another copy of the game hosting?"), Port));
		return false;
	}
	bHost = true;
	Now = EState::Listening;
	UE_LOG(LogKSNet, Display, TEXT("Hosting on port %d"), Port);
	return true;
}

void FKSNet::Join(const FString& Address)
{
	Close();
	Problem.Reset();
	FString Name = Address.TrimStartAndEnd();
	Room = TEXT("-");
	int32 Hash;
	if (Name.FindChar(TEXT('#'), Hash)) { Room = Name.Mid(Hash + 1).TrimStartAndEnd(); Name = Name.Left(Hash); if (Room.IsEmpty()) Room = TEXT("-"); }
	int32 Port = DefaultPort, Colon;
	if (Name.FindLastChar(TEXT(':'), Colon)) { Port = FCString::Atoi(*Name.Mid(Colon + 1)); Name = Name.Left(Colon); if (Port <= 0 || Port > 65535) Port = DefaultPort; }
	if (Name.IsEmpty()) { Fail(TEXT("No address given.")); return; }

	bHost = false;
	Now = EState::Connecting;
	DialCancelled = MakeShared<std::atomic<bool>, ESPMode::ThreadSafe>(false);
	TSharedPtr<std::atomic<bool>, ESPMode::ThreadSafe> Cancelled = DialCancelled;
	// Looking a name up and dialling can each block for seconds, so they happen off the game thread.
	Dialling = Async(EAsyncExecution::Thread, [Name, Port, Cancelled]() -> FDialled
	{
		FDialled Result;
		ISocketSubsystem* SS = Sockets();
		if (!SS) { Result.Error = TEXT("This machine cannot open a network socket."); return Result; }
		const FAddressInfoResult Found = SS->GetAddressInfo(*Name, nullptr, EAddressInfoFlags::Default, FName(FNetworkProtocolTypes::IPv4), SOCKTYPE_Streaming);
		if (Found.ReturnCode != SE_NO_ERROR || Found.Results.Num() == 0) { Result.Error = FString::Printf(TEXT("Nothing is known by the name %s."), *Name); return Result; }
		TSharedRef<FInternetAddr> Addr = Found.Results[0].Address->Clone();
		Addr->SetPort(Port);
		FSocket* S = SS->CreateSocket(NAME_Stream, TEXT("KSNet"), Addr->GetProtocolType());
		if (!S) { Result.Error = TEXT("This machine cannot open a network socket."); return Result; }
		if (!S->Connect(*Addr))
		{
			Result.Error = FString::Printf(TEXT("Nobody answers at %s port %d."), *Name, Port);
			S->Close(); SS->DestroySocket(S);
			return Result;
		}
		if (Cancelled->load()) { S->Close(); SS->DestroySocket(S); return Result; }
		Result.Socket = S;
		return Result;
	});
}

void FKSNet::Adopt(FSocket* Fresh)
{
	Socket = Fresh;
	Socket->SetNonBlocking(true);
	Socket->SetNoDelay(true);
	In.Reset(); Out.Reset();
	LastHeard = FPlatformTime::Seconds();
}

double FKSNet::Silence() const { return Socket ? FPlatformTime::Seconds() - LastHeard : 0.0; }

void FKSNet::Send(const FString& Line)
{
	if (!Socket) return;
	const FTCHARToUTF8 Utf8(*Line);
	Out.Append((const uint8*)Utf8.Get(), Utf8.Length());
	Out.Add((uint8)'\n');
	Flush();
}

void FKSNet::Flush()
{
	while (Socket && Out.Num() > 0)
	{
		int32 Sent = 0;
		if (!Socket->Send(Out.GetData(), Out.Num(), Sent) || Sent <= 0) break;   // full for now: the rest goes next frame
		Out.RemoveAt(0, Sent, EAllowShrinking::No);
	}
}

void FKSNet::Tick(TArray<FString>& OutLines)
{
	if (Now == EState::Listening && Listener)
	{
		bool bPending = false;
		if (Listener->HasPendingConnection(bPending) && bPending)
		{
			if (FSocket* Visitor = Listener->Accept(TEXT("KSNet visitor")))
			{
				Destroy(Listener);    // two players: the door closes behind the first visitor
				Adopt(Visitor);
				Send(TEXT("ROLE guest"));
				Now = EState::Connected;
				UE_LOG(LogKSNet, Display, TEXT("A player joined."));
			}
		}
	}
	if (Now == EState::Connecting && Dialling.IsValid() && Dialling.IsReady())
	{
		FDialled Result = Dialling.Get();
		Dialling = TFuture<FDialled>();
		DialCancelled.Reset();
		if (!Result.Socket) { Fail(Result.Error.IsEmpty() ? TEXT("Could not connect.") : Result.Error); return; }
		Adopt(Result.Socket);
		// The relay reads this line to learn the room. A hosting game ignores it.
		Send(FString::Printf(TEXT("HELLO 1 %s"), *Room));
		Now = EState::Waiting;
		UE_LOG(LogKSNet, Display, TEXT("Connected; waiting to be given a role."));
	}
	if (!Socket) return;

	Flush();
	uint8 Buffer[2048];
	for (;;)
	{
		int32 Read = 0;
		// False means the other side closed the connection (or it broke); true with nothing read means nothing has come.
		if (!Socket->Recv(Buffer, sizeof(Buffer), Read)) { Fail(Now == EState::Connected ? TEXT("The opponent left.") : TEXT("The connection was closed.")); return; }
		if (Read <= 0) break;
		In.Append(Buffer, Read);
		LastHeard = FPlatformTime::Seconds();
	}
	int32 Start = 0;
	for (int32 i = 0; i < In.Num(); i++)
	{
		if (In[i] != (uint8)'\n') continue;
		const FUTF8ToTCHAR Wide((const ANSICHAR*)In.GetData() + Start, i - Start);
		FString Line(Wide.Length(), Wide.Get());
		Line.TrimStartAndEndInline();
		Start = i + 1;
		if (Line.IsEmpty()) continue;
		// Who runs the match is settled here; everything else is the game's business.
		if (Line.StartsWith(TEXT("ROLE ")))
		{
			bHost = Line.Mid(5) == TEXT("host");
			Now = EState::Connected;
			UE_LOG(LogKSNet, Display, TEXT("An opponent is on the line; this game is the %s."), bHost ? TEXT("host") : TEXT("guest"));
			continue;
		}
		if (Line == TEXT("WAIT") || Line.StartsWith(TEXT("HELLO "))) continue;
		if (Now == EState::Connected) OutLines.Add(MoveTemp(Line));
	}
	if (Start > 0) In.RemoveAt(0, Start, EAllowShrinking::No);
	if (In.Num() > 65536) Fail(TEXT("The connection sent nonsense."));
}

TArray<FString> FKSNet::LocalAddresses()
{
	TArray<FString> Out;
	TArray<TSharedPtr<FInternetAddr>> Found;
	ISocketSubsystem* SS = Sockets();
	if (SS && SS->GetLocalAdapterAddresses(Found))
	{
		for (const TSharedPtr<FInternetAddr>& Addr : Found)
		{
			if (!Addr.IsValid() || Addr->GetProtocolType() != FName(FNetworkProtocolTypes::IPv4)) continue;
			const FString Text = Addr->ToString(false);
			if (Text.StartsWith(TEXT("127.")) || Text.StartsWith(TEXT("169.254."))) continue;
			Out.AddUnique(Text);
		}
	}
	return Out;
}
