#pragma once

#include "PrimitiveComponent.h"
#include "Particles/ParticleEmitterInstance.h"
#include "ParticleSystemComponent.generated.h"

 UCLASS()
class UParticleSystemComponent : public UPrimitiveComponent
{
	GENERATED_BODY()
public:
	UParticleSystemComponent();
	~UParticleSystemComponent();
	virtual void TickComponent(float DeltaTime) override;

	TArray<FParticleEmitterInstance*> EmitterInstances;
	//UParticleSystem* Template;

	//TArray<FDynamicEmitterDataBase*> EmitterRenderData;
protected:

};