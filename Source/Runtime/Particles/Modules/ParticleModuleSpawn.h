#pragma once

#include "Particles/ParticleModule.h"
#include "Particles/ParticleRange.h"

struct FParticleBurst
{
	// 에미터 루프 시작 기준 시간(초)
	float Time = 0.0f;
	int32 Count = 0;
};

class UParticleModuleSpawn : public UParticleModule
{
	DECLARE_CLASS(UParticleModuleSpawn, UParticleModule)

public:
	UParticleModuleSpawn() = default;

	// 초당 스폰 개수
	float SpawnRate = 10.0f;

	TArray<FParticleBurst> Bursts;
};
