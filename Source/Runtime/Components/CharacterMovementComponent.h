#pragma once

#include "Components/MovementComponent.h"

struct FHitResult;

class UCharacterMovementComponent : public UMovementComponent
{
	DECLARE_CLASS(UCharacterMovementComponent, UMovementComponent)

public:
	UCharacterMovementComponent();

	void BeginPlay() override;
	void TickComponent(float DeltaTime) override;

	// 월드 좌표 기준 방향.
	// 값은 다음 SetMoveInput 또는 Stop까지 유지된다.
	void SetMoveInput(const FVector& WorldDirection);
	void SetMaxSpeed(float InMaxSpeed);

	void SetAcceleration(float InAcceleration);
	void SetBrakingDeceleration(float InDeceleration);

	void SetVerticalVelocity(float Value);
	float GetVerticalVelocity() const;

	void SetGravityAcceleration(float InGravity);
	void SetGravityScale(float InScale);

	FVector GetVelocity() const { return Velocity; }
	float GetGroundSpeed() const;

	bool IsGrounded() const { return bIsGrounded; }

	void StopMovementImmediately() override;

private:
	bool FindFloor(FHitResult& OutHit) const;
	void UpdateGroundedState();

private:
	FVector MoveInput = FVector::ZeroVector;

	float MaxSpeed = 5.0f;
	float Acceleration = 20.0f;
	float BrakingDeceleration = 30.0f;

	// Gravity는 부모 클래스의 기존 멤버를 사용한다.
	float GravityScale = 1.0f;
	float MaxFallSpeed = 30.0f;


	bool bIsGrounded = false;

	// 캐릭터 충돌체 전체를 이 거리만큼 아래로 Sweep하여 지면 충돌 여부를 판단한다.
	float GroundProbeDistance = 0.05f;
	// 바닥 탐색 거리와 실제 접지 허용 거리를 구분한다.
	float GroundContactDistance = 0.003f;

	// 걸을 수 있는 최대 경사 약 45도.
	// cos(45) = 0.7071
	float WalkableFloorZ = 0.70710678;

	// 바닥에 아주 조금 겹친 경우의 허용 오차.
	float GroundPenetrationTolerance = 0.005f;
};