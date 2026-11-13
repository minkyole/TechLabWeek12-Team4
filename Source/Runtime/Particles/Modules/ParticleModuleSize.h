#pragma once

#include "Particles/ParticleModule.h"
#include "Particles/ParticleRange.h"

class UParticleModuleSize : public UParticleModule
{
	DECLARE_CLASS(UParticleModuleSize, UParticleModule)

public:
	UParticleModuleSize() = default;

	FParticleVectorRange StartSize = FParticleVectorRange(FVector(1.0f, 1.0f, 1.0f));

	// true이면 수명에 따라 BaseSize에서 EndSize로 선형 보간한다.
	bool bUseSizeOverLife = false;
	FVector EndSize = FVector(1.0f, 1.0f, 1.0f);
};
