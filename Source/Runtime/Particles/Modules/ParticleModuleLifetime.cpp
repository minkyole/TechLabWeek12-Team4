#include "EnginePCH.h"
#include "Particles/Modules/ParticleModuleLifetime.h"

void UParticleModuleLifetime::Spawn(FParticleEmitterInstance& Owner, int32 PayloadOffset, float SpawnTime, FBaseParticle& Particle)
{
	constexpr float MinLifetime = 0.001f;
	float TotalLifetime = std::max(Lifetime.Sample(), MinLifetime);

	// 다른 Lifetime 모듈이 이미 값을 넣었으면 수명(초)끼리 합산
	if (Particle.OneOverMaxLifetime > 0.0f)
	{
		TotalLifetime += 1.0f / Particle.OneOverMaxLifetime;
	}

	Particle.OneOverMaxLifetime = 1.0f / TotalLifetime;
	Particle.RelativeTime = SpawnTime * Particle.OneOverMaxLifetime;
}
