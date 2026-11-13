#include "EnginePCH.h"
#include "ParticleRenderData.h"

#include <algorithm>
#include <cassert>

#include "ParticleVertex.h"

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
