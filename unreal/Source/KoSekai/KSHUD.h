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
		float Edge = 2.f, const FLinearColor& EdgeColor = FLinearColor(0.04f, 0.f, 0.10f, 0.95f), float MaxW = 0.f);
	/** The size at which Str is no wider than MaxW (a fraction of the screen width): Px, or smaller. */
	float Fit(const FString& Str, float Px, EKSFont Font, float MaxW) const;
	/** Words wrapped into at most MaxLines lines no wider than MaxW, centred on X, Y; the writing shrinks until it fits.
	 *  False if even the smallest size does not fit. Everything written inside a box goes through this or MaxW. */
	bool Lines(const FString& Str, float X, float Y, float MaxW, int32 MaxLines, float Px, const FLinearColor& Color, EKSFont Font = EKSFont::Hud,
		float Edge = 2.f, const FLinearColor& EdgeColor = FLinearColor(0.04f, 0.f, 0.10f, 0.95f));
	FVector2D TextSize(const FString& Str, float Px, EKSFont Font) const;   // in screen pixels
	TArray<FString> Wrap(const FString& Str, float Px, EKSFont Font, float MaxPixels) const;
	void Label(const FString& Str, float X, float Y, float Size, const FLinearColor& Color, int32 Align = 0, float MaxW = 0.f)
	{
		Text(Str, X, Y, Size * 20.f, Color, Align, EKSFont::Hud, 2.f, FLinearColor(0.04f, 0.f, 0.10f, 0.95f), MaxW);
	}
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

	/* ---- where things go. Badges and bubbles look for room beside the fighters instead of landing on them. Pixels. ---- */
	/** Starts the frame's layout: where the two fighters and their heads are on screen, and which badges are still up. */
	void BeginLayout(const AKSGameMode& G);
	/** Marks a part of the screen (fractions) as used by something fixed: a badge or bubble will not be put there. */
	void Reserve(float X, float Y, float W, float H);
	/** How bad it is to put something in Box: what it would cover of the heads, bodies, fixed pieces and other badges. */
	float SpotCost(const FBox2D& Box, int32 Self) const;
	/** The centre of the best place for something of Size, as near to Want as the fighters and everything else allow. */
	FVector2D FindSpot(const FVector2D& Want, const FVector2D& Size, int32 Self, float* OutCost = nullptr) const;
	void PlaceBubble(const AKSGameMode& G);

	/* ---- the screens ---- */
	void HealthBar(float X, float W, float Fraction, float Trail, const FLinearColor& Color, bool bRightToLeft, float Jolt);
	void DrawMenu(const AKSGameMode& G);
	void DrawIntro(const AKSGameMode& G);     // the opening film, shown frame by frame from sheets (KSCineData.h)
	void DrawStreet(const AKSGameMode& G);
	void DrawFight(const AKSGameMode& G);
	void DrawCalibrate(const AKSGameMode& G);
	void DrawTracker(const AKSGameMode& G, bool bWithSkeleton);
	void DrawPopup(const AKSGameMode& G, const FKSPopup& P);
	void DrawAnnouncement(const AKSGameMode& G);
	void SpeechBubble(const AKSGameMode& G);
	void SpeechCaption(const AKSGameMode& G);
	void Subtitles(const AKSGameMode& G);

	UPROPERTY() TMap<FName, TObjectPtr<UTexture2D>> Textures;
	UPROPERTY() TObjectPtr<UFont> HudFont;
	UPROPERTY() TArray<TObjectPtr<UTexture2D>> CineSheets;
	bool bLoggedIntro = false;
	float HandsOutSince = -1.f;               // when the webcam last lost the player's hands; the STEP BACK plate waits a moment
	UPROPERTY() TObjectPtr<UFont> ComicFont;
	bool bArtLoaded = false, bLoggedBubble = false, bLoggedDamage = false, bLoggedMenu = false, bLoggedLayout = false, bLoggedCal = false;

	struct FPlacedBadge { float Start; FString Text; FVector2D At, Size; };
	TArray<FPlacedBadge> Placed;              // the badges on screen and where each was put when it appeared
	TArray<FBox2D> Bodies, Heads, Taken;      // this frame: the fighters, their heads, the fixed pieces
	FBox2D Region;                            // where badges and bubbles may go at all
	FVector2D BubbleAt = FVector2D::ZeroVector, BubbleGoal = FVector2D::ZeroVector, BubbleSize = FVector2D::ZeroVector, BubbleHead = FVector2D::ZeroVector;
	bool bBubbleSet = false, bCalTextOutside = false;
	int32 BadgesPlaced = 0, BadgesOnHeads = 0;
	float WorstCover = 0.f;

	float SW = 1280.f, SH = 720.f, Unit = 1.f, Now = 0.f;
	// Small memories that drive the animation: when the current line, announcement and health values first appeared.
	FString BubbleLine, AnnounceText;
	float BubbleStart = 0.f, AnnounceStart = 0.f, AnnounceEnd = 0.f;
	float SeenPlayerHP = -1.f, SeenOppHP = -1.f, PlayerJoltAt = -10.f, OppJoltAt = -10.f;
	int32 SeenCombo = 0, SeenMenu = -1, SeenCount = -1;
	float ComboAt = -10.f, MenuAt = -10.f, CountAt = -10.f;
};
