// Everything drawn over the 3D view: the start menu, health bars, cues, comic badges, speech bubbles, the VS card,
// result screens, fades and the webcam status. The pictures are Dreamina art in /Game/KS/UI (art/ui), the two fonts
// are Russo One and Bangers; without them every element falls back to plain boxes and the engine font.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "KSHUD.generated.h"

class AKSGameMode;
class UFont;
class UTexture2D;
struct FKSPopup;

enum class EKSFont : uint8 { Hud, Comic };

UCLASS()
class AKSHUD : public AHUD
{
	GENERATED_BODY()
public:
	virtual void DrawHUD() override;

private:
	/* ---- drawing tools. X, Y, W, H are fractions of the screen; Px sizes are pixels on a 720-pixel-high screen ---- */
	void LoadArt();
	UTexture2D* Tex(const TCHAR* Name) const;
	/** Text in one of the two fonts with a dark edge and drop shadow. Align: -1 left, 0 centred, 1 right. */
	void Text(const FString& Str, float X, float Y, float Px, const FLinearColor& Color, int32 Align = 0, EKSFont Font = EKSFont::Hud,
		float Edge = 2.f, const FLinearColor& EdgeColor = FLinearColor(0.04f, 0.f, 0.10f, 0.95f));
	FVector2D TextSize(const FString& Str, float Px, EKSFont Font) const;   // in screen pixels
	TArray<FString> Wrap(const FString& Str, float Px, EKSFont Font, float MaxPixels) const;
	void Label(const FString& Str, float X, float Y, float Size, const FLinearColor& Color, int32 Align = 0) { Text(Str, X, Y, Size * 20.f, Color, Align); }
	void Fill(float X, float Y, float W, float H, const FLinearColor& Color);
	/** A picture centred on X, Y and H high, as wide as its own shape makes it. False if the picture is missing. */
	bool Pic(const TCHAR* Name, float X, float Y, float H, float Alpha = 1.f, bool bFlip = false, const FLinearColor& Tint = FLinearColor::White);
	/** A picture stretched over a box with its ends (CapU, and CapV when not 0, as fractions of the picture) kept in shape. */
	bool Frame(const TCHAR* Name, float X, float Y, float W, float H, float CapU, float CapV, float CapPixels, float Alpha = 1.f, const FLinearColor& Tint = FLinearColor::White);
	void Panel(float X, float Y, float W, float H, float Alpha = 1.f);
	void Plate(float X, float Y, float W, float H, float Alpha = 1.f, const FLinearColor& Tint = FLinearColor::White);
	void Part(UTexture2D* Texture, float PX, float PY, float PW, float PH, float U0, float V0, float U1, float V1, const FLinearColor& Color);
	/** Everything drawn until PopSpin is scaled and turned about the point X, Y. */
	void PushSpin(float X, float Y, float Scale, float Degrees);
	void PopSpin();

	/* ---- the screens ---- */
	void HealthBar(float X, float W, float Fraction, float Trail, const FLinearColor& Color, bool bRightToLeft, float Jolt);
	void DrawMenu(const AKSGameMode& G);
	void DrawStreet(const AKSGameMode& G);
	void DrawFight(const AKSGameMode& G);
	void DrawCalibrate(const AKSGameMode& G);
	void DrawTracker(const AKSGameMode& G, bool bWithSkeleton);
	void DrawPopup(const AKSGameMode& G, const FKSPopup& P);
	void DrawAnnouncement(const AKSGameMode& G);
	void SpeechBubble(const AKSGameMode& G);
	void Subtitles(const AKSGameMode& G);

	UPROPERTY() TMap<FName, TObjectPtr<UTexture2D>> Textures;
	UPROPERTY() TObjectPtr<UFont> HudFont;
	UPROPERTY() TObjectPtr<UFont> ComicFont;
	bool bArtLoaded = false, bLoggedBubble = false, bLoggedDamage = false, bLoggedMenu = false;

	float SW = 1280.f, SH = 720.f, Unit = 1.f, Now = 0.f;
	// Small memories that drive the animation: when the current line, announcement and health values first appeared.
	FString BubbleLine, AnnounceText;
	float BubbleStart = 0.f, AnnounceStart = 0.f, AnnounceEnd = 0.f;
	float SeenPlayerHP = -1.f, SeenOppHP = -1.f, PlayerJoltAt = -10.f, OppJoltAt = -10.f;
	int32 SeenCombo = 0, SeenMenu = -1, SeenCount = -1;
	float ComboAt = -10.f, MenuAt = -10.f, CountAt = -10.f;
};
