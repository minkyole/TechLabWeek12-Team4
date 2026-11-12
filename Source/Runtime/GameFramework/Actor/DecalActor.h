#pragma once

#include "GameFramework/Actor.h"
#include "Components/DecalComponent.h"

class UBillboardComponent;
class UStaticMeshComponent;

class ADecalActor : public AActor
{
    DECLARE_CLASS(ADecalActor, AActor)

public:
	ADecalActor();

	UBillboardComponent* GetBillboardComponent() const;
	UDecalComponent* GetDecalComponent() const;
	UStaticMeshComponent* GetArrowComponent() const;

private:
	void SetupDecalRoot(UDecalComponent* InDecalComponent, const FString& IconPath);


	UDecalComponent* DecalComponent = nullptr;
	UBillboardComponent* BillboardComponent = nullptr;
	UStaticMeshComponent* ArrowComponent = nullptr;
};