#include "EnginePCH.h"
#include "ParticleSystemSceneProxy.h"

#include "Components/ParticleSystemComponent.h"

FParticleSystemSceneProxy::FParticleSystemSceneProxy(
	UParticleSystemComponent* Component)
    : FPrimitiveSceneProxy(Component)
{
}

void FParticleSystemSceneProxy::UpdateDynamicData()
{
	const auto* Component =
		static_cast<const UParticleSystemComponent*>(GetComponent());

	DynamicEmitterData.SetNum(Component->EmitterInstances.Num());

	for (int32 Index = 0; Index < Component->EmitterInstances.Num(); ++Index)
	{
		TUniquePtr<FDynamicEmitterDataBase>& Data = DynamicEmitterData[Index];

		Data.reset(Component->EmitterInstances[Index]->GetDynamicData());
		if (!Data)
			continue;

		Data->EmitterIndex = Index;
		Data->PrepareRenderData(*this);
	}
}

bool FParticleSystemSceneProxy::GatherDynamicRenderPackets(
    FRenderQueue& RenderQueue,
    FRenderQueue*,
    const FVector& CameraLocation,
    FBillboardMatrixBuilder,
    const void*,
    TArray<int32>&,
    TArray<float>&) const
{
	for (const TUniquePtr<FDynamicEmitterDataBase>& Data : DynamicEmitterData)
	{
		if (Data)
			Data->GatherRenderPackets(*this, RenderQueue, CameraLocation);
	}
	return true;
}
