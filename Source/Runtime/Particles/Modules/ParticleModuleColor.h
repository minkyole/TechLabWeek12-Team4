#pragma once

#include "Particles/ParticleModule.h"
#include "Particles/ParticleRange.h"

class UParticleModuleColor : public UParticleModule
{
	DECLARE_CLASS(UParticleModuleColor, UParticleModule)

public:
	UParticleModuleColor() = default;

	FParticleColorRange StartColor;

	// true이면 수명에 따라 BaseColor에서 EndColor로 선형 보간한다.
	bool bUseColorOverLife = false;
	FLinearColor EndColor = FLinearColor(1.0f, 1.0f, 1.0f, 0.0f);
};
