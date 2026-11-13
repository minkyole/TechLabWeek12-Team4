#include "EnginePCH.h"
#include "Particles/ParticleEmitter.h"

#include "Particles/ParticleLODLevel.h"
#include "Particles/ParticleModule.h"

void UParticleEmitter::CacheEmitterModuleInfo()
{
	// TODO: LODLevels[0]의 모듈을 순회해 Payload 오프셋과 슬롯 크기를 계산한다.
	ParticleSize = static_cast<int32>(sizeof(FBaseParticle));
	ModuleOffsetMap.Empty();
	SpawnModules.Empty();
	UpdateModules.Empty();
	EmitterType = EDynamicEmitterType::Sprite;
}
