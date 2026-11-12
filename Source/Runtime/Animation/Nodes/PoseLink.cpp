#include "EnginePCH.h"
#include "PoseLink.h"

#include "Animation/Nodes/AnimNodes.h"
#include "Animation/AnimationContext.h"

void FPoseLink::OnEnter()
{
	if (LinkedNode)
	{
		LinkedNode->OnEnter();
	}
}

void FPoseLink::Update(const FAnimationUpdateContext& Context)
{
	if (LinkedNode)
	{
		LinkedNode->Update(Context);
	}
}

void FPoseLink::Evaluate(FPoseContext& Output)
{
	if (LinkedNode)
	{
		LinkedNode->Evaluate(Output);
		return;
	}

	// 연결되지 않은 입력은 기준 포즈로 평가한다.
	// Output은 대상 메시가 설정된 컨텍스트여야 한다.
	Output.ResetToRefPose();
}
