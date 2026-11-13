#pragma once

#include "Particles/ParticleModule.h"
#include "Particles/ParticleRange.h"

class UParticleModuleLifetime : public UParticleModule
{
	DECLARE_CLASS(UParticleModuleLifetime, UParticleModule)

public:
	UParticleModuleLifetime() = default;

	// 초 단위 수명
	FParticleFloatRange Lifetime = FParticleFloatRange(1.0f);
};
