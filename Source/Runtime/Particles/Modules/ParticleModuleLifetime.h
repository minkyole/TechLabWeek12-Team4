#pragma once

#include "Particles/ParticleModule.h"
#include "Particles/ParticleRange.h"

// 파티클이 태어날 때 "이 파티클은 몇 초동안 살지"를 한 번 정해서, 파티클 구조체에 저장해 두는 일
class UParticleModuleLifetime : public UParticleModule
{
	DECLARE_CLASS(UParticleModuleLifetime, UParticleModule)

public:
	UParticleModuleLifetime() = default;

	void Spawn(FParticleEmitterInstance& Owner, int32 PayloadOffset, float SpawnTime, FBaseParticle& Particle) override;

	bool NeedsSpawn() const override { return true; }

	// 에미터가 동시에 살아 있을 수 있는 최대 파티클 수를 추정할 떄 사용
	float GetMaxLifetime() const
	{
		return Lifetime.Max;
	}

	// 초 단위 수명
	FParticleFloatRange Lifetime = FParticleFloatRange(1.0f);
};
