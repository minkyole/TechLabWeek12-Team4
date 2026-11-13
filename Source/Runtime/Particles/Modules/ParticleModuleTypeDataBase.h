#pragma once

#include "Particles/ParticleModule.h"

// 에미터 종류(Sprite/Mesh)를 결정하는 모듈의 베이스.
// LODLevel에 없으면 Sprite 에미터로 취급한다.
class UParticleModuleTypeDataBase : public UParticleModule
{
	DECLARE_CLASS(UParticleModuleTypeDataBase, UParticleModule)

public:
	UParticleModuleTypeDataBase() = default;

	virtual EDynamicEmitterType GetEmitterType() const
	{
		return EDynamicEmitterType::Sprite;
	}
};
