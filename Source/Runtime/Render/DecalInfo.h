#pragma once

#include "Math/Vector4.h"
#include "Math/Vector2.h"
#include "Math/Matrix.h"
#include "Math/Color.h"

struct FDecalInfo
{
	FMatrix			WorldToDecal;
	FLinearColor	DecalColor = FLinearColor(1,1,1,1);
	FVector2		ViewSize;
	float			NormalThreshold = -1.0f;	// cos(최대 각도). 투영 방향과의 각도가 더 크면 픽셀을 버린다.
	float			bHasNormalTexture = 0.0f;	// 1이면 노말 맵을 Normal DBuffer에 쓴다.
};