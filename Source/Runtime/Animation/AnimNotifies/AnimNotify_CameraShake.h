#pragma once

#include "Animation/AnimNotifies/AnimNotify.h"

#include <memory>

class UAnimNotify_CameraShake : public UAnimNotify
{
	DECLARE_CLASS(UAnimNotify_CameraShake, UAnimNotify)
	REFLECT_START(UAnimNotify_CameraShake)
		PROPERTY(ShakeScale)
		PROPERTY(ShakeDuration)
	REFLECT_END()

public:
	void Notify(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation) override;
	void NormalizeProperties(const FProperty* ChangedProperty = nullptr) override;

	std::unique_ptr<class FCameraShakePattern> CreateShakePattern() const;

	float ShakeScale = 1.0f;
	float ShakeDuration = 0.3f;
};
