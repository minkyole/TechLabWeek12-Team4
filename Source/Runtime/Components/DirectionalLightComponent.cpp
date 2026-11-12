#include "EnginePCH.h"
#include "DirectionalLightComponent.h"

#include "Engine/DirectionalLightSceneProxy.h"

UDirectionalLightComponent::UDirectionalLightComponent()
{

}

UDirectionalLightComponent::~UDirectionalLightComponent()
{
}

FLightSceneProxy* UDirectionalLightComponent::CreateSceneProxy() const
{
    return new FDirectionalLightSceneProxy(this);
}

void UDirectionalLightComponent::NormalizeProperties(const FProperty* ChangedProperty)
{
    // 부모의 해상도 등 기존 그림자 설정 편집 처리를 유지한다.
	Super::NormalizeProperties(ChangedProperty);

    // Getter와 같은 범위를 사용해 편집값과 실제 계산값을 맞춘다.
	if (!ChangedProperty || ChangedProperty->Name == "ShadowDistance")
        ShadowDistance = GetShadowDistance();
	if (!ChangedProperty || ChangedProperty->Name == "CascadeCount")
        CascadeCount = GetCascadeCount();
	if (!ChangedProperty || ChangedProperty->Name == "CascadeDistributionExponent")
        CascadeDistributionExponent = GetCascadeDistributionExponent();
	if (!ChangedProperty || ChangedProperty->Name == "CascadeTransitionFraction")
        CascadeTransitionFraction = GetCascadeTransitionFraction();
	if (!ChangedProperty || ChangedProperty->Name == "SourceAngle")
        SourceAngle = GetSourceAngle();
}
