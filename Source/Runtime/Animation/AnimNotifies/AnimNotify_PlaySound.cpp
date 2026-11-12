#include "EnginePCH.h"
#include "AnimNotify_PlaySound.h"

#include "Engine/Engine.h"
#include "Audio/AudioSystem.h"

#include <algorithm>
#include <filesystem>

void UAnimNotify_PlaySound::Notify(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation)
{
	if (SoundPath.empty() || !GEngine)
	{
		return;
	}

	FAudioSystem* Audio = GEngine->GetAudioSystem();
	if (!Audio || !Audio->IsInitialized())
	{
		return;
	}

	const uint32 Handle = Audio->Play(SoundPath, false, VolumeMultiplier);
	if (Handle != 0 && PitchMultiplier != 1.0f)
	{
		Audio->SetPitch(Handle, PitchMultiplier);
	}
}

FString UAnimNotify_PlaySound::GetNotifyName() const
{
	if (SoundPath.empty())
	{
		return Super::GetNotifyName();
	}
	return "Sound: " + std::filesystem::path(SoundPath).stem().string();
}

const char* UAnimNotify_PlaySound::GetPropertyAssetFolder(const FProperty& Property) const
{
	return Property.Name == FName("SoundPath") ? "Assets/Sounds" : nullptr;
}

void UAnimNotify_PlaySound::NormalizeProperties(const FProperty* ChangedProperty)
{
	VolumeMultiplier = std::clamp(VolumeMultiplier, 0.0f, 2.0f);
	PitchMultiplier = std::clamp(PitchMultiplier, 0.1f, 4.0f);
}
