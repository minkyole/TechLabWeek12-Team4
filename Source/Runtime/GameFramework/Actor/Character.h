#pragma once

#include "GameFramework/Actor.h"

class UCapsuleComponent;
class USkeletalMeshComponent;
class UCharacterMovementComponent;
class UScriptComponent;
class UCameraComponent;
class ULuaAnimInstance;
class UStateMachineAnimInstance;

class ACharacter : public AActor
{
	DECLARE_CLASS(ACharacter, AActor)
public:
	ACharacter();

	void BeginPlay() override;

	UCapsuleComponent* GetCapsuleComponent() const { return CapsuleComponent; }
	USkeletalMeshComponent* GetMesh() const { return SkeletalMeshComponent; }
	UCharacterMovementComponent* GetCharacterMovement() const { return MovementComponent; }
	UScriptComponent* GetScriptComponent() const { return ScriptComponent; }
	UCameraComponent* GetCameraComponent() const { return CameraComponent; }
	ULuaAnimInstance* GetLuaAnimInstance() const;
	UStateMachineAnimInstance* GetStateMachineAnimInstance() const;

	void HandleAnimNotify(const struct FAnimNotifyEvent& Notify);
	void HandleAnimNotifyStateBegin(const FAnimNotifyEvent& Notify);
	void HandleAnimNotifyStateEnd(const FAnimNotifyEvent& Notify);
	void HandleAnimNotifyStateTick(const FAnimNotifyEvent& Notify, float DeltaSeconds);

private:
	// Actor의 기본 컴포넌트를 참조한다.
	// 여기서 별도로 delete하지 않는다.
	UCapsuleComponent* CapsuleComponent = nullptr;
	USkeletalMeshComponent* SkeletalMeshComponent = nullptr;
	UCharacterMovementComponent* MovementComponent = nullptr;
	UScriptComponent* ScriptComponent = nullptr;
	UCameraComponent* CameraComponent = nullptr;
};