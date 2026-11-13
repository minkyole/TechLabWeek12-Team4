#pragma once

#include "Math/Vector.h"
#include "Math/Color.h"

enum class EDynamicEmitterType : uint8
{
	Sprite,
	Mesh,
};

enum class EParticleSortMode : uint8
{
	None,
	DistanceToView,
	ViewDepth,
};

struct FBaseParticle
{
	FVector Location;
	float RelativeTime; // 0~1, 1 이상이면 사망
	FVector OldLocation;
	float OneOverMaxLifetime; // 1 / Lifetime
	FVector Velocity;
	float Rotation; // 라디안
	FVector BaseVelocity;
	float RotationRate;
	FVector Size;
	float BaseRotationRate;
	FVector BaseSize;
	int32 Flags;
	FLinearColor Color;
	FLinearColor BaseColor;
};
