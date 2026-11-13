// The link between two players: one TCP connection carrying short lines of text.
//
// Three ways to get connected, all speaking the same lines afterwards:
//   - Host():  this game listens; the other player joins by this machine's address (same network, VPN or forwarded port).
//   - Join():  connect to a host's address.
//   - Join():  connect to the relay server (tools/ko_relay.py), which pairs two players and passes their lines on.
//              Nobody needs an open port for that except the relay.
// Whoever is told "ROLE host" runs the match (picks the fighters, rings the bell, calls time); the other is the guest.
// A game that hosts gives itself the host role and tells the visitor "ROLE guest"; the relay hands the roles out.
#pragma once

#include "CoreMinimal.h"
#include "Async/Future.h"
#include <atomic>

class FSocket;

class FKSNet
{
public:
	enum class EState : uint8
	{
		Off,
		Listening,     // hosting, nobody has come yet
		Connecting,    // dialling
		Waiting,       // connected to the relay, which has no opponent for us yet
		Connected,     // an opponent is on the line
		Failed         // see Error()
	};
	static constexpr int32 DefaultPort = 7777;

	~FKSNet();
	bool Host(int32 Port);
	/** Address is "name", "name:port" or either with "#room" on the end (the relay pairs players who give the same room). */
	void Join(const FString& Address);
	void Close();

	/** Once a frame: lets a visitor in, finishes dialling, sends what is waiting and hands back the lines that arrived. */
	void Tick(TArray<FString>& OutLines);
	void Send(const FString& Line);

	EState State() const { return Now; }
	bool IsHost() const { return bHost; }
	const FString& Error() const { return Problem; }
	/** Seconds since the other side was last heard from. */
	double Silence() const;
	/** This machine's own addresses, for the host to read out. */
	static TArray<FString> LocalAddresses();

private:
	struct FDialled { FSocket* Socket = nullptr; FString Error; };
	void Adopt(FSocket* Fresh);
	void Fail(const FString& Why);
	void Flush();

	EState Now = EState::Off;
	bool bHost = false;
	FString Problem, Room;
	FSocket* Listener = nullptr;
	FSocket* Socket = nullptr;
	TFuture<FDialled> Dialling;
	TSharedPtr<std::atomic<bool>, ESPMode::ThreadSafe> DialCancelled;
	TArray<uint8> In, Out;
	double LastHeard = 0.0;
};
