#pragma once

#include "Animation/AnimNotifies/AnimNotify.h"

class UAnimNotify_PlaySound : public UAnimNotify
{
	DECLARE_CLASS(UAnimNotify_PlaySound, UAnimNotify)
	REFLECT_START(UAnimNotify_PlaySound)
		PROPERTY(SoundPath)
		PROPERTY(VolumeMultiplier)
		PROPERTY(PitchMultiplier)
	REFLECT_END()

public:
	void Notify(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation) override;
	FString GetNotifyName() const override;
	const char* GetPropertyAssetFolder(const FProperty& Property) const override;
	void NormalizeProperties(const FProperty* ChangedProperty = nullptr) override;

	FString SoundPath;
	float VolumeMultiplier = 1.0f;
	float PitchMultiplier = 1.0f;
};
