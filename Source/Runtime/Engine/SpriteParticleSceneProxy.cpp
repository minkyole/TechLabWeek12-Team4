#include "EnginePCH.h"
#include "SpriteParticleSceneProxy.h"

#include "Asset/AssetManager.h"
#include "Components/SpriteParticleTestComponent.h"
#include "Render/Material.h"
#include "Render/RenderPacket.h"

#include <cassert>
#include <utility>
#include <algorithm>

FSpriteParticleSceneProxy::FSpriteParticleSceneProxy(
    USpriteParticleTestComponent* Component)
    : FPrimitiveSceneProxy(Component)
{
}

void FSpriteParticleSceneProxy::UpdateDynamicData()
{
	const auto* Component =
	    static_cast<const USpriteParticleTestComponent*>(
	        GetComponent());

	DynamicData = MakeUnique<FDynamicSpriteEmitterData>(
	    Component->BuildReplayData());

	const auto& Source = DynamicData->GetSource();

	// 이번 단계에서 지원하는 실제 재질 타입은 UMaterial이다.
	Material = Cast<UMaterial>(Source.MaterialInterface);

	assert(Material);
	assert(Source.ActiveParticleCount >= 0);

	Instances.SetNum(Source.ActiveParticleCount);

	const FMatrix& LocalToWorld = GetLocalToWorld();

	for (int32 ActiveIndex = 0;
	     ActiveIndex < Source.ActiveParticleCount;
	     ++ActiveIndex)
	{
		const FBaseParticle& Particle =
		    Source.GetParticle(ActiveIndex);

		FParticleSpriteInstanceVertex Instance{};

		Instance.Position =
		    Source.bUseLocalSpace
		        ? LocalToWorld.TransformPosition(Particle.Location)
		        : Particle.Location;

		Instance.Rotation = Particle.Rotation;

		Instance.Size = FVector2(
		    std::abs(Particle.Size.X * Source.Scale.X),
		    std::abs(Particle.Size.Y * Source.Scale.Y));

		Instance.Color = FVector4(
		    Particle.Color.R,
		    Particle.Color.G,
		    Particle.Color.B,
		    Particle.Color.A);

		// 전체 텍스처를 사용.
		Instance.UVRect = FVector4(0.0f, 0.0f, 1.0f, 1.0f);

		Instances[ActiveIndex] = std::move(Instance);
	}
}

bool FSpriteParticleSceneProxy::GatherDynamicRenderPackets(
    FRenderQueue& RenderQueue,
    FRenderQueue*,
    const FVector& CameraLocation,
    FBillboardMatrixBuilder,
    const void*,
    TArray<int32>&,
    TArray<float>&) const
{
	if (!DynamicData || !Material || Instances.Num() == 0)
		return true;

	// AlphaBlend/ Additive Sprite를 대상으로 한다.
	assert(
	    Material->BlendState == EBlendState::AlphaBlend ||
	    Material->BlendState == EBlendState::Additive);

	FRenderPacket Packet{};

	Packet.Material = Material;

	// 뷰별 정렬을 위해 패킷이 독립적인 배열을 소유한다.
	Packet.SpriteInstances = Instances;

	if (Material->BlendState == EBlendState::AlphaBlend)
	{
		std::stable_sort(
		    Packet.SpriteInstances.begin(),
		    Packet.SpriteInstances.end(),
		    [&CameraLocation](
		        const FParticleSpriteInstanceVertex& A,
		        const FParticleSpriteInstanceVertex& B)
		    {
			    return (A.Position - CameraLocation).SizeSquared() >
			           (B.Position - CameraLocation).SizeSquared();
		    });
	}

	FVector Center = FVector::ZeroVector;

	for (const FParticleSpriteInstanceVertex& Instance :
	     Packet.SpriteInstances)
	{
		Center += Instance.Position;
	}

	Center *= 1.0f / static_cast<float>(Packet.SpriteInstances.Num());

	Packet.CameraToParticleDistance = (Center - CameraLocation).SizeSquared();
	Packet.ParticleBillboardMode = DynamicData->GetSource().BillboardMode;

	// 배열을 포함한 패킷을 이동한다.
	RenderQueue.Add(std::move(Packet));

	return true;
}