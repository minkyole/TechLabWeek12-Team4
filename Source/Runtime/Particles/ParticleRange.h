#pragma once

#include "Math/Vector.h"
#include "Math/Color.h"
#include "Math/Noise.h"

// Min~Max 균일 랜덤 범위. Min == Max이면 상수로 취급한다.
struct FParticleFloatRange
{
	float Min = 0.0f;
	float Max = 0.0f;

	FParticleFloatRange() = default;
	FParticleFloatRange(float InMin, float InMax) : Min(InMin), Max(InMax) {}
	explicit FParticleFloatRange(float Constant) : Min(Constant), Max(Constant) {}

	float Sample() const
	{
		return FMath::FRandRange(Min, Max);
	}
};

struct FParticleVectorRange
{
	FVector Min = FVector(0.0f, 0.0f, 0.0f);
	FVector Max = FVector(0.0f, 0.0f, 0.0f);

	FParticleVectorRange() = default;
	FParticleVectorRange(const FVector& InMin, const FVector& InMax) : Min(InMin), Max(InMax) {}
	explicit FParticleVectorRange(const FVector& Constant) : Min(Constant), Max(Constant) {}

	// 축마다 독립적으로 뽑는다. Min~Max를 모서리로 하는 박스 안의 균일 분포다.
	FVector Sample() const
	{
		return FVector(
		    FMath::FRandRange(Min.X, Max.X),
		    FMath::FRandRange(Min.Y, Max.Y),
		    FMath::FRandRange(Min.Z, Max.Z));
	}
};

struct FParticleColorRange
{
	FLinearColor Min = FLinearColor(1.0f, 1.0f, 1.0f, 1.0f);
	FLinearColor Max = FLinearColor(1.0f, 1.0f, 1.0f, 1.0f);

	FParticleColorRange() = default;
	FParticleColorRange(const FLinearColor& InMin, const FLinearColor& InMax) : Min(InMin), Max(InMax) {}
	explicit FParticleColorRange(const FLinearColor& Constant) : Min(Constant), Max(Constant) {}

	// 난수 하나로 Min과 Max 사이를 보간한다. 채널마다 따로 뽑으면 두 색 사이가 아닌
	// 엉뚱한 색이 나오므로, 결과는 항상 Min~Max를 잇는 선분 위에 놓인다.
	FLinearColor Sample() const
	{
		const float T = FMath::FRand();
		return FLinearColor(
		    Min.R + (Max.R - Min.R) * T,
		    Min.G + (Max.G - Min.G) * T,
		    Min.B + (Max.B - Min.B) * T,
		    Min.A + (Max.A - Min.A) * T);
	}
};
