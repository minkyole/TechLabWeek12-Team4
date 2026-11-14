#include "EnginePCH.h"
#include "ParticleSystemComponent.h"

UParticleSystemComponent::UParticleSystemComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
}

UParticleSystemComponent::~UParticleSystemComponent()
{
	for (auto* EmitterInstance : EmitterInstances)
	{
		delete EmitterInstance;
	}
	EmitterInstances.Empty();
}

void UParticleSystemComponent::TickComponent(float DeltaTime)
{
	Super::TickComponent(DeltaTime);

	if (EmitterInstances.IsEmpty())
	{
		FParticleEmitterInstance* ParticleEmitterInstance = new FParticleSpriteEmitterInstance();
		ParticleEmitterInstance->Component = this;
		ParticleEmitterInstance->Init(100, sizeof(FBaseParticle));
		EmitterInstances.Add(ParticleEmitterInstance);
	}
	
	for (auto* EmitterInstance : EmitterInstances)
	{
		EmitterInstance->Tick(DeltaTime);
	}

}