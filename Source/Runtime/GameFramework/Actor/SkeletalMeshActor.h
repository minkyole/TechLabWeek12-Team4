#pragma once

#include "GameFramework/Actor.h"
#include "Components/SkeletalMeshComponent.h"

class ASkeletalMeshActor : public AActor
{
	DECLARE_CLASS(ASkeletalMeshActor, AActor)
public:
	ASkeletalMeshActor();
	USkeletalMeshComponent* GetSkeletalMeshComponent() const { return SkeletalMeshComponent; }

private:
	USkeletalMeshComponent* SkeletalMeshComponent = nullptr;
};
