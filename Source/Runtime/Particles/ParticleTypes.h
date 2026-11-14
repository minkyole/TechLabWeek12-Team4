#pragma once

#include "Math/Vector.h"
#include "Math/Color.h"

enum class EDynamicEmitterType : uint8
{
	Sprite,
	Mesh,
};

enum class EParticleSortMode : uint8
{
	None,
	DistanceToView,
	ViewDepth,
};

// 스프라이트 판이 카메라에 대해 어떤 방향을 갖는지.
enum class EParticleScreenAlignment : uint8
{
	ViewPlane,      // 화면 정렬. 카메라의 right/up 축에 맞춘다. (기본)
	FacingPosition, // 각 파티클에서 카메라 위치를 향한다.
	WorldFixed,     // 카메라를 무시하고 파티클 자체 회전으로 고정한다.
	AxisLock,       // LockAxis는 고정하고 나머지만 카메라를 따라온다.
};

// AxisLock에서 고정할 월드 축.
enum class EParticleLockAxis : uint8
{
	X,
	Y,
	Z,
};

struct FBaseParticle
{
	FVector Location;
	float RelativeTime; // 0~1, 1 이상이면 사망
	FVector OldLocation;
	float OneOverMaxLifetime; // 1 / Lifetime
	FVector Velocity;
	float Rotation; // 라디안
	FVector BaseVelocity;
	float RotationRate;
	FVector Size;
	float BaseRotationRate;
	FVector BaseSize;
	int32 Flags;
	FLinearColor Color;
	FLinearColor BaseColor;
};
