#pragma once

#include "Particles/ParticleModule.h"
#include "Particles/ParticleRange.h"

class UParticleModuleVelocity : public UParticleModule
{
	DECLARE_CLASS(UParticleModuleVelocity, UParticleModule)

public:
	UParticleModuleVelocity() = default;

	FParticleVectorRange StartVelocity;
};
