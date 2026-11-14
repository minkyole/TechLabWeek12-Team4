#pragma once

#include "Engine/PrimitiveSceneProxy.h"
#include "Particles/ParticleRenderData.h"

class UParticleSystemComponent;

class FParticleSystemSceneProxy : public FPrimitiveSceneProxy
{
public:
	explicit FParticleSystemSceneProxy(
		UParticleSystemComponent* Component);

	bool GatherDynamicRenderPackets(
	    FRenderQueue& RenderQueue,
	    FRenderQueue* IconQueue,
	    const FVector& CameraLocation,
	    FBillboardMatrixBuilder BuildBillboardMatrix,
	    const void* ViewContext,
	    TArray<int32>& SortedParticleIndices,
	    TArray<float>& ParticleDistancesSquared) const override;

protected:
	void UpdateDynamicData() override;

private:
	TArray<TUniquePtr<FDynamicEmitterDataBase>> DynamicEmitterData;
};
