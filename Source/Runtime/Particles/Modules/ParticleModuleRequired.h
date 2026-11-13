#pragma once

#include "Particles/ParticleModule.h"

class UMaterialInterface;

// 에미터 전체에 적용되는 필수 설정. ReplayData를 채울 때 읽는다.
class UParticleModuleRequired : public UParticleModule
{
	DECLARE_CLASS(UParticleModuleRequired, UParticleModule)

public:
	UParticleModuleRequired() = default;

	UMaterialInterface* Material = nullptr;

	bool bUseLocalSpace = false;
	EParticleSortMode SortMode = EParticleSortMode::None;

	float EmitterDuration = 1.0f;
	// 0이면 무한 반복.
	int32 EmitterLoops = 0;

	int32 MaxActiveParticles = 100;
};
