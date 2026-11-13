#pragma once

#include "Asset/RenderAsset.h"

class UParticleEmitter;

// 파티클 이펙트 에셋. 에미터 목록을 소유한다.
class UParticleSystem : public URenderAsset
{
	DECLARE_CLASS(UParticleSystem, URenderAsset)

public:
	UParticleSystem() = default;
	~UParticleSystem() override = default;

	TArray<UParticleEmitter*> Emitters;
};
