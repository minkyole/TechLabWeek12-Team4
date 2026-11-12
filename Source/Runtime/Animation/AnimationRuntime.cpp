#include "EnginePCH.h"
#include "AnimationRuntime.h"

#include "Animation/AnimationPose.h"
#include "Animation/AnimationContext.h"

void FAnimationRuntime::BlendTwoPosesTogether(
    const FAnimationPose& PoseA, const FAnimationPose& PoseB,
    const FAnimationCurve& CurveA, const FAnimationCurve& CurveB,
    float WeightA, FAnimationPose& OutPose, FAnimationCurve& OutCurve)
{
	// 입력과 출력을 같은 객체로 사용하는 것은 지원하지 않는다.
	// 특히 Curve를 지우면 입력까지 지워질 수 있다.
	const bool bSepartedOutputs =
	    &OutPose != &PoseA &&
	    &OutPose != &PoseB &&
	    &OutCurve != &CurveA &&
	    &OutCurve != &CurveB;

	if (!bSepartedOutputs)
	{
		assert(false && "Blend outputs must not alias inputs");
		return;
	}

	if (!PoseA.IsCompatibleWith(PoseB) ||
	    !std::isfinite(WeightA))
	{
		assert(false && "Invalid animation blend inputs");

		OutPose = FAnimationPose{};
		OutCurve.Reset();
		return;
	}

	const float WA = FMath::Clamp01(WeightA);
	const float WB = 1.0f - WA;

	// 끝점에서는 해당 입력을 그대로 사용한다.
	if (WA == 1.0f)
	{
		OutPose = PoseA;
		OutCurve = CurveA;
		return;
	}
	else if (WB == 1.0f)
	{
		OutPose = PoseB;
		OutCurve = CurveB;
		return;
	}

	OutPose.TargetMesh = PoseA.TargetMesh;

	const int32 BoneCount = PoseA.LocalTransforms.Num();
	OutPose.LocalTransforms.SetNum(BoneCount, false);

	for (int32 Index = 0; Index < BoneCount; ++Index)
	{
		const FAnimBoneTransform& BoneA =
		    PoseA.LocalTransforms[Index];
		
		const FAnimBoneTransform& BoneB =
		    PoseB.LocalTransforms[Index];

		FAnimBoneTransform& Result =
		    OutPose.LocalTransforms[Index];

		Result.Translation =
		    BoneA.Translation * WA + BoneB.Translation * WB;
		Result.Scale = 
			BoneA.Scale * WA + BoneB.Scale * WB;

		// Slerp의 T는 B쪽으로 이동하는 비율이다.
		Result.Rotation = FMath::SlerpShortestPath(
		    BoneA.Rotation, BoneB.Rotation, WB);
	}

	// Curve는 없는 값을 0으로 보고 가중합한다.
	OutCurve.Reset();

	for (const auto& [Name, ValueA] : CurveA.Values)
	{
		const float* ValueB = CurveB.Values.Find(Name);
		const float Result =
			ValueA * WA +
		    (ValueB ? *ValueB : 0.0f) * WB;

		OutCurve.Values.Add(Name, Result);
	}

	// B에만 있는 항목도 추가한다.
	for (const auto& [Name, ValueB] : CurveB.Values)
	{
		if (!CurveA.Values.Contains(Name))
		{
			OutCurve.Values.Add(Name, ValueB * WB);
		}
	}
}

ETypeAdvanceAnim FAnimationRuntime::AdvanceTime(bool bAllowLooping, float MoveDelta, float& InOutTime, float& OutMovedDelta)
{
	OutMovedDelta = 0.0f;

	if (!std::isfinite(MoveDelta) || MoveDelta == 0.0f)
	{
		return ETypeAdvanceAnim::Default;
	}

	// float끼리 더하면 경계 근처에서 1.0을 넘었는지 판정이 흔들려서 double로 계산한다
	const double OldTime = InOutTime;
	const double NewTime = OldTime + MoveDelta;

	if (bAllowLooping)
	{
		// floor는 음수 쪽으로 내림이라 역방향(-0.1 → 0.9)도 같은 식으로 되감긴다
		double Wrapped = NewTime - std::floor(NewTime);

		// -1e-9 같은 값은 되감으면 0.999999999가 되고, float로 바꾸면 1.0이 돼서 범위를 벗어난다
		if (static_cast<float>(Wrapped) >= 1.0f)
		{
			Wrapped = 0.0;
		}

		InOutTime = static_cast<float>(Wrapped);

		// 루프는 원하던 만큼 다 움직였다. 몇 바퀴 돌았는지 Notify가 알아야 해서 되감기 전 값을 준다
		OutMovedDelta = MoveDelta;

		const bool bCrossed = NewTime >= 1.0 || NewTime < 0.0;
		return bCrossed ? ETypeAdvanceAnim::Looped : ETypeAdvanceAnim::Default;
	}

	const double Clamped = std::clamp(NewTime, 0.0, 1.0);
	InOutTime = static_cast<float>(Clamped);

	// 끝에 멈춘 뒤에는 0이 기록돼야 끝 지점 Notify가 매 프레임 다시 울리지 않는다
	OutMovedDelta = static_cast<float>(Clamped - OldTime);

	// 반대쪽 끝에서 출발하는 경우를 잘못 판정하지 않도록 진행 방향의 끝만 본다
	const bool bHitEnd = MoveDelta > 0.0f ? Clamped >= 1.0 : Clamped <= 0.0;
	return bHitEnd ? ETypeAdvanceAnim::Finished : ETypeAdvanceAnim::Default;
}