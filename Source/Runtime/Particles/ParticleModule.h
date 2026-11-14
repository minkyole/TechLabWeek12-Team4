#pragma once

#include "UObject/Object.h"
#include "UObject/Class.h"
#include "Particles/ParticleTypes.h"

struct FParticleEmitterInstance;

// 모듈 한 종류가 파티클 슬롯에 추가로 요구하는 메모리 정보는
// UParticleEmitter::CacheEmitterModuleInfo()에서 계산한다.
// 이 인터페이스는 시뮬레이션 코어와 합의가 필요한 부분이다.
class UParticleModule : public UObject
{
	DECLARE_CLASS(UParticleModule, UObject)

public:
	UParticleModule() = default;
	~UParticleModule() override = default;

	// 파티클 슬롯 뒤에 붙는 모듈 전용 Payload 바이트 수. 필요 없으면 0.
	virtual int32 GetRequiredPayloadBytes() const
	{
		return 0;
	}

	// 파티클이 생성될 때 한 번 호출된다.
	// PayloadOffset은 파티클 슬롯 시작 기준 바이트 오프셋이다.
	virtual void Spawn(FParticleEmitterInstance& Owner, int32 PayloadOffset, float SpawnTime, FBaseParticle& Particle)
	{
	}

	// 매 틱 살아 있는 파티클마다 호출된다.
	virtual void Update(FParticleEmitterInstance& Owner, int32 PayloadOffset, float DeltaTime)
	{
	}

	// Spawn/Update 중 구현한 쪽만 순회 대상으로 캐시하기 위한 표식.
	virtual bool NeedsSpawn() const
	{
		return false;
	}

	virtual bool NeedsUpdate() const
	{
		return false;
	}

	bool bEnabled = true;
};
