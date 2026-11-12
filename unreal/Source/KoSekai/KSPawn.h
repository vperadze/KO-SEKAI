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

private:
	UPROPERTY() TObjectPtr<UCameraComponent> Camera;
};
