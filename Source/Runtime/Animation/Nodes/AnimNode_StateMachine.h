#pragma once

#include "Animation/Nodes/AnimNodes.h"
#include "Animation/AnimCondition.h"
#include "Animation/AnimParameters.h"

using FAnimStateId = int32;
using FAnimTransitionId = int32;

inline constexpr FAnimStateId InvalidAnimStateId = -1;
inline constexpr FAnimTransitionId InvalidAnimTransitionId = -1;

enum class EAnimBlendCurve : uint8
{
	Linear,
	SmoothStep,
};

struct FAnimBlendSettings
{
	float Duration = 0.2f;
	EAnimBlendCurve Curve = EAnimBlendCurve::Linear;
};

struct FAnimStateDefinition
{
	FName Name;

	// 캐릭터별 런타임 노드 배열에서 사용하는 인덱스
	int32 RootNodeIndex = -1;
};

enum class EAnimTransitionInterruptPolicy : uint8
{
	Never,
	HigherPriorityOnly,
	Always,
};

struct FAnimTransitionDefinition
{
	FAnimStateId From = InvalidAnimStateId;
	FAnimStateId To = InvalidAnimStateId;

	// true이면 From을 무시하고 모든 상태에서 사용할 수 있다.
	bool bFromAnyState = false;

	FAnimConditionId Condition = InvalidAnimConditionId;
	int32 Priority = 0;

	FAnimBlendSettings Blend;

	// 현재 상태가 To여도 재진입할 것인지.
	bool bRestartIfSameState = false;

	// 이 전환이 진행 중일 때 다른 전환을 허용하는가 기준.
	EAnimTransitionInterruptPolicy InterruptPolicy =
	    EAnimTransitionInterruptPolicy::Always;

	// 이 전환이 실제로 실행되었을 때 소비할 Trigger
	TArray<FName> ConsumeTriggers;
};

struct FAnimStateMachineDefinition
{
	FAnimStateId EntryState = InvalidAnimStateId;

	TArray<FAnimStateDefinition> States;
	TArray<FAnimConditionDefinition> Conditions;
	TArray<FAnimTransitionDefinition> Transitions;
};

struct FAnimConditionContext
{
	const FAnimParameterSet& Parameters;

	bool bSourceFinished = false;
	float StateElpased = 0.0f;
};

class UMyAnimInstance;

struct FAnimNode_StateMachine : public FAnimNode_Base
{
	bool Initialize(
	    const FAnimStateMachineDefinition& InDefinition,
	    const TArray<FAnimNode_Base*>& InNodes);

	void OnEnter() override;
    void OnExit() override;
    bool IsFinished() const override { return IsClipFinished(); }

	void Update(const FAnimationUpdateContext& Context) override;
	void Evaluate(FPoseContext& Output) override;

	FAnimStateId GetCurrentState() const { return CurrentState; }
	bool IsTransitioning() const { return bTransitioning; }
 int GetActiveTransitionIndex() const { return ActiveTransitionIndex; }
	float GetTransitionProgress() const { return bTransitioning && BlendDuration > 0 ? std::clamp(BlendElapsed / BlendDuration, 0.0f, 1.0f) : 1.0f; }

	// void ProcessState(UMyAnimInstance* AnimInstance);

	bool IsClipFinished() const;

private:
	FAnimNode_Base* GetStateNode(FAnimStateId StateId) const;

	bool TryTransition(FAnimTransitionId TransitionId);
	void ChangeState(const FAnimTransitionDefinition& Transition);
	void ClearTransition();

	bool CanTakeTransition(
	    const FAnimTransitionDefinition& Transition) const;

	void ProcessTransitions();

public:
	FAnimParameterSet Parameters;
    FAnimParameterSet* SharedParameters = nullptr;
    FAnimParameterSet& GetParameters() { return SharedParameters ? *SharedParameters : Parameters; }

private:
	// 비소유 참조. 실행 중에는 정의를 수정하지 않는다.
	const FAnimStateMachineDefinition* Definition = nullptr;

	// 배열은 이 상태 머신이 소유하지만 노드 자체는 소유하지 않는다.
	TArray<FAnimNode_Base*> Nodes;

	FAnimStateId CurrentState = InvalidAnimStateId;

	TArray<uint8> ConditionResults;
	float StateElpased = 0.0f;
	int32 ActiveTransitionPriority = 0;
 int32 ActiveTransitionIndex = -1;
	EAnimTransitionInterruptPolicy ActiveInterruptPolicy =
	    EAnimTransitionInterruptPolicy::Always;

	// 일반 전환의 출발 노드. 비소유 참조.
	FAnimNode_Base* PreviousNode = nullptr;

	bool bTransitioning = false;
	bool bUseSnapshot = false;

	float BlendElapsed = 0.0f;
	float BlendDuration = 0.0f;
	EAnimBlendCurve BlendCurve = EAnimBlendCurve::Linear;

	// 매 Evaluate에서 저장하는 최종 출력.
	bool bHasLastOutput = false;
	FAnimationPose LastOutputPose;
	FAnimationCurve LastOutputCurve;

	// 전환이 끊긴 순간의 출력. 전환 중에는 고정한다.
	FAnimationPose TransitionSourcePose;
	FAnimationCurve TransitionSourceCurve;
};