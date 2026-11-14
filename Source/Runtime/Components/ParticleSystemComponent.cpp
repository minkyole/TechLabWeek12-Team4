#include "EnginePCH.h"
#include "ParticleSystemComponent.h"
#include "Engine/ParticleSystemSceneProxy.h"

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

	// 동적 바운드는 씬에 등록된 박스보다 커졌을 때만 다시 반영한다.
	FPrimitiveSceneProxy* Proxy = GetSceneProxy();
	FBox NewBox{};
	if (!bUseFixedRelativeBoundingBox && Proxy && GetEmitterBoundingBox(NewBox))
	{
		const FAABB& Current = Proxy->GetBounds();
		const FVector CurrentMin = Current.Center - Current.Extent;
		const FVector CurrentMax = Current.Center + Current.Extent;

		const bool bInside =
			NewBox.Min.X >= CurrentMin.X && NewBox.Min.Y >= CurrentMin.Y && NewBox.Min.Z >= CurrentMin.Z &&
			NewBox.Max.X <= CurrentMax.X && NewBox.Max.Y <= CurrentMax.Y && NewBox.Max.Z <= CurrentMax.Z;

		if (!bInside)
		{
			OnTransformDirty();
		}
	}
}

FBox UParticleSystemComponent::CalcBounds() const
{
	if (bUseFixedRelativeBoundingBox)
	{
		return FixedRelativeBoundingBox.GetWorldAABB(GetWorldMatrix());
	}

	FBox BoundingBox{};
	if (!GetEmitterBoundingBox(BoundingBox))
	{
		const FVector Origin = GetWorldLocation();
		return FBox{ Origin, Origin };
	}

	// Extent(절반 크기)의 10%만큼 확장해 조금 커져도 다시 반영하지 않게 한다.
	const FVector ExpandAmount = (BoundingBox.Max - BoundingBox.Min) * 0.05f;
	return FBox{ BoundingBox.Min - ExpandAmount, BoundingBox.Max + ExpandAmount };
}

bool UParticleSystemComponent::GetEmitterBoundingBox(FBox& OutBoundingBox) const
{
	bool bValid = false;
	for (const FParticleEmitterInstance* EmitterInstance : EmitterInstances)
	{
		if (!EmitterInstance || EmitterInstance->ActiveParticles <= 0)
		{
			continue;
		}

		if (!bValid)
		{
			OutBoundingBox = EmitterInstance->GetBoundingBox();
			bValid = true;
		}
		else
		{
			OutBoundingBox.Expand(EmitterInstance->GetBoundingBox());
		}
	}
	return bValid;
}

FPrimitiveSceneProxy* UParticleSystemComponent::CreateSceneProxy()
{
	return new FParticleSystemSceneProxy(this);
}