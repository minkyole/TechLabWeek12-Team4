#include "EnginePCH.h"
#include "Particles/Modules/ParticleModuleLocation.h"

void UParticleModuleLocation::Spawn(FParticleEmitterInstance& Owner, int32 PayloadOffset, float SpawnTime, FBaseParticle& Particle)
{
	Particle.Location += StartLocation.Sample();
}
