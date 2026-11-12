#include "EnginePCH.h"
#include "MyAnimInstance.h"

#include "Animation/AnimSequence.h"
#include "Animation/AnimCondition.h"
#include "Render/Mesh.h"
#include "UObject/UObjectGlobals.h"

#include <algorithm>
#include <cmath>

UMyAnimInstance::~UMyAnimInstance()
{
	ReleaseTestAssets();
}

UAnimSequence* UMyAnimInstance::CreateRootTestClip(
    int32 RootBoneIndex, float DurationSeconds, const FVector& Offset)
{
	const USkeletalMesh* Mesh = GetSkeletalMesh();
	if (!Mesh || !Mesh->MeshData.Bones.IsValidIndex(RootBoneIndex))
		return nullptr;

	UAnimSequence* Clip = NewObject<UAnimSequence>(this);
	Clip->SetSkeletalMesh(Mesh);
	UAnimDataModel* Model = Clip->GetDataModel();
	constexpr int32 FPS = 30;
	Model->SetFrameRate(FFrameRate(FPS, 1));
	Model->SetNumberOfFrames(std::max(
	    1, static_cast<int32>(std::round(DurationSeconds * FPS))));

	const FSkeletalBone& RootBone = Mesh->MeshData.Bones[RootBoneIndex];
	const int32 TrackIndex = Model->AddBoneTrack(FName(RootBone.Name));
	TArray<FVector> Positions;
	const int32 NumKeys = Model->GetNumberOfKeys();
	Positions.Reserve(NumKeys);
	constexpr float TwoPi = 6.28318530718f;
	for (int32 Key = 0; Key < NumKeys; ++Key)
	{
		const float Phase = static_cast<float>(Key) / (NumKeys - 1);
		// 시작/끝은 기준 위치, 중간은 Offset. 반복 경계에서도 연속적이다.
		const float Weight = (Key == 0 || Key == NumKeys - 1)
		                         ? 0.0f
		                         : 0.5f - 0.5f * std::cos(TwoPi * Phase);
		Positions.Add(RootBone.RefPoseTransform.Location + Offset * Weight);
	}
	// 회전/스케일과 나머지 본의 로컬 변환은 기준 포즈를 유지한다.
	Model->SetBoneTrackKeys(TrackIndex, Positions, {}, {});
	return Clip;
}

void UMyAnimInstance::NativeInitializeAnimation()
{
	ReleaseTestAssets();
	bIsAttacking = false;
	bIsHit = false;
	// StateMachine.State = EAnimState::Locomotion;

	const USkeletalMesh* Mesh = GetSkeletalMesh();
	if (!Mesh)
		return;

	int32 RootBoneIndex = 1;

	// 메시 로컬 X: 앞뒤, Z: 위아래. 액터 이동이 아닌 루트 본 이동 테스트.
	IdleClip = CreateRootTestClip(RootBoneIndex, 1.0f, FVector(0, 0, 0));
	WalkClip = CreateRootTestClip(RootBoneIndex, 1.0f, FVector(0, 0, 0.5f));
	RunClip = CreateRootTestClip(RootBoneIndex, 0.5f, FVector(0, 0, 1.2f));
	AttackClip = CreateRootTestClip(RootBoneIndex, 0.6f, FVector(3.0f, 0, 0));
	HitClip = CreateRootTestClip(RootBoneIndex, 0.4f, FVector(-1.5f, 0, 0));
	if (!IdleClip || !WalkClip || !RunClip || !AttackClip || !HitClip)
	{
		ReleaseTestAssets();
		return;
	}

	TestBlendSpace = NewObject<UBlendSpace1D>(this);
	TestBlendSpace->SetSkeletalMesh(Mesh);
	FBlendParameter SpeedAxis;
	SpeedAxis.Name = FName("Speed");
	SpeedAxis.Min = 0.0f;
	SpeedAxis.Max = 600.0f;
	const auto AddSample = [this](UAnimSequence* Clip, float SampleSpeed)
	{
		FBlendSample Sample;
		Sample.Animation = Clip;
		Sample.SampleValue = FVector(SampleSpeed, 0, 0);
		return TestBlendSpace->AddSample(Sample);
	};
	if (!TestBlendSpace->SetBlendParameter(0, SpeedAxis) ||
	    !AddSample(IdleClip, 0.0f) || !AddSample(WalkClip, 300.0f) ||
	    !AddSample(RunClip, 600.0f))
	{
		ReleaseTestAssets();
		return;
	}

	LocomotionPlayer.BlendSpace = TestBlendSpace;
	LocomotionPlayer.bLoop = true;
	LocomotionPlayer.bPlaying = true;
	LocomotionPlayer.PlayRate = 1.0f;
	AttackPlayer.Sequence = AttackClip;
	HitPlayer.Sequence = HitClip;
	AttackPlayer.bLoop = HitPlayer.bLoop = false;
	AttackPlayer.bPlaying = HitPlayer.bPlaying = true;
	AttackPlayer.PlayRate = HitPlayer.PlayRate = 1.0f;

	StateMachineDefinition = {};

	auto AddState = [&](const char* Name, int32 NodeIndex)
	{
		const FAnimStateId Id = StateMachineDefinition.States.Num();

		FAnimStateDefinition State;
		State.Name = FName(Name);
		State.RootNodeIndex = NodeIndex;

		StateMachineDefinition.States.Add(State);
		return Id;
	};

	const FAnimStateId Locomotion = AddState("Locomotion", 0);
	const FAnimStateId Attack = AddState("Attack", 1);
	const FAnimStateId Hit = AddState("Hit", 2);

	StateMachineDefinition.EntryState = Locomotion;

	auto AddTransition = [&](
	                         FAnimStateId From,
	                         FAnimStateId To,
	                         float Duration,
	                         bool bAnyState,
	                         bool bRestart)
	{
		const FAnimTransitionId Id =
		    StateMachineDefinition.Transitions.Num();

		FAnimTransitionDefinition Transition;
		Transition.From = From;
		Transition.To = To;
		Transition.bFromAnyState = bAnyState;
		Transition.Blend.Duration = Duration;
		Transition.bRestartIfSameState = bRestart;

		StateMachineDefinition.Transitions.Add(Transition);
		return Id;
	};

	HitTransitionId = AddTransition(
	    InvalidAnimStateId, Hit, 0.1f, true, true);

	AttackTransitionId = AddTransition(
	    Locomotion, Attack, 0.2f, false, false);

	AttackReturnTransitionId = AddTransition(
	    Attack, Locomotion, 0.2f, false, false);

	HitReturnTransitionId = AddTransition(
	    Hit, Locomotion, 0.2f, false, false);

	auto AddCondition =
	    [&](const FAnimConditionDefinition& Condition)
	{
		const FAnimConditionId Id =
		    StateMachineDefinition.Conditions.Num();

		StateMachineDefinition.Conditions.Add(Condition);
		return Id;
	};

	FAnimConditionDefinition HitRequested;
	HitRequested.Type = EAnimConditionType::TriggerActive;
	HitRequested.ParameterName = FName("HitRequested");
	const FAnimConditionId HitConditionId =
	    AddCondition(HitRequested);

	FAnimConditionDefinition AttackRequested;
	AttackRequested.Type = EAnimConditionType::TriggerActive;
	AttackRequested.ParameterName = FName("AttackRequested");
	const FAnimConditionId AttackConditionId =
	    AddCondition(AttackRequested);

	FAnimConditionDefinition Finished;
	Finished.Type = EAnimConditionType::SourceFinished;
	const FAnimConditionId FinishedConditionId =
	    AddCondition(Finished);

	// 복합 조건 추가 가능
	//FAnimConditionDefinition Grounded;
	//Grounded.Type = EAnimConditionType::BoolEquals;
	//Grounded.ParameterName = FName("IsGrounded");
	//Grounded.BoolValue = true;

	//const FAnimConditionId GroundedId = AddCondition(Grounded);

	//FAnimConditionDefinition CanAttack;
	//CanAttack.Type = EAnimConditionType::And;
	//CanAttack.Children.Add(GroundedId);
	//CanAttack.Children.Add(AttackConditionId);

	//const FAnimConditionId CanAttackId = AddCondition(CanAttack);

	//StateMachineDefinition
	//    .Transitions[AttackTransitionId]
	//    .Condition = CanAttackId;

	auto& HitTransition =
	    StateMachineDefinition.Transitions[HitTransitionId];
	HitTransition.Condition = HitConditionId;
	HitTransition.Priority = 100;
	HitTransition.ConsumeTriggers.Add(FName("HitRequested"));

	auto& AttackTransition =
	    StateMachineDefinition.Transitions[AttackTransitionId];

	AttackTransition.Condition = AttackConditionId;
	AttackTransition.Priority = 50;
	AttackTransition.ConsumeTriggers.Add(FName("AttackRequested"));
	
	auto& AttackReturn =
	    StateMachineDefinition.Transitions[AttackReturnTransitionId];
	AttackReturn.Condition = FinishedConditionId;
	AttackReturn.Priority = 0;

	auto& HitReturn =
	    StateMachineDefinition.Transitions[HitReturnTransitionId];
	HitReturn.Condition = FinishedConditionId;
	HitReturn.Priority = 0;

	TArray<FAnimNode_Base*> Nodes;
	Nodes.Add(&LocomotionPlayer);
	Nodes.Add(&AttackPlayer);
	Nodes.Add(&HitPlayer);

	if (!StateMachine.Initialize(StateMachineDefinition, Nodes))
	{
		RootNode = nullptr;
		return;
	}

	RootNode = &StateMachine;
	RootNode->OnEnter();

	StateMachine.Parameters = {};

	StateMachine.Parameters.SetFloat(FName("Speed"), 0.0f);
	StateMachine.Parameters.SetBool(FName("IsGrounded"), true);
	
	StateMachine.Parameters.Trigger(FName("AttackRequested"));
	StateMachine.Parameters.Trigger(FName("HitRequested"));
	StateMachine.Parameters.ClearTriggers();

	// 최초 Evaluate 전에도 BlendSpace 샘플 가중치를 준비한다.
	NativeUpdateAnimation(0.0f);
	FAnimationUpdateContext Context;
	Context.AnimInstance = this;
	Context.DeltaSeconds = 0.0f;
	LocomotionPlayer.Update(Context);
}

void UMyAnimInstance::NativeUnInitializeAnimation()
{
	ReleaseTestAssets();
}

void UMyAnimInstance::NativeUpdateAnimation(float DeltaSeconds)
{
	// 테스트 입력은 공개 Speed / bIsAttacking / bIsHit으로 전달한다.
	const float SafeSpeed = std::isfinite(Speed)
	                         ? std::clamp(Speed, 0.0f, 600.0f)
	                         : 0.0f;

	StateMachine.Parameters.SetFloat(
	    FName("Speed"), SafeSpeed);

	if (bIsAttacking)
	{
		StateMachine.Parameters.Trigger(
		    FName("AttackRequested"));
		bIsAttacking = false;
	}

	if (bIsHit)
	{
		StateMachine.Parameters.Trigger(
		    FName("HitRequested"));
		bIsHit = false;
	}

	LocomotionPlayer.X = SafeSpeed;
}

void UMyAnimInstance::ReleaseTestAssets()
{
	RootNode = nullptr;
	LocomotionPlayer.BlendSpace = nullptr;
	AttackPlayer.Sequence = nullptr;
	HitPlayer.Sequence = nullptr;
	LocomotionPlayer.ResetPlayback();
	AttackPlayer.ResetPlayback();
	HitPlayer.ResetPlayback();
	FAnimationUpdateContext Context;
	Context.AnimInstance = this;
	Context.DeltaSeconds = 0.0f;
	LocomotionPlayer.Update(Context); // 이전 샘플 가중치도 비운다.

	delete TestBlendSpace;
	TestBlendSpace = nullptr;
	const auto DeleteClip = [](UAnimSequence*& Clip)
	{
		// DataModel 정리를 소유 시퀀스에 맡기는 단일 해제
		delete Clip;
		Clip = nullptr;
	};
	DeleteClip(IdleClip);
	DeleteClip(WalkClip);
	DeleteClip(RunClip);
	DeleteClip(AttackClip);
	DeleteClip(HitClip);
}

//void UMyAnimInstance::ProcessStateTransition()
//{
//	if (bIsHit)
//	{
//		if (StateMachine.TryTransition(HitTransitionId))
//		{
//			bIsHit = false;
//		}
//	}
//	else if (
//		bIsAttacking &&
//		StateMachine.TryTransition(AttackTransitionId))
//	{
//		bIsAttacking = false;
//	}
//	else if (StateMachine.IsClipFinished())
//	{
//		if (!StateMachine.TryTransition(AttackReturnTransitionId))
//		{
//			StateMachine.TryTransition(HitReturnTransitionId);
//		}
//	}
//}