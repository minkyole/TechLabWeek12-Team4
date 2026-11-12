#include "EnginePCH.h"
#include "AnimNode_StateMachine.h"

#include "Animation/MyAnimInstance.h"

#include <algorithm>
#include <cmath>

namespace
{
	float EvaluateBlendAlpha(
	    EAnimBlendCurve Curve,
	    float Progress)
	{
		const float T = std::clamp(Progress, 0.0f, 1.0f);

		switch (Curve)
		{
		case EAnimBlendCurve::SmoothStep:
			return T * T * (3.0f - 2.0f * T);
		case EAnimBlendCurve::Linear:
		default:
			return T;
		}
	}

	bool CompareAnimFloat(
	    float Left,
	    EAnimCompareOp Op,
	    float Right,
	    float Tolerance)
	{
		switch (Op)
		{
		case EAnimCompareOp::Less:
			return Left < Right;

		case EAnimCompareOp::LessEqual:
			return Left <= Right;

		case EAnimCompareOp::Greater:
			return Left > Right;

		case EAnimCompareOp::GreaterEqual:
			return Left >= Right;

		case EAnimCompareOp::NearlyEqual:
			return std::abs(Left - Right) <= Tolerance;

		case EAnimCompareOp::NotNearlyEqual:
			return std::abs(Left - Right) > Tolerance;
		}

		return false;
	}

	bool EvaluateAnimConditions(
	    const TArray<FAnimConditionDefinition>& Definitions,
	    const FAnimConditionContext& Context,
	    TArray<uint8>& Results)
	{
		// Results는 초기화 시 Definitions.Num() 크기로 준비한다.
		if (Results.Num() != Definitions.Num())
			return false;

		for (int32 Index = 0; Index < Definitions.Num(); ++Index)
		{
			const FAnimConditionDefinition& Condition = Definitions[Index];

			bool Result = false;

			switch (Condition.Type)
			{
			case EAnimConditionType::Always:
				Result = true;
				break;

			case EAnimConditionType::BoolEquals:
			{
				bool Value = false;

				if (!Context.Parameters.TryGetBool(
				        Condition.ParameterName, Value))
				{
					return false;
				}

				Result = Value == Condition.BoolValue;
				break;
			}

			case EAnimConditionType::FloatCompare:
			{
				float Value = 0.0f;

				if (!Context.Parameters.TryGetFloat(
				        Condition.ParameterName, Value))
				{
					return false;
				}

				Result = CompareAnimFloat(
				    Value,
				    Condition.CompareOp,
				    Condition.FloatValue,
				    Condition.Tolerance);
				break;
			}

			case EAnimConditionType::TriggerActive:
			{
				if (!Context.Parameters.TryGetTrigger(
				        Condition.ParameterName, Result))
				{
					return false;
				}

				break;
			}

			case EAnimConditionType::SourceFinished:
				Result = Context.bSourceFinished;
				break;

			case EAnimConditionType::StateElapsedCompare:
				Result = CompareAnimFloat(
				    Context.StateElpased,
				    Condition.CompareOp,
				    Condition.FloatValue,
				    Condition.Tolerance);
				break;

			case EAnimConditionType::And:
				Result = true;

				for (FAnimConditionId Child : Condition.Children)
					Result = Result && (Results[Child] != 0);
				break;

			case EAnimConditionType::Or:
				Result = false;

				for (FAnimConditionId Child : Condition.Children)
					Result = Result || (Results[Child] != 0);

				break;

			case EAnimConditionType::Not:
				Result = Results[Condition.Children[0]] == 0;
				break;

			case EAnimConditionType::Custom:
			{

				if (!Condition.Callback)
					return false;

				Result = Condition.Callback(Context);
				break;
			}

			default:
				return false;
			}

			Results[Index] = Result ? 1 : 0;
		}
		return true;
	}

	bool ValidateAnimConditions(
	    const FAnimStateMachineDefinition& Definition)
	{
		for (int32 Index = 0;
		     Index < Definition.Conditions.Num();
		     ++Index)
		{
			const FAnimConditionDefinition& Condition =
			    Definition.Conditions[Index];

			switch (Condition.Type)
			{
			case EAnimConditionType::And:
			case EAnimConditionType::Or:
				if (Condition.Children.Num() == 0)
					return false;
				break;

			case EAnimConditionType::Not:
				if (Condition.Children.Num() != 1)
					return false;
				break;
			
			case EAnimConditionType::Custom:
			{
				if (Condition.Children.Num() != 0)
					return false;

				if (!Condition.Callback)
					return false;

				break;
			}

			case EAnimConditionType::Always:
			case EAnimConditionType::BoolEquals:
			case EAnimConditionType::FloatCompare:
			case EAnimConditionType::TriggerActive:
			case EAnimConditionType::SourceFinished:
			case EAnimConditionType::StateElapsedCompare:
				if (Condition.Children.Num() != 0)
					return false;
				break;

			default:
				return false;
			}

			for (FAnimConditionId Child : Condition.Children)
			{
				// 유효 인덱스 검사와 순환 방지를 동시에 수행
				if (Child < 0 || Child >= Index)
					return false;
			}

			if (Condition.Type == EAnimConditionType::FloatCompare ||
			    Condition.Type ==
			        EAnimConditionType::StateElapsedCompare)
			{
				if (!std::isfinite(Condition.FloatValue) ||
				    !std::isfinite(Condition.Tolerance) ||
				    Condition.Tolerance < 0.0f)
				{
					return false;
				}

				switch (Condition.CompareOp)
				{
				case EAnimCompareOp::Less:
				case EAnimCompareOp::LessEqual:
				case EAnimCompareOp::Greater:
				case EAnimCompareOp::GreaterEqual:
				case EAnimCompareOp::NearlyEqual:
				case EAnimCompareOp::NotNearlyEqual:
					break;

				default:
					return false;
				}
			}
		}

		for (const FAnimTransitionDefinition& Transition : Definition.Transitions)
		{
			if (!Definition.Conditions.IsValidIndex(
			        Transition.Condition))
			{
				return false;
			}

			switch (Transition.InterruptPolicy)
			{
			case EAnimTransitionInterruptPolicy::Never:
			case EAnimTransitionInterruptPolicy::HigherPriorityOnly:
			case EAnimTransitionInterruptPolicy::Always:
				break;

			default:
				return false;
			}
		}

		return true;
	}
}

bool FAnimNode_StateMachine::Initialize(
    const FAnimStateMachineDefinition& InDefinition,
    const TArray<FAnimNode_Base*>& InNodes)
{
	ClearTransition();

	if (!ValidateAnimConditions(InDefinition))
		return false;

	ConditionResults = {};

	for (int32 Index = 0;
	     Index < InDefinition.Conditions.Num();
	     ++Index)
	{
		ConditionResults.Add(0);
	}

	Definition = nullptr;
	Nodes = {};
	CurrentState = InvalidAnimStateId;

	bHasLastOutput = false;
	LastOutputPose = FAnimationPose();
	LastOutputCurve.Reset();

	if (!InDefinition.States.IsValidIndex(InDefinition.EntryState))
		return false;

	for (const FAnimStateDefinition& State : InDefinition.States)
	{
		if (!InNodes.IsValidIndex(State.RootNodeIndex))
			return false;

		if (!InNodes[State.RootNodeIndex])
			return false;
	}

	for (const FAnimTransitionDefinition& Transition : InDefinition.Transitions)
	{
		if (!InDefinition.States.IsValidIndex(Transition.To))
			return false;

		if (!Transition.bFromAnyState &&
		    !InDefinition.States.IsValidIndex(Transition.From))
		{
			return false;
		}

		if (!std::isfinite(Transition.Blend.Duration) ||
		    Transition.Blend.Duration < 0.0f)
		{
			return false;
		}

		if (Transition.Blend.Curve != EAnimBlendCurve::Linear &&
		    Transition.Blend.Curve != EAnimBlendCurve::SmoothStep)
		{
			return false;
		}
	}

	Definition = &InDefinition;
	Nodes = InNodes;

	return true;
}

FAnimNode_Base* FAnimNode_StateMachine::GetStateNode(
    FAnimStateId StateId) const
{
	if (!Definition || !Definition->States.IsValidIndex(StateId))
		return nullptr;

	const int32 NodeIndex =
	    Definition->States[StateId].RootNodeIndex;

	if (!Nodes.IsValidIndex(NodeIndex))
		return nullptr;

	return Nodes[NodeIndex];
}

void FAnimNode_StateMachine::OnEnter()
{
	ClearTransition();

	StateElpased = 0.0f;

	bHasLastOutput = false;
	LastOutputPose = FAnimationPose();
	LastOutputCurve.Reset();

	CurrentState = InvalidAnimStateId;

	FAnimNode_Base* EntryNode =
	    GetStateNode(Definition->EntryState);

	if (!EntryNode)
	{
		assert(false);
		return;
	}

	CurrentState = Definition->EntryState;
	EntryNode->OnEnter(); // 초기 상태 노드에 진입
}

void FAnimNode_StateMachine::Update(const FAnimationUpdateContext& Context)
{
	const float DeltaSeconds =
	    std::isfinite(Context.DeltaSeconds)
	        ? std::max(Context.DeltaSeconds, 0.0f)
	        : 0.0f;

	ProcessTransitions();

	FAnimNode_Base* CurrentNode = GetStateNode(CurrentState);

	if (!CurrentNode)
	{
		assert(false);
		return;
	}

	// 일반 크로스페이드: 이전 애니메이션도 계속 진행한다.
	// 스냅샷 전환: 이전 포즈가 고정되어 있으므로 업데이트 하지 않는다.
	// Evaluate는 시간을 더한 뒤의 Alpha로 섞으니, 루트 이동 비율도 같은 시점 값으로 맞춘다
	float Alpha = 1.0f;
	if (bTransitioning)
	{
		Alpha = BlendDuration > 0.0f
		            ? EvaluateBlendAlpha(BlendCurve, (BlendElapsed + DeltaSeconds) / BlendDuration)
		            : 1.0f;
	}

	if (bTransitioning && !bUseSnapshot && PreviousNode && Alpha < 1.0f)
	{
		FAnimationUpdateContext PreviousContext =
		    Context.FractionalWeight(1.0f - Alpha);

		PreviousContext.bFreezeBlendSpaceInput = true;

		PreviousNode->Update(PreviousContext);
	}

	// 스냅샷 전환은 이전 포즈가 멈춰 있어 이동이 없으니, 현재 상태도 섞이는 비율만큼만 이동한다
	if (Alpha > 0.0f) CurrentNode->Update(Context.FractionalWeight(Alpha));

	StateElpased += DeltaSeconds;

	if (bTransitioning)
	{
		BlendElapsed += DeltaSeconds;

		if (BlendElapsed >= BlendDuration)
		{
			ClearTransition();
		}
	}
}

void FAnimNode_StateMachine::Evaluate(FPoseContext& Output)
{
	FAnimNode_Base* CurrentNode = GetStateNode(CurrentState);

	if (!CurrentNode)
	{
		assert(false);
		Output.ResetToRefPose();
		bHasLastOutput = false;
		return;
	}

	// 복사 생성자는 동일한 평가 환경의 기준 포즈 버퍼를 만든다.
	FPoseContext CurrentPose(Output);
	CurrentNode->Evaluate(CurrentPose);

	if (bTransitioning)
	{
		FPoseContext SourcePose(Output);
		bool bSourceValid = false;

		if (bUseSnapshot)
		{
			// 메시 교체 등으로 다른 스켈레톤의 포즈가 섞이지 않게 검사.
			bSourceValid =
			    TransitionSourcePose.TargetMesh ==
			        CurrentPose.Pose.TargetMesh &&
			    TransitionSourcePose.LocalTransforms.Num() ==
			        CurrentPose.Pose.LocalTransforms.Num();

			if (bSourceValid)
			{
				// Context 복사 생성 대신 실제 포즈와 커브를 대입한다.
				SourcePose.Pose = TransitionSourcePose;
				SourcePose.Curve = TransitionSourceCurve;
			}
		}
		else if (PreviousNode)
		{
			PreviousNode->Evaluate(SourcePose);
			bSourceValid = true;
		}

		if (bSourceValid)
		{
			const float Alpha = EvaluateBlendAlpha(
			    BlendCurve,
			    BlendElapsed / BlendDuration);

			// 이 프로젝트의 함수는 첫 번째 포즈의 가중치를 받는다.
			FAnimationRuntime::BlendTwoPosesTogether(
			    SourcePose.Pose, CurrentPose.Pose,
			    SourcePose.Curve, CurrentPose.Curve,
			    1.0f - Alpha, Output.Pose, Output.Curve);
		}
		else
		{
			Output.Pose = CurrentPose.Pose;
			Output.Curve = CurrentPose.Curve;
		}
	}
	else // 전환 중이 아니면 현재 상태의 포즈를 그대로 출력한다.
	{
		Output.Pose = CurrentPose.Pose;
		Output.Curve = CurrentPose.Curve;
	}

	// 반드시 혼합이 끝난 최종 출력을 저장한다.
	LastOutputPose = Output.Pose;
	LastOutputCurve = Output.Curve;
	bHasLastOutput = Output.Pose.TargetMesh != nullptr;
}

bool FAnimNode_StateMachine::TryTransition(
    FAnimTransitionId TransitionId)
{
	if (!Definition ||
	    !Definition->Transitions.IsValidIndex(TransitionId))
	{
		return false;
	}

	const FAnimTransitionDefinition& Transition =
	    Definition->Transitions[TransitionId];

	if (!CanTakeTransition(Transition))
		return false;

	ChangeState(Transition);
	return true;
}

bool FAnimNode_StateMachine::IsClipFinished() const
{
	const FAnimNode_Base* CurrentNode = GetStateNode(CurrentState);
	return CurrentNode && CurrentNode->IsFinished();
}

void FAnimNode_StateMachine::ChangeState(
    const FAnimTransitionDefinition& Transition)
{
	FAnimNode_Base* NewNode = GetStateNode(Transition.To);
	FAnimNode_Base* OldNode = GetStateNode(CurrentState);

	const float RequestedDuration = Transition.Blend.Duration;

	// 진행 중인 혼합을 끊거나 같은 플레이어를 재시작할 때는
	// 노드 두 개로 이전 출력을 재현할 수 없으므로 스냅샷 사용.
	const bool bNeedsSnapshot =
	    bTransitioning || OldNode == NewNode;

	const bool bCanBlend =
	    RequestedDuration > 0.0f &&
	    (!bNeedsSnapshot || bHasLastOutput);

	ClearTransition();

	if (bCanBlend)
	{
		bTransitioning = true;
        ActiveTransitionIndex = static_cast<int32>(&Transition - &Definition->Transitions[0]);
		BlendDuration = RequestedDuration;
		BlendCurve = Transition.Blend.Curve;
		bUseSnapshot = bNeedsSnapshot;

		if (bUseSnapshot)
		{
			TransitionSourcePose = LastOutputPose;
			TransitionSourceCurve = LastOutputCurve;
		}
		else
		{
			PreviousNode = OldNode;
		}
	}

	if (OldNode && (bUseSnapshot || !bTransitioning)) OldNode->OnExit();
	CurrentState = Transition.To;
	StateElpased = 0.0f;

	if (bTransitioning)
	{
		ActiveTransitionPriority = Transition.Priority;
		ActiveInterruptPolicy = Transition.InterruptPolicy;
	}

	NewNode->OnEnter();
}

void FAnimNode_StateMachine::ClearTransition()
{
    if (PreviousNode) PreviousNode->OnExit();
 ActiveTransitionIndex=-1;
	PreviousNode = nullptr;
	bTransitioning = false;
	bUseSnapshot = false;

	BlendElapsed = 0.0f;
	BlendDuration = 0.0f;
	BlendCurve = EAnimBlendCurve::Linear;

	TransitionSourcePose = FAnimationPose();
	TransitionSourceCurve.Reset();

	ActiveTransitionPriority = 0;
	ActiveInterruptPolicy = EAnimTransitionInterruptPolicy::Always;
}

bool FAnimNode_StateMachine::CanTakeTransition(
    const FAnimTransitionDefinition& Transition) const
{
	if (!GetStateNode(CurrentState) ||
	    !GetStateNode(Transition.To))
	{
		return false;
	}

	if (!Transition.bFromAnyState &&
	    Transition.From != CurrentState)
	{
		return false;
	}

	if (Transition.To == CurrentState &&
	    !Transition.bRestartIfSameState)
	{
		return false;
	}

	if (bTransitioning)
	{
		switch (ActiveInterruptPolicy)
		{
		case EAnimTransitionInterruptPolicy::Never:
			return false;

		case EAnimTransitionInterruptPolicy::HigherPriorityOnly:
			if (Transition.Priority <= ActiveTransitionPriority)
				return false;
			break;

		case EAnimTransitionInterruptPolicy::Always:
			break;
		}
	}
	return true;
}

void FAnimNode_StateMachine::ProcessTransitions()
{
	if (!Definition)
		return;

	const FAnimNode_Base* CurrentNode =
	    GetStateNode(CurrentState);

	if (!CurrentNode)
		return;

	const FAnimConditionContext Context{
		GetParameters(),
		CurrentNode->IsFinished(),
		StateElpased,
	};

	if (!EvaluateAnimConditions(
	        Definition->Conditions,
	        Context,
	        ConditionResults))
	{
		// 실제 적용 시 잘못된 파라미터 등에 대한 진단을 남긴다.
		return;
	}

	FAnimTransitionId Selected = InvalidAnimTransitionId;

	for (int32 Index = 0;
	     Index < Definition->Transitions.Num();
	     ++Index)
	{
		const FAnimTransitionDefinition& Transition =
		    Definition->Transitions[Index];

		if (!CanTakeTransition(Transition))
			continue;

		if (ConditionResults[Transition.Condition] == 0)
			continue;

		if (Selected == InvalidAnimTransitionId ||
		    Transition.Priority >
		        Definition->Transitions[Selected].Priority)
		{
			Selected = Index;
		}
	}

	if (Selected == InvalidAnimTransitionId)
		return;

	if (!TryTransition(Selected))
		return;

	const FAnimTransitionDefinition& Transition =
	    Definition->Transitions[Selected];

	for (const FName& TriggerName : Transition.ConsumeTriggers)
		GetParameters().ConsumeTrigger(TriggerName);
}
void FAnimNode_StateMachine::OnExit()
{
    if (auto* Node = GetStateNode(CurrentState)) Node->OnExit();
    ClearTransition();
}
