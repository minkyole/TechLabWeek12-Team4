#include "EnginePCH.h"
#include "ParticleRenderData.h"

#include <algorithm>
#include <cassert>
#include <utility>

#include "ParticleVertex.h"
#include "Engine/ParticleSystemSceneProxy.h"
#include "Render/Material.h"
#include "Render/RenderPacket.h"

FDynamicSpriteEmitterData::FDynamicSpriteEmitterData(
    FDynamicSpriteEmitterReplayData&& InSource)
    : Source(std::move(InSource))
{
	Source.eEmitterType = EDynamicEmitterType::Sprite;
}

const FDynamicSpriteEmitterReplayData&
FDynamicSpriteEmitterData::GetSource() const
{
	return Source;
}

int32 FDynamicSpriteEmitterData::GetDynamicVertexStride() const
{
	// 현재 인스턴싱 설계에서 동적으로 업로드하는 레코드의 크기.
	return static_cast<int32>(
	    sizeof(FParticleSpriteInstanceVertex));
}

FDynamicMeshEmitterData::FDynamicMeshEmitterData(
	FDynamicMeshEmitterReplayData&& InSource)
	: Source(std::move(InSource))
{
	Source.eEmitterType = EDynamicEmitterType::Mesh;
}

const FDynamicMeshEmitterReplayData&
FDynamicMeshEmitterData::GetSource() const
{
	return Source;
}

int32 FDynamicMeshEmitterData::GetDynamicVertexStride() const
{
	return static_cast<int32>(
	    sizeof(FMeshParticleInstanceVertex));
}

void FDynamicSpriteEmitterDataBase::SortSpriteParticles(
	const FVector& CameraLocation,
	const FVector& CameraForward,
	const FMatrix& LocalToWorld,
	TArray<FParticleOrder>* OutParticleOrder) const
{
	assert(OutParticleOrder);
	TArray<FParticleOrder>& ParticleOrder = *OutParticleOrder;

	const FDynamicSpriteEmitterReplayDataBase& Source = GetSource();

	assert(Source.ActiveParticleCount >= 0);

	ParticleOrder.Reset();
	ParticleOrder.Reserve(Source.ActiveParticleCount);

	for (int32 ActiveIndex = 0;
		ActiveIndex < Source.ActiveParticleCount;
		++ActiveIndex)
	{
		FParticleOrder Order;
		Order.ParticleIndex = Source.GetParticleIndex(ActiveIndex);

		if (Source.SortMode != EParticleSortMode::None)
		{
			const FBaseParticle& Particle = Source.GetParticle(ActiveIndex);

			const FVector WorldPosition =
			    Source.bUseLocalSpace
			        ? LocalToWorld.TransformPosition(Particle.Location)
			        : Particle.Location;

			const FVector ToParticle = WorldPosition - CameraLocation;

			switch (Source.SortMode)
			{
			case EParticleSortMode::DistanceToView:
				Order.SortKey = ToParticle.SizeSquared();
				break;

			case EParticleSortMode::ViewDepth:
				Order.SortKey =
				    FVector::Dot(ToParticle, CameraForward);
				break;

			default:
				assert(false);
				break;
			}

			assert(std::isfinite(Order.SortKey));
		}

		ParticleOrder.Add(Order);
	}

	if (Source.SortMode == EParticleSortMode::None ||
		ParticleOrder.Num() < 2)
	{
		return;
	}

	// 동일한 키의 파티클은 기존 활성 목록 순서를 유지한다.
	std::stable_sort(
	    ParticleOrder.begin(),
	    ParticleOrder.end(),
	    [](const FParticleOrder& A, const FParticleOrder& B)
	    {
		    // SortKey가 큰 순서대로 정렬한다. (카메라에서 먼 순서)
		    return A.SortKey > B.SortKey;
	    });
	
}

void FDynamicSpriteEmitterData::PrepareRenderData(const FParticleSystemSceneProxy& Proxy)
{
	// 이번 단계에서 지원하는 실제 재질 타입은 UMaterial이다.
	Material = Cast<UMaterial>(Source.MaterialInterface);
	if (!Material)
	{
		Instances.Reset();
		return;
	}

	assert(Source.ActiveParticleCount >= 0);

	Instances.SetNum(Source.ActiveParticleCount);

	const FMatrix& LocalToWorld = Proxy.GetLocalToWorld();

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

		Instance.Velocity =
			Source.bUseLocalSpace
			? LocalToWorld.TransformPosition(Particle.Velocity) -
			LocalToWorld.TransformPosition(FVector::ZeroVector)
			: Particle.Velocity;

		Instances[ActiveIndex] = std::move(Instance);
	}
}

void FDynamicSpriteEmitterData::GatherRenderPackets(
	const FParticleSystemSceneProxy& Proxy,
	FRenderQueue& RenderQueue,
	const FVector& CameraLocation) const
{
	if (!Material || Instances.Num() == 0)
		return;

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

	Packet.ParticleScreenAlignment = Source.ScreenAlignment;
	Packet.ParticleAxisLock = Source.AxisLock;

	// 배열을 포함한 패킷을 이동한다.
	RenderQueue.Add(std::move(Packet));
}
