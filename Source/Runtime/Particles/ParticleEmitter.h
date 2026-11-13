#pragma once

#include "UObject/Object.h"
#include "UObject/Class.h"
#include "Particles/ParticleTypes.h"

class UParticleLODLevel;
class UParticleModule;

class UParticleEmitter : public UObject
{
	DECLARE_CLASS(UParticleEmitter, UObject)

public:
	UParticleEmitter() = default;
	~UParticleEmitter() override = default;

	// LODLevels[0]을 기준으로 아래 캐시 값을 계산한다.
	// 모듈이나 LODLevel 구성이 바뀐 뒤 다시 호출해야 한다.
	void CacheEmitterModuleInfo();

	TArray<UParticleLODLevel*> LODLevels;

	// ---- CacheEmitterModuleInfo 결과 ----

	// 파티클 슬롯 하나의 크기. sizeof(FBaseParticle) + 모듈 Payload 합.
	int32 ParticleSize = static_cast<int32>(sizeof(FBaseParticle));

	// 모듈별 Payload 오프셋. 슬롯 시작 기준 바이트.
	TMap<UParticleModule*, int32> ModuleOffsetMap;

	TArray<UParticleModule*> SpawnModules;
	TArray<UParticleModule*> UpdateModules;

	EDynamicEmitterType EmitterType = EDynamicEmitterType::Sprite;
};
