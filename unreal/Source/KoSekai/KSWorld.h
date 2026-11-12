// Builds the two places in the game out of engine cubes, lights and text: the night street with the last
// cabinet, and the arena inside it. Everything is spawned from code, so there are no level files to merge.
#pragma once

#include "CoreMinimal.h"

class UStaticMesh;
class UMaterialInterface;
class UMaterialInstanceDynamic;
class UTextureCube;
class APointLight;
class ATextRenderActor;
class AKSDummy;

struct FKSBuilder
{
	UWorld* World = nullptr;
	TArray<TObjectPtr<AActor>>* Out = nullptr;
	UStaticMesh* Cube = nullptr;
	UStaticMesh* Sphere = nullptr;
	UStaticMesh* Cylinder = nullptr;
	UMaterialInterface* Solid = nullptr;
	UTextureCube* SkyCube = nullptr;

	UMaterialInstanceDynamic* Shape(UStaticMesh* Mesh, const FVector& Center, const FVector& Size, const FLinearColor& Color, float Glow = 0.f, const FRotator& Rot = FRotator::ZeroRotator, bool bShadow = false);
	UMaterialInstanceDynamic* Box(const FVector& Center, const FVector& Size, const FLinearColor& Color, float Glow = 0.f, const FRotator& Rot = FRotator::ZeroRotator, bool bShadow = false)
	{
		return Shape(Cube, Center, Size, Color, Glow, Rot, bShadow);
	}
	/** A flat picture: Across and Down are the world directions of the picture's width and height. UV = (scale U, scale V, offset U, offset V). */
	UMaterialInstanceDynamic* Image(const TCHAR* Texture, bool bLit, const FVector& Center, const FVector& Across, const FVector& Down, float Width, float Height, const FVector4& UV = FVector4(1, 1, 0, 0), float Glow = 1.f, const TCHAR* Material = nullptr);
	APointLight* Light(const FVector& Pos, const FLinearColor& Color, float Candelas, float Radius, bool bShadows = false);
	ATextRenderActor* Text(const FVector& Pos, const FRotator& Rot, const FString& String, const FLinearColor& Color, float Size);
	void Ambient(const FLinearColor& Color, float Intensity, const FLinearColor& FogColor, float FogDensity);
};

struct FKSStreet
{
	static constexpr float CabinetX = 3300.f;
	FVector ScreenPos = FVector(CabinetX - 42.f, 0.f, 128.f);
	TObjectPtr<UMaterialInstanceDynamic> Screen;
	TObjectPtr<UMaterialInstanceDynamic> Eye;
	TObjectPtr<APointLight> ScreenLight;
	TObjectPtr<APointLight> EyeLight;
	TObjectPtr<ATextRenderActor> ScreenText;
};

struct FKSArena
{
	TObjectPtr<ATextRenderActor> NameSign;
	TArray<TObjectPtr<AKSDummy>> Crowd;
};

namespace KSWorld
{
	/** bDead builds the street after the ending: the cabinet is switched off. */
	void BuildStreet(FKSBuilder& B, FKSStreet& Street, bool bDead);
	void BuildArena(FKSBuilder& B, FKSArena& Arena);
}
