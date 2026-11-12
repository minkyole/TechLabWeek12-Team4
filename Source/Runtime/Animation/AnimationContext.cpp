#include "EnginePCH.h"
#include "AnimationContext.h"

#include "AnimInstance.h"

void FAnimationCurve::Reset()
{
	Values.Reset();
}

FPoseContext::FPoseContext(UAnimInstance& InInstance)
    : AnimInstance(&InInstance)
{
	const USkeletalMesh* Mesh = InInstance.GetSkeletalMesh();

	if (Mesh)
	{
		Pose.Initialize(*Mesh);
	}

	// Pose는 메시가 없으면 기본 상태:
	// TargetMesh == nullptr, LocalTransforms는 빈 배열.
	// Curve도 기본 생성 시 빈 상태다.
}

FPoseContext::FPoseContext(const FPoseContext& Other)
	: AnimInstance(Other.AnimInstance)
{
	// 부모 Context와 동일한 대상 메시를 사용한다.
	// 부모가 계산한 자세 자체는 복사하지 않는다.
	if (Other.Pose.TargetMesh)
	{
		Pose.Initialize(*Other.Pose.TargetMesh);
	}

	// Curve는 기본 생성된 빈 상태를 유지한다.
}

void FPoseContext::ResetToRefPose()
{
	Pose.ResetToRefPose();
	Curve.Reset();
}