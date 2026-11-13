#pragma once

#include "Particles/Modules/ParticleModuleTypeDataBase.h"

class UStaticMesh;

class UParticleModuleTypeDataMesh : public UParticleModuleTypeDataBase
{
	DECLARE_CLASS(UParticleModuleTypeDataMesh, UParticleModuleTypeDataBase)

public:
	UParticleModuleTypeDataMesh() = default;

	EDynamicEmitterType GetEmitterType() const override
	{
		return EDynamicEmitterType::Mesh;
	}

	// 회전 값(FVector) 하나를 저장하는 Payload
	int32 GetRequiredPayloadBytes() const override
	{
		return sizeof(FVector);
	}

	UStaticMesh* Mesh = nullptr;
};
