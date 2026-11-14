#pragma once

#include "Engine/PrimitiveSceneProxy.h"
#include "Components/BillboardComponent.h"
#include "Particles/ParticleRenderData.h"
#include "Particles/ParticleVertex.h"

class USpriteParticleTestComponent;

class FSpriteParticleSceneProxy : public FPrimitiveSceneProxy
{
public:
	explicit FSpriteParticleSceneProxy(
	    USpriteParticleTestComponent* Component);

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
	UMaterial* Material = nullptr;
	
	TUniquePtr<FDynamicSpriteEmitterData> DynamicData;

	TArray<FParticleSpriteInstanceVertex> Instances;
};