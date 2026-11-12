#pragma once

#include "GameFramework/Actor/DecalActor.h"

// 조명 계산 없이 Additive Decal로 바닥의 빛을 표현한다. 다른 라이트처럼 게임 화면에 광원은 그리지 않는다.
class AFakeSpotLightActor : public ADecalActor
{
	DECLARE_CLASS(AFakeSpotLightActor, ADecalActor)

public:
	AFakeSpotLightActor();
};
