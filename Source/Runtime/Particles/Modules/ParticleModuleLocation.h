#pragma once

#include "Particles/ParticleModule.h"
#include "Particles/ParticleRange.h"

class UParticleModuleLocation : public UParticleModule
{
	DECLARE_CLASS(UParticleModuleLocation, UParticleModule)

public:
	UParticleModuleLocation() = default;

	// 스폰 시 에미터 원점 기준 위치 오프셋 범위
	FParticleVectorRange StartLocation;
};
