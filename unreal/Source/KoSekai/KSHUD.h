// Everything drawn over the 3D view: health bars, cues, the VS card, subtitles, fades and the webcam status.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "KSHUD.generated.h"

class AKSGameMode;

UCLASS()
class AKSHUD : public AHUD
{
	GENERATED_BODY()
public:
	virtual void DrawHUD() override;

private:
	// X and Y are fractions of the screen; Size is relative to a 720-pixel-high screen.
	void Label(const FString& Text, float X, float Y, float Size, const FLinearColor& Color, int32 Align = 0);
	void Fill(float X, float Y, float W, float H, const FLinearColor& Color);
	void HealthBar(float X, float W, float Fraction, float Trail, const FLinearColor& Color, bool bRightToLeft);
	void DrawStreet(const AKSGameMode& G);
	void DrawFight(const AKSGameMode& G);
	void DrawTracker(const AKSGameMode& G, bool bWithSkeleton);
	void Subtitles(const AKSGameMode& G);

	float SW = 1280.f, SH = 720.f, Unit = 1.f;
};
