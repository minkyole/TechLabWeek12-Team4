#pragma once

#include "Animation/AnimInstance.h"
#include "Animation/Nodes/AnimNode_StateMachine.h"

class UMyAnimInstance : public UAnimInstance
{
	DECLARE_CLASS(UMyAnimInstance, UAnimInstance)
public:
	~UMyAnimInstance() override;

	float Speed = 0.0f;
	bool bIsAttacking = false;
	bool bIsHit = false;

	// 상태마다 포즈를 내는 노드
	FAnimNode_StateMachine StateMachine;
	FAnimNode_BlendSpacePlayer LocomotionPlayer; // Locomotion: Idle / Walk / Run Blend Space
	FAnimNode_SequencePlayer AttackPlayer;       // Attack: 공격 클립 한 번 재생
	FAnimNode_SequencePlayer HitPlayer;          // Hit: 피격 클립 한 번 재생

protected:
	void NativeInitializeAnimation() override;
	void NativeUnInitializeAnimation() override;
	void NativeUpdateAnimation(float DeltaSeconds) override;

private:
	UAnimSequence* CreateRootTestClip(
	    int32 RootBoneIndex, float DurationSeconds, const FVector& Offset);
	void ReleaseTestAssets();
	//void ProcessStateTransition();

	// 테스트용으로 생성한 에셋은 이 인스턴스가 소유한다.
	UBlendSpace1D* TestBlendSpace = nullptr;
	UAnimSequence* IdleClip = nullptr;
	UAnimSequence* WalkClip = nullptr;
	UAnimSequence* RunClip = nullptr;
	UAnimSequence* AttackClip = nullptr;
	UAnimSequence* HitClip = nullptr;

	// 상태들
	FAnimStateMachineDefinition StateMachineDefinition;

	FAnimTransitionId HitTransitionId = InvalidAnimTransitionId;
	FAnimTransitionId AttackTransitionId = InvalidAnimTransitionId;
	FAnimTransitionId AttackReturnTransitionId = InvalidAnimTransitionId;
	FAnimTransitionId HitReturnTransitionId = InvalidAnimTransitionId;
};
