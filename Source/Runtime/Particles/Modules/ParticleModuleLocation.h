#pragma once

#include "Particles/ParticleModule.h"
#include "Particles/ParticleRange.h"

class UParticleModuleLocation : public UParticleModule
{
	DECLARE_CLASS(UParticleModuleLocation, UParticleModule)

public:
	UParticleModuleLocation() = default;

	void Spawn(FParticleEmitterInstance& Owner, int32 PayloadOffset, float SpawnTime, FBaseParticle& Particle) override;
	bool NeedsSpawn() const override { return true; }

	// 스폰 시 에미터 원점 기준 위치 오프셋 범위
	FParticleVectorRange StartLocation;
};
