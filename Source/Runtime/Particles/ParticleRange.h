#pragma once

#include "Math/Vector.h"
#include "Math/Color.h"

// Min~Max 균일 랜덤 범위. Min == Max이면 상수로 취급한다.
struct FParticleFloatRange
{
	float Min = 0.0f;
	float Max = 0.0f;

	FParticleFloatRange() = default;
	FParticleFloatRange(float InMin, float InMax) : Min(InMin), Max(InMax) {}
	explicit FParticleFloatRange(float Constant) : Min(Constant), Max(Constant) {}
};

struct FParticleVectorRange
{
	FVector Min = FVector(0.0f, 0.0f, 0.0f);
	FVector Max = FVector(0.0f, 0.0f, 0.0f);

	FParticleVectorRange() = default;
	FParticleVectorRange(const FVector& InMin, const FVector& InMax) : Min(InMin), Max(InMax) {}
	explicit FParticleVectorRange(const FVector& Constant) : Min(Constant), Max(Constant) {}
};

struct FParticleColorRange
{
	FLinearColor Min = FLinearColor(1.0f, 1.0f, 1.0f, 1.0f);
	FLinearColor Max = FLinearColor(1.0f, 1.0f, 1.0f, 1.0f);

	FParticleColorRange() = default;
	FParticleColorRange(const FLinearColor& InMin, const FLinearColor& InMax) : Min(InMin), Max(InMax) {}
	explicit FParticleColorRange(const FLinearColor& Constant) : Min(Constant), Max(Constant) {}
};
