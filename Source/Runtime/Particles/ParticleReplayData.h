#pragma once

#include "Particles/ParticleTypes.h"

inline constexpr int32 InvalidPayloadOffset = -1;

class UMaterialInterface;
class UStaticMesh;

struct FParticleDataContainer
{
public:
	FParticleDataContainer();
	~FParticleDataContainer();

	FParticleDataContainer(const FParticleDataContainer&) = delete;
	FParticleDataContainer& operator=(const FParticleDataContainer&) = delete;

	FParticleDataContainer(FParticleDataContainer&& Other) noexcept;
	FParticleDataContainer& operator=(FParticleDataContainer&& Other) noexcept;

	// 슬롯 데이터와 활성 인덱스를 자체 메모리로 복사.
	void CopyFrom(
	    const uint8* InParticleData,
	    int32 InParticleDataNumBytes,
	    const uint16* InParticleIndices,
	    int32 InParticleIndicesNumShorts);

	void Reset();

	const uint8* GetParticleData() const;
	const uint16* GetParticleIndices() const;

	int32 GetParticleDataNumBytes() const;
	int32 GetParticleIndicesNumShorts() const;

private:
	int32 MemBlockSize = 0;
	int32 ParticleDataNumBytes = 0;
	int32 ParticleIndicesNumShorts = 0;

	uint8* ParticleData = nullptr;
	uint16* ParticleIndices = nullptr;
};

struct FDynamicEmitterReplayDataBase
{
	FDynamicEmitterReplayDataBase() = default;
	virtual ~FDynamicEmitterReplayDataBase() = default;

	FDynamicEmitterReplayDataBase(
	    const FDynamicEmitterReplayDataBase&) = delete;
	FDynamicEmitterReplayDataBase& operator=(
	    const FDynamicEmitterReplayDataBase&) = delete;

	FDynamicEmitterReplayDataBase(
	    FDynamicEmitterReplayDataBase&&) noexcept = default;
	FDynamicEmitterReplayDataBase& operator=(
	    FDynamicEmitterReplayDataBase&&) noexcept = default;

	EDynamicEmitterType eEmitterType = EDynamicEmitterType::Sprite;

	int32 ActiveParticleCount = 0;
	int32 ParticleStride = 0;

	FParticleDataContainer DataContainer;

	FVector Scale = FVector(1.0f, 1.0f, 1.0f);
	EParticleSortMode SortMode = EParticleSortMode::None;

	// 활성 목록 내 위치를 실제 저장 슬롯 번호로 변환
	uint16 GetParticleIndex(int32 ActiveIndex) const;

	// 활성 목록 인덱스로 파티클 조회
	const FBaseParticle& GetParticle(int32 ActiveIndex) const;

	// PayloadOffset은 파티클 슬롯 시작 기준 바이트 오프셋
	const uint8* GetParticlePayload(
	    int32 ActiveIndex,
	    int32 PayloadOffset) const;
};

struct FDynamicSpriteEmitterReplayDataBase
	: public FDynamicEmitterReplayDataBase
{
	UMaterialInterface* MaterialInterface = nullptr;

	bool bUseLocalSpace = false;

	// Billboard 방식등...
	// TODO
};

struct FDynamicSpriteEmitterReplayData
	: public FDynamicSpriteEmitterReplayDataBase
{
	// SubUV 등 Sprite 고유의 Payload 오프셋·설정을 추가.
};

struct FDynamicMeshEmitterReplayData
	: public FDynamicSpriteEmitterReplayDataBase
{
	FDynamicMeshEmitterReplayData();

	UStaticMesh* Mesh = nullptr;
	int32 MeshRotationPayloadOffset = InvalidPayloadOffset;
};
