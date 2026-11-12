#pragma once 

#include "Containers/Map.h"
#include "Core/NameTypes.h"
#include "AnimationPose.h"

class UAnimInstance;
struct FAnimationPose;

struct FAnimationUpdateContext
{
	UAnimInstance* AnimInstance = nullptr;
	float DeltaSeconds = 0.0f;

	// 이 가지가 최종 포즈에 섞이는 비율. 블렌드 노드를 지날 때마다 곱해져 내려간다
	float BlendWeight = 1.0f;

	// 이전에 적용한 BlendSpace 입력으로 계속 재생한다.
	bool bFreezeBlendSpaceInput = false;

	// Context는 const로 받으니, 자식마다 비율만 바꾼 복사본을 만들어 넘긴다
	FAnimationUpdateContext FractionalWeight(float Weight) const
	{
		FAnimationUpdateContext Child = *this;
		Child.BlendWeight = BlendWeight * Weight;
		return Child;
	}
};

struct FAnimationCurve
{
	TMap<FName, float, FNameHash> Values;
	
	void Reset();
};

struct FPoseContext
{
	UAnimInstance* AnimInstance = nullptr;
	FAnimationPose Pose;
	FAnimationCurve Curve;

	FPoseContext() = default;

	explicit FPoseContext(UAnimInstance& InInstance);

	// 같은 평가 환경과 본 구성을 갖는 독립된 출력 버퍼 생성.
	// Pose 데이터는 RefPose, Curve는 빈 값으로 초기화한다.
	FPoseContext(const FPoseContext& Other);

	void ResetToRefPose();
};