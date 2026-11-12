#include "EnginePCH.h"
#include "SkeletalMeshActor.h"

ASkeletalMeshActor::ASkeletalMeshActor()
{
	SkeletalMeshComponent = CreateDefaultSubobject<USkeletalMeshComponent>("USkeletalMeshComponent");
	SetRootComponent(SkeletalMeshComponent);
}
