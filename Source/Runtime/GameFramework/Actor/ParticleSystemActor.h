#pragma once

#include "GameFramework/Actor.h"
#include "Components/ParticleSystemComponent.h"
#include "ParticleSystemActor.generated.h"

class UBillboardComponent;

// UE5의 AEmitter
UCLASS()
class AParticleSystemActor : public AActor
{
	GENERATED_BODY()
public:
	AParticleSystemActor();
	UParticleSystemComponent* ParticleSystemComponent = nullptr;
	UBillboardComponent* BillboardComponent = nullptr;
protected:

private:
	void SetupParticleRoot(UParticleSystemComponent* InParticleComponent, const FString& IconPath);

};