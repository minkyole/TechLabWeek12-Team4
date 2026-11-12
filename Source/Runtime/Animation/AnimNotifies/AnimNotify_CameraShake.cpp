#include "EnginePCH.h"
#include "AnimNotify_CameraShake.h"

#include "Components/SkeletalMeshComponent.h"
#include "Engine/World.h"
#include "Camera/PlayerCameraManager.h"
#include "Camera/CameraModifier_CameraShake.h"
#include "Camera/Shake/CameraShakePattern.h"

#include <algorithm>
#include <memory>

std::unique_ptr<FCameraShakePattern> UAnimNotify_CameraShake::CreateShakePattern() const
{
	auto Pattern = std::make_unique<FPerlinNoiseCameraShakePattern>();
	Pattern->StateParams.Duration = ShakeDuration;
	Pattern->StateParams.BlendInTime = std::min(0.05f, ShakeDuration * 0.5f);
	Pattern->StateParams.BlendOutTime = std::min(0.15f, ShakeDuration * 0.5f);
	Pattern->Pitch = { 1.5f, 15.0f };
	Pattern->Yaw = { 1.5f, 15.0f };
	Pattern->Roll = { 1.0f, 15.0f };
	return Pattern;
}

void UAnimNotify_CameraShake::Notify(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation)
{
	if (!MeshComp || ShakeScale <= 0.0f)
	{
		return;
	}

	UWorld* World = MeshComp->GetWorld();
	APlayerCameraManager* CameraManager = World ? World->GetPlayerCameraManager() : nullptr;
	if (!CameraManager)
	{
		return;
	}

	CameraManager->AddCameraModifier<UCameraModifier_CameraShake>(CreateShakePattern(), ShakeScale);
}

void UAnimNotify_CameraShake::NormalizeProperties(const FProperty* ChangedProperty)
{
	ShakeScale = std::clamp(ShakeScale, 0.0f, 10.0f);
	ShakeDuration = std::clamp(ShakeDuration, 0.05f, 5.0f);
}
