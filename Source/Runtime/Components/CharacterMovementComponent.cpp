#include "EnginePCH.h"
#include "CharacterMovementComponent.h"

#include "Collision/HitResult.h"
#include "Components/ShapeComponent.h"
#include "GameFramework/Actor.h"
#include "Collision/CollisionShape.h"
#include "Engine/World.h"
#include "Engine/Level.h"
#include "GameFramework/Actor/Character.h"
#include "Components/SkeletalMeshComponent.h"

#include <algorithm>
#include <cmath>

namespace
{
	FVector MoveVelocityTowards(
	    const FVector& Current,
	    const FVector& Target,
	    float MaxChange)
	{
		const FVector Difference = Target - Current;
		const float Distance =
		    std::hypot(Difference.X, Difference.Y);

		if (Distance <= MaxChange || Distance <= 0.000001f)
			return Target;

		return Current + Difference * (MaxChange / Distance);
	}
}

UCharacterMovementComponent::UCharacterMovementComponent()
{
	PrimaryComponentTick.bCanEverTick = true;

	// PrePhysics의 Lua 입력 처리가 끝난 뒤 이동한다.
	PrimaryComponentTick.TickGroup = ETickingGroup::DuringPhysics;

	Velocity = FVector::ZeroVector;

	Gravity = 20.0f;
}

void UCharacterMovementComponent::BeginPlay()
{
	Super::BeginPlay();

	UpdateGroundedState();
}

void UCharacterMovementComponent::TickComponent(float DeltaTime)
{
	Super::TickComponent(DeltaTime);

	AActor* Owner = GetOwner();

	if (!Owner || Owner->IsActorBeingDestroyed())
		return;

	if (!UpdatedComponent)
	{
		bIsGrounded = false;
		Velocity = FVector::ZeroVector;
		return;
	}

	// 초기 포즈 갱신 등 0초 업데이트에서는 기존 속도를 보존한다.
	if (!std::isfinite(DeltaTime) || DeltaTime <= 0.0f)
		return;

	// 현재 엔진에서 충동 Sweep을 사용하려면
	// 이동 대상이 blocking Shape여야 한다.
	UShapeComponent* Shape =
	    Cast<UShapeComponent>(UpdatedComponent);

	if (!Shape || !Shape->IsBlockComponent())
	{
		if (!bWarnedSweepWithoutShape)
		{
			HTR_LOG(
			    Warning,
			    "[CharacterMovement] A blocking Shape root is required.");

			bWarnedSweepWithoutShape = true;
		}

		bIsGrounded = false;
		StopMovementImmediately();
		return;
	}

	USceneComponent* MovingComponent = UpdatedComponent;

	const auto CanContinue = [&]()
	{
		return !Owner->IsActorBeingDestroyed() &&
		       UpdatedComponent == MovingComponent;
	};

	const auto AbortMovement = [&]()
	{
		bIsGrounded = false;
		Velocity = FVector::ZeroVector;
		UpdateComponentVelocity();
	};

	UpdateGroundedState();

	// --------------------------------------------------
	// 1. 수평 가속·감속
	// --------------------------------------------------

	const FVector CurrentHorizontal(
	    Velocity.X,
	    Velocity.Y,
	    0.0f);

	const FVector TargetHorizontal =
	    MoveInput * MaxSpeed;

	const float CurrentSpeed =
	    std::hypot(CurrentHorizontal.X, CurrentHorizontal.Y);

	const float TargetSpeed =
	    std::hypot(TargetHorizontal.X, TargetHorizontal.Y);

	const float ChangeRate =
	    TargetSpeed < CurrentSpeed
	        ? BrakingDeceleration
	        : Acceleration;

	const FVector NextHorizontal = MoveVelocityTowards(
	    CurrentHorizontal,
	    TargetHorizontal,
	    ChangeRate * DeltaTime);

	// 메시가 언제 Tick했든 Consume이 비워 주니 이번 프레임 이동은 여기서 한 번만 쓰인다
	FVector RootMotionDelta = FVector::ZeroVector;
	FQuat RootMotionRotation = FQuat::Identity();
	bool bHasRootMotion = false;
	if (ACharacter* Character = Cast<ACharacter>(Owner))
	{
		if (USkeletalMeshComponent* Mesh = Character->GetMesh())
		{
			bHasRootMotion = Mesh->ConsumeRootMotionWorld(RootMotionDelta, RootMotionRotation);
		}
	}

	// 입력 속도와 더하면 두 번 움직이니, Root Motion이 있으면 입력 속도를 버리고 같은 Sweep 경로로 보낸다
	// Z는 중력과 점프가 맡고 있어서 수평 성분만 쓴다
	const FVector HorizontalDelta = bHasRootMotion
	                                    ? FVector(RootMotionDelta.X, RootMotionDelta.Y, 0.0f)
	                                    : NextHorizontal * DeltaTime;

	// 캡슐이 기울면 안 되니 회전도 Yaw만 받는다
	FRotator NewRotation = UpdatedComponent->GetWorldRotation();
	const float RootMotionYaw = bHasRootMotion ? RootMotionRotation.ToFRotator().Yaw : 0.0f;
	NewRotation.Yaw += RootMotionYaw;

	if (!std::isfinite(HorizontalDelta.X) ||
	    !std::isfinite(HorizontalDelta.Y))
	{
		AbortMovement();
		return;
	}

	const FVector PreviousLocation =
	    UpdatedComponent->GetWorldLocation();

	if (HorizontalDelta.X != 0.0f || HorizontalDelta.Y != 0.0f || RootMotionYaw != 0.0f)
	{
		FHitResult HorizontalHit;

		MoveUpdatedComponent(
		    HorizontalDelta,
		    NewRotation,
		    true,
		    &HorizontalHit);

		if (!CanContinue())
		{
			AbortMovement();
			return;
		}
	}

	const FVector HorizontalMoved =
	    UpdatedComponent->GetWorldLocation() - PreviousLocation;

	// 실제 이동량으로 XY만 갱신한다.
	// Z는 Lua가 설정한 속도 및 중력 계산 결과를 유지한다.
	Velocity.X = HorizontalMoved.X / DeltaTime;
	Velocity.Y = HorizontalMoved.Y / DeltaTime;

	// 이번 수평 이동으로 바닥 끝을 벗어났는지 확인한다.
	UpdateGroundedState();

	// --------------------------------------------------
	// 2. 수직 속도와 중력
	// --------------------------------------------------

	float VerticalDelta = 0.0f;

	if (bIsGrounded)
	{
		Velocity.Z = 0.0f;
	}
	else
	{
		const float OldVerticalVelocity = Velocity.Z;
		const float EffectiveGravity = Gravity * GravityScale;

		Velocity.Z = std::max(
		    Velocity.Z - EffectiveGravity * DeltaTime,
		    -MaxFallSpeed);

		VerticalDelta =
		    (OldVerticalVelocity + Velocity.Z) *
		    0.5f * DeltaTime;
	}

	if (!std::isfinite(Velocity.Z) ||
	    !std::isfinite(VerticalDelta))
	{
		AbortMovement();
		return;
	}

	// --------------------------------------------------
	// 3. 수직 충돌 이동
	// --------------------------------------------------

	if (VerticalDelta != 0.0f)
	{
		FHitResult VerticalHit;

		MoveUpdatedComponent(
		    FVector(0.0f, 0.0f, VerticalDelta),
		    UpdatedComponent->GetWorldRotation(),
		    true,
		    &VerticalHit);

		if (!CanContinue())
		{
			AbortMovement();
			return;
		}

		if (VerticalHit.bBlockingHit)
		{
			// 이 최소 구현에서는 수직 이동이 막히면
			// 상승/낙하 속도를 제거한다.
			// 걸을 수 있는 바닥인지는 아래에서 별도 판정한다.
			Velocity.Z = 0.0f;
		}
	}

	// --------------------------------------------------
	// 4. 최종 접지와 속도 반영
	// --------------------------------------------------

	UpdateGroundedState();

	if (bIsGrounded)
		Velocity.Z = 0.0f;

	UpdateComponentVelocity();
}



void UCharacterMovementComponent::SetMoveInput(
    const FVector& WorldDirection)
{
	if (!std::isfinite(WorldDirection.X) ||
	    !std::isfinite(WorldDirection.Y) ||
	    !std::isfinite(WorldDirection.Z))
	{
		MoveInput = FVector::ZeroVector;
		return;
	}

	// 수평 이동만 지원한다.
	MoveInput = FVector(
	    WorldDirection.X,
	    WorldDirection.Y,
	    0.0f);

	// 대각선 이동 속도 증가 방지.
	// 길이 1 미만 입력은 유지하여 아날로그 입력도 허용한다.
	const double Length = std::hypot(
	    static_cast<double>(MoveInput.X),
	    static_cast<double>(MoveInput.Y));

	if (Length > 1.0)
	{
		MoveInput.X = static_cast<float>(MoveInput.X / Length);
		MoveInput.Y = static_cast<float>(MoveInput.Y / Length);
	}
}

void UCharacterMovementComponent::SetMaxSpeed(float InMaxSpeed)
{
	if (!std::isfinite(InMaxSpeed) || InMaxSpeed < 0.0f)
	{
		HTR_LOG(
		    Warning,
		    "[CharacterMovement] MaxSpeed must be finite and non-negative.");

		return;
	}

	MaxSpeed = InMaxSpeed;
}

void UCharacterMovementComponent::SetAcceleration(float Value)
{
	if (std::isfinite(Value) && Value >= 0.0f)
	{
		Acceleration = Value;
	}
}

void UCharacterMovementComponent::SetBrakingDeceleration(float Value)
{
	if (std::isfinite(Value) && Value >= 0.0f)
	{
		BrakingDeceleration = Value;
	}
}

void UCharacterMovementComponent::SetVerticalVelocity(float Value)
{
	if (!std::isfinite(Value))
		return;

	Velocity.Z = Value;

	// 상승 속도를 받았는데 접지 상태를 유지하면
	// Tick에서 Velocity.Z를 다시 0으로 만들 수 있다.
	if (Value > 0.0f)
		bIsGrounded = false;

	UpdateComponentVelocity();
}

float UCharacterMovementComponent::GetVerticalVelocity() const
{
	return Velocity.Z;
}

void UCharacterMovementComponent::SetGravityAcceleration(
    float InGravity)
{
	if (!std::isfinite(InGravity) || InGravity < 0.0f)
		return;

	Gravity = InGravity;
}

void UCharacterMovementComponent::SetGravityScale(float InScale)
{
	if (!std::isfinite(InScale) || InScale < 0.0f)
		return;

	GravityScale = InScale;
}

float UCharacterMovementComponent::GetGroundSpeed() const
{
	return std::sqrt(
	    Velocity.X * Velocity.X +
	    Velocity.Y * Velocity.Y);
}

void UCharacterMovementComponent::StopMovementImmediately()
{
	MoveInput = FVector::ZeroVector;

	// 부모가 Velocity를 0으로 만들고 ComponentVelocity도 갱신한다.
	Super::StopMovementImmediately();
}

bool UCharacterMovementComponent::FindFloor(
    FHitResult& OutHit) const
{
	OutHit = FHitResult{};

	AActor* Owner = GetOwner();

	if (!Owner || Owner->IsActorBeingDestroyed())
		return false;

	UShapeComponent* CharacterShape =
	    Cast<UShapeComponent>(UpdatedComponent);

	if (!CharacterShape || !CharacterShape->IsBlockComponent())
		return false;

	UWorld* World = CharacterShape->GetWorld();

	if (!World || !World->GetPersistentLevel())
		return false;

	if (!std::isfinite(GroundProbeDistance) ||
	    GroundProbeDistance <= 0.0f)
	{
		return false;
	}

	const FVector Start =
	    CharacterShape->GetWorldLocation();

	const FVector Delta(
	    0.0f,
	    0.0f,
	    -GroundProbeDistance);

	const FQuat Rotation =
	    CharacterShape->GetWorldRotation().Quaternion();

	const FCollisionShape ProbeShape =
	    CharacterShape->GetCollisionShape();

	bool bFoundFloor = false;
	float BestTime = 1.0f;

	// 반복마다 배열을 새로 할당하지 않도록 재사용한다.
	TArray<Collision::FSweepTarget> Targets;
	Targets.Reserve(1);

	for (AActor* OtherActor :
	     World->GetPersistentLevel()->GetActors())
	{
		if (!OtherActor ||
		    OtherActor == Owner ||
		    OtherActor->IsActorBeingDestroyed())
		{
			continue;
		}

		for (UActorComponent* Component :
		     OtherActor->GetComponents())
		{
			UShapeComponent* OtherShape =
			    Cast<UShapeComponent>(Component);

			if (!OtherShape ||
			    !OtherShape->IsRegistered() ||
			    !OtherShape->IsBlockComponent())
			{
				continue;
			}

			Targets.Reset();

			Collision::FSweepTarget Target;
			Target.Shape = OtherShape->GetCollisionShape();
			Target.Pos = OtherShape->GetWorldLocation();
			Target.Rot =
			    OtherShape->GetWorldRotation().Quaternion();

			Targets.Add(Target);

			const Collision::FSweepResult Result =
			    Collision::Sweep(
			        ProbeShape,
			        Start,
			        Delta,
			        Rotation,
			        Targets);

			if (!Result.bBlockingHit)
				continue;

			// 깊게 박힌 상태를 정상적인 접지로 취급하지 않는다.
			// 단, 현재 Sweep은 단순 접촉도 초기 겹침으로
			// 반환할 수 있으므로 작은 오차는 허용한다.
			if (Result.bStartPenetrating &&
			    Result.Contact.PenetrationDepth >
			        GroundPenetrationTolerance)
			{
				continue;
			}

			const FVector Normal = Result.Contact.Normal;

			const float NormalLength = std::sqrt(
			    Normal.X * Normal.X +
			    Normal.Y * Normal.Y +
			    Normal.Z * Normal.Z);

			if (!std::isfinite(NormalLength) ||
			    NormalLength <= 0.000001f)
			{
				continue;
			}

			const FVector UnitNormal =
			    Normal / NormalLength;

			// 위쪽을 충분히 향하는 면만 바닥으로 인정한다.
			// 벽: Z≈0, 천장: Z<0, 평지: Z≈1
			if (UnitNormal.Z < WalkableFloorZ)
				continue;

			const float HitTime =
			    std::clamp(Result.Time, 0.0f, 1.0f);

			if (bFoundFloor && HitTime >= BestTime)
				continue;

			bFoundFloor = true;
			BestTime = HitTime;

			OutHit.bBlockingHit = true;
			OutHit.bStartPenetrating =
			    Result.bStartPenetrating;

			OutHit.Time = HitTime;
			OutHit.Distance =
			    GroundProbeDistance * HitTime;

			OutHit.TraceStart = Start;
			OutHit.TraceEnd = Start + Delta;

			// Sweep 시 충돌체 기준 위치와 실제 접촉점은 다르다.
			OutHit.Location = Start + Delta * HitTime;
			OutHit.ImpactPoint = Result.Contact.Point;

			OutHit.Normal = UnitNormal;
			OutHit.ImpactNormal = UnitNormal;

			OutHit.PenetrationDepth =
			    Result.Contact.PenetrationDepth;

			OutHit.HitComponent = OtherShape;
			OutHit.HitActor = OtherActor;
		}
	}

	return bFoundFloor;
}

void UCharacterMovementComponent::UpdateGroundedState()
{
	bIsGrounded = false;

	// 점프 상승 중 재접지 되는 것을 방지한다.
	if (Velocity.Z > 0.0f)
		return;

	FHitResult FloorHit;

	if (!FindFloor(FloorHit))
		return;

	bIsGrounded = FloorHit.Distance <= GroundContactDistance;
}
