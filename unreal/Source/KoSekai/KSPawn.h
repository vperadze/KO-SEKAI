// The player's eyes. It is only a camera; KSGameMode decides where it is (walking the street, being pulled
// into the cabinet, standing behind the player's fighter).
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"
#include "KSPawn.generated.h"

class UCameraComponent;

UCLASS()
class AKSPawn : public APawn
{
	GENERATED_BODY()
public:
	AKSPawn();
	void SetView(const FVector& Location, const FRotator& Rotation, float FieldOfView);
	/** The picture's colour cast and how far colours split apart towards the edges (an old lens), per arena. */
	void SetLook(const FLinearColor& Tint, float Fringe);

private:
	UPROPERTY() TObjectPtr<UCameraComponent> Camera;
};
