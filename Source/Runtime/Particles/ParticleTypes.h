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

enum class EParticleBillboardMode : uint32
{
	ScreenAligned = 0,
	FaceCameraPosition = 1,
	WorldUpLocked = 2,
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
