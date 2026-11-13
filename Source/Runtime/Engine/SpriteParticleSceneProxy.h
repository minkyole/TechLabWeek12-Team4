#pragma once

#include "Engine/PrimitiveSceneProxy.h"
#include "Components/BillboardComponent.h"
#include "Particles/ParticleRenderData.h"

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
	UStaticMesh* QuadMesh = nullptr;
	UMaterial* Material = nullptr;
	
	TUniquePtr<FDynamicSpriteEmitterData> DynamicData;

	// 패킷에 실제 Draw될 때까지 주소가 유지되어야 한다.
	TArray<FSubUVConstants> ParticleParams;
};