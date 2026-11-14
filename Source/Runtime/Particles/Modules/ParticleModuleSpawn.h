#pragma once

#include "Particles/ParticleModule.h"
#include "Particles/ParticleRange.h"

struct FParticleBurst
{
	// 에미터 루프 시작 기준 시간(초)
	float Time = 0.0f;
	int32 Count = 0;
};

struct FParticleSpawnBatch
{
	int32 Count = 0;
	float StartTime = 0.0f;
	float Increment = 0.0f;
};


// 이번 틱에 파티클을 몇 개, 어떤 시간 간격으로 만들지 계산해서 알려주는 모듈
class UParticleModuleSpawn : public UParticleModule
{
	DECLARE_CLASS(UParticleModuleSpawn, UParticleModule)

public:
	UParticleModuleSpawn() = default;

	// Rate: 이번 Tick에 파티클 몇 개 만들면 되는지 계산
	FParticleSpawnBatch GetRateBatch(float DeltaTime, float& InOutSpawnFraction) const;

	// Burst: 이번 Tick에 터질 시간이 된 폭죽이 있는지 확인해서, 있으면 목록에 담아 알려주는 함수
	void GetBurstBatches(float PrevEmitterTime, float CurrEmitterTime, float Duration, TArray<FParticleSpawnBatch>& Out) const;

	// 초당 스폰 개수
	float SpawnRate = 10.0f;

	TArray<FParticleBurst> Bursts;
};
