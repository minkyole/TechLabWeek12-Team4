#include "EnginePCH.h"
#include "LocalLightComponent.h"

ULocalLightComponent::ULocalLightComponent() : AttenuationRadius(5.0f) {}
ULocalLightComponent::~ULocalLightComponent() {}

// 로컬 광원 감쇠 반경의 변경값과 전체 복원값 보정
void ULocalLightComponent::NormalizeProperties(const FProperty* ChangedProperty)
{
	Super::NormalizeProperties(ChangedProperty);
	if (!ChangedProperty || ChangedProperty->Name == "AttenuationRadius")
	{
		// 기존 Setter를 통한 거리 최솟값 보장
		SetAttenuationRadius(AttenuationRadius);
	}
}