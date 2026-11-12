#pragma once

#include "Animation/AnimTypes.h"

struct FAnimationPose;
struct FAnimationCurve;

struct FAnimationRuntime
{
	// 두 포즈를 Alpha 로 섞는다.
	static void BlendTwoPosesTogether(
	    const FAnimationPose& PoseA, const FAnimationPose& PoseB,
	    const FAnimationCurve& CurveA, const FAnimationCurve& CurveB,
	    float WeightA, FAnimationPose& OutPose, FAnimationCurve& OutCurve);

	// 정규화 시간(0~1)을 MoveDelta만큼 진행한다. 되감기·클램프 규칙은 이 한 곳에만 둔다.
	static ETypeAdvanceAnim AdvanceTime(bool bAllowLooping, float MoveDelta, float& InOutTime, float& OutMovedDelta);
};