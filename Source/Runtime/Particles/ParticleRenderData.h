#pragma once

#include "Particles/ParticleReplayData.h"
#include "Particles/ParticleVertex.h"

class FParticleSystemSceneProxy;
class FRenderQueue;
class UMaterial;

inline constexpr int32 InvalidEmitterIndex = -1;

struct FParticleOrder
{
	// PartilceData 내 실제 저장 슬롯 번호.
	uint16 ParticleIndex = 0;
	float SortKey = 0.0f;
};

struct FDynamicEmitterDataBase
{
	virtual ~FDynamicEmitterDataBase() = default;

	int32 EmitterIndex = InvalidEmitterIndex;

	virtual const FDynamicEmitterReplayDataBase& GetSource() const = 0;

	// 프레임당 1번
	virtual void PrepareRenderData(const FParticleSystemSceneProxy& Proxy) {}

	// 뷰마다
	virtual void GatherRenderPackets(
		const FParticleSystemSceneProxy& Proxy,
		FRenderQueue& RenderQueue,
		const FVector& CameraLocation
	) const {}
};

struct FDynamicSpriteEmitterDataBase : public FDynamicEmitterDataBase
{
	// 공통 ReplayData에 대한 타입 지정 접근.
	virtual const FDynamicSpriteEmitterReplayDataBase&
	GetSource() const override = 0;

	// 원본 인덱스 배열을 변경하지 않고 뷰별 정렬 순서를 출력.
	void SortSpriteParticles(
	    const FVector& CameraLocation,
	    const FVector& CameraForward,
	    const FMatrix& LocalToWorld,
	    TArray<FParticleOrder>* OutParticleOrder) const;

	virtual int32 GetDynamicVertexStride() const = 0;
};

struct FDynamicSpriteEmitterData : public FDynamicSpriteEmitterDataBase
{
public:
	explicit FDynamicSpriteEmitterData(
	    FDynamicSpriteEmitterReplayData&& InSource);

	const FDynamicSpriteEmitterReplayData& GetSource() const override;

	int32 GetDynamicVertexStride() const override;

	void PrepareRenderData(const FParticleSystemSceneProxy& Proxy) override;
	void GatherRenderPackets(
		const FParticleSystemSceneProxy& Proxy,
		FRenderQueue& RenderQueue,
		const FVector& CameraLocation) const override;

private:
	FDynamicSpriteEmitterReplayData Source;
	UMaterial* Material = nullptr;
	TArray<FParticleSpriteInstanceVertex> Instances;
};

struct FDynamicMeshEmitterData : public FDynamicSpriteEmitterDataBase
{
public:
	explicit FDynamicMeshEmitterData(
	    FDynamicMeshEmitterReplayData&& InSource);

	const FDynamicMeshEmitterReplayData& GetSource() const override;

	int32 GetDynamicVertexStride() const override;

private:
	FDynamicMeshEmitterReplayData Source;
};
