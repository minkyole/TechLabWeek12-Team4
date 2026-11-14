#include "EnginePCH.h"
#include "Particles/Modules/ParticleModuleVelocity.h"

void UParticleModuleVelocity::Spawn(FParticleEmitterInstance& Owner, int32 PayloadOffset, float SpawnTime, FBaseParticle& Particle)
{
	const FVector V = StartVelocity.Sample();
	Particle.Velocity += V;
	Particle.BaseVelocity += V;
}
