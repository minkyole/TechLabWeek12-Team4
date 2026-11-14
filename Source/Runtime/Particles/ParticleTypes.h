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
	FacingCameraPosition, // 카메라 위치를 향한다. (카메라가 가까이 붙으면 뒤집히는 현상 발생)
	Square,               // 카메라 평면에 직교하는 정사각형. SizeX를 가로 세로 동일하게 사용
	Rectangle,            // 카메라 평면에 직교하는 직사각형. SizeX, SizeY를 그대로 사용
	Velocity,             // 카메라를 향하면서 이동방향에 맞춰 축을 정렬
	// TypeSpecific,
	FacingCameraDistanceBlend, // 카메라 위치를 향하지만 일정 거리 이상이면 정사각형으로 바뀐다.
};

// AxisLock에서 고정할 월드 축.
enum class EParticleAxisLock : uint8
{
	None,
	X,
	Y,
	Z,
};

enum EParticleStates
{
	/** Ignore updates to the particle						*/
	STATE_Particle_JustSpawned = 0x02000000,
	/** Ignore updates to the particle						*/
	STATE_Particle_Freeze = 0x04000000,
	/** Ignore collision updates to the particle			*/
	STATE_Particle_IgnoreCollisions = 0x08000000,
	/**	Stop translations of the particle					*/
	STATE_Particle_FreezeTranslation = 0x10000000,
	/**	Stop rotations of the particle						*/
	STATE_Particle_FreezeRotation = 0x20000000,
	/** Combination for a single check of 'ignore' flags	*/
	STATE_Particle_CollisionIgnoreCheck = STATE_Particle_Freeze | STATE_Particle_IgnoreCollisions | STATE_Particle_FreezeTranslation | STATE_Particle_FreezeRotation,
	/** Delay collision updates to the particle				*/
	STATE_Particle_DelayCollisions = 0x40000000,
	/** Flag indicating the particle has had at least one collision	*/
	STATE_Particle_CollisionHasOccurred = 0x80000000,
	/** State mask. */
	STATE_Mask = 0xFE000000,
	/** Counter mask. */
	STATE_CounterMask = (~STATE_Mask)
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
	FLinearColor Color{ 1.0f, 1.0f, 1.0f, 1.0f };
	FLinearColor BaseColor{ 1.0f, 1.0f, 1.0f, 1.0f };
};
