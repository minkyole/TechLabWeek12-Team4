#include "EnginePCH.h"
#include "AnimNodes.h"

#include "Animation/AnimationContext.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimInstance.h"

namespace
{
	// None 모드는 원본 궤적을 보여 주는 게 목적이라 Lock도 같이 푼다
	bool ShouldLockRoot(bool bUseRootMotion, const UAnimInstance* Instance)
	{
		return bUseRootMotion &&
		       (!Instance || Instance->GetEffectiveRootMotionMode() != ERootMotionMode::NoRootMotionExtraction);
	}

	bool IsNameNotifyState(const FAnimNotifyEvent& Notify)
	{
		return Notify.Notify == nullptr && Notify.Duration > 0.0f;
	}

	// 구간 [StateStart, StateStart + Duration]이 이번 프레임에 재생 헤드가 지난 범위와 겹치는지.
	// 정방향은 (Start, End], 역방향은 [End, Start)로 보고, 멈춘 프레임은 현재 시간이 구간 안인지 본다.
	bool IsNotifyStateActive(double StateStart, double Duration, double Start, double End, double Length, bool bLoop, bool bReverse)
	{
		const double StateEnd = StateStart + Duration;
		if (End == Start)
		{
			// 멈춘 위치가 구간 경계면 진행 방향 쪽에서 이미 빠져나온 것으로 본다.
			return bReverse ? (Start > StateStart && Start <= StateEnd) : (Start >= StateStart && Start < StateEnd);
		}

		const bool bForward = End > Start;
		const double Low = bForward ? Start : End;
		const double High = bForward ? End : Start;

		if (!bLoop || Length <= 0.0)
		{
			return bForward ? (StateStart <= High && StateEnd > Low) : (StateStart < High && StateEnd >= Low);
		}

		double KMin = 0.0;
		double KMax = 0.0;
		if (bForward)
		{
			KMin = std::floor((Low - StateEnd) / Length) + 1.0;
			KMax = std::floor((High - StateStart) / Length);
		}
		else
		{
			KMin = std::ceil((Low - StateEnd) / Length);
			KMax = std::ceil((High - StateStart) / Length) - 1.0;
		}
		return KMax >= KMin;
	}

	// 구간 안에 들어오는 Time + k * Length (k는 정수)의 개수. 루프면 바퀴 수만큼 센다.
	int32 CountNotifyCrossings(double Time, double Start, double End, double Length, bool bLoop, bool bIncludeStart)
	{
		const bool bForward = End > Start;
		const double Low = bForward ? Start : End;
		const double High = bForward ? End : Start;
		const bool bLowInclusive = !bForward || bIncludeStart;
		const bool bHighInclusive = bForward || bIncludeStart;

		if (!bLoop || Length <= 0.0)
		{
			const bool bAboveLow = bLowInclusive ? Time >= Low : Time > Low;
			const bool bBelowHigh = bHighInclusive ? Time <= High : Time < High;
			return (bAboveLow && bBelowHigh) ? 1 : 0;
		}

		const double KLow = (Low - Time) / Length;
		const double KHigh = (High - Time) / Length;
		const double First = bLowInclusive ? std::ceil(KLow) : std::floor(KLow) + 1.0;
		const double Last = bHighInclusive ? std::floor(KHigh) : std::ceil(KHigh) - 1.0;
		return Last >= First ? static_cast<int32>(Last - First + 1.0) : 0;
	}
}

void FAnimNode_TwoWayBlend::OnEnter()
{
	A.OnEnter();
	B.OnEnter();
}

void FAnimNode_TwoWayBlend::Update(const FAnimationUpdateContext& Context)
{
	// 각 입력이 자기 재생 시간을 흘린다. 루트 이동도 포즈와 같은 비율로 나눠 넘긴다
	Alpha = std::clamp(Alpha, 0.0f, 1.0f);
	if (Alpha < 1.0f) A.Update(Context.FractionalWeight(1.0f - Alpha));
	if (Alpha > 0.0f) B.Update(Context.FractionalWeight(Alpha));
}

void FAnimNode_TwoWayBlend::Evaluate(FPoseContext& Output)
{
	if (Alpha <= 0.0f) { A.Evaluate(Output); return; }
    if (Alpha >= 1.0f) { B.Evaluate(Output); return; }
	FPoseContext PoseA(Output);
	FPoseContext PoseB(Output);
	A.Evaluate(PoseA);
	B.Evaluate(PoseB);

	// 본마다 위치 / 스케일은 Lerp, 회전은 Quaternion Slerp
	FAnimationRuntime::BlendTwoPosesTogether(
	    PoseA.Pose, PoseB.Pose, PoseA.Curve, PoseB.Curve,
	    1.0f - Alpha, // 첫 번째 포즈(A) 의 가중치
	    Output.Pose, Output.Curve);
}

void FAnimNode_Constant::Update(const FAnimationUpdateContext& Context)
{
	// 아무것도 하지 않는다. ConstantPose는 외부에서 설정한다.
}

void FAnimNode_Constant::Evaluate(FPoseContext& Output)
{
	// Pose와 Curve를 그대로 복사한다.
	Output.Pose = ConstantPose.Pose;
	Output.Curve = ConstantPose.Curve;
}

void FAnimNode_AssetPlayerBase::SetNormalizedTime(float InTime)
{
	if (!std::isfinite(InTime))
	{
		return;
	}
	NormalizedTime = std::clamp(InTime, 0.0f, 1.0f);

	// 스크럽·점프는 재생이 아니라 순간이동이라 지나간 구간이 없다
	DeltaTimeRecord.Set(NormalizedTime, 0.0f);
	LastAdvanceResult = ETypeAdvanceAnim::Default;
	bIncludeStartInNextAdvance = true;
}

void FAnimNode_AssetPlayerBase::ResetPlayback()
{
	// 루프 역재생도 1에서 시작해야 첫 프레임에 Looped가 떠서 끝 지점 Notify가 울리지 않는다
	SetNormalizedTime(IsReversePlay() ? 1.0f : 0.0f);
}

void FAnimNode_AssetPlayerBase::SetReversePlay(bool bReverse)
{
	// 속도 크기는 유지하고 부호만 바꿔서, 뷰어의 배속 설정과 충돌하지 않게 한다
	const float Speed = std::abs(PlayRate);
	PlayRate = bReverse ? -Speed : Speed;
}

void FAnimNode_AssetPlayerBase::OnEnter()
{
	// 재생 시작 시점에 호출된다. 재생 시간을 초기화한다.
	ResetPlayback();
}

void FAnimNode_AssetPlayerBase::Update(
    const FAnimationUpdateContext& Context)
{
	// 정지 중에도 입력값 변경은 포즈에 반영되어야 한다.
	// 따라서 bPlyaing 검사보다 먼저 호출한다.
	UpdateAssetPlayer(Context);

	AdvancePlayback(Context);

	// 멈춘 프레임에도 걸쳐 있는 구간을 알려야 일시정지 중에 End가 나가지 않는다.
	GatherNotifyStates(Context);
}

void FAnimNode_AssetPlayerBase::AdvancePlayback(
    const FAnimationUpdateContext& Context)
{
	// 멈춘 프레임도 "안 움직임"으로 덮어써야 지난 프레임 구간으로 Notify가 또 울리지 않는다
	DeltaTimeRecord.Set(NormalizedTime, 0.0f);
	LastAdvanceResult = ETypeAdvanceAnim::Default;

	if (!bPlaying || GetAnimationAsset() == nullptr)
	{
		return;
	}

	const float Length = GetEffectivePlayLength();

	if (!std::isfinite(Length) || Length <= 0.0f)
	{
		return;
	}

	if (!std::isfinite(Context.DeltaSeconds) ||
	    Context.DeltaSeconds < 0.0f ||
	    !std::isfinite(PlayRate) ||
	    PlayRate == 0.0f)
	{
		return;
	}

	// 초 단위를 길이로 나눠 정규화한다. BlendSpace는 이 길이가 가중치에 따라 매 프레임 바뀐다
	const float MoveDelta = static_cast<float>(
	    static_cast<double>(Context.DeltaSeconds) * PlayRate / Length);

	float MovedDelta = 0.0f;
	LastAdvanceResult = FAnimationRuntime::AdvanceTime(
	    bLoop, MoveDelta, NormalizedTime, MovedDelta);

	// Previous는 위에서 움직이기 전 값으로 이미 넣어 두었다
	DeltaTimeRecord.Set(DeltaTimeRecord.Previous, MovedDelta);

	if (!DeltaTimeRecord.IsMoved())
	{
		return;
	}

	GatherNotifies(Context, Length, bIncludeStartInNextAdvance);
	bIncludeStartInNextAdvance = false;

	// Notify와 같은 구간 기록을 써야 이벤트와 이동이 같은 시간 범위를 본다
	if (bUseRootMotion && Context.AnimInstance)
	{
		ExtractRootMotion(Context, DeltaTimeRecord.Previous, DeltaTimeRecord.Delta);
	}
}

void FAnimNode_SequencePlayer::Evaluate(FPoseContext& Output)
{
	if (!Sequence)
	{
		return;
	}
	Sequence->GetAnimationPose(GetNormalizedTime() * Sequence->GetPlayLength(), Output.Pose, ShouldLockRoot(bUseRootMotion, Output.AnimInstance));
}

bool FAnimNode_SequencePlayer::IsFinished() const
{
	if (!Sequence)
	{
		return true;
	}
	if (bLoop)
	{
		return false;
	}

	// 일시정지나 SetPosition으로 끝에 놓인 경우도 맞게 판정하려고 결과값 대신 시간을 본다
	return IsReversePlay() ? GetNormalizedTime() <= 0.0f : GetNormalizedTime() >= 1.0f;
}

void FAnimNode_SequencePlayer::ExtractRootMotion(
    const FAnimationUpdateContext& Context, float PrevTime, float MovedDelta)
{
	const USkeletalMesh* Mesh = Context.AnimInstance->GetSkeletalMesh();
	if (!Sequence || !Mesh)
	{
		return;
	}

	const FRootMotionParams Delta = Sequence->ExtractRootMotion(*Mesh, PrevTime, MovedDelta, bLoop);
	Context.AnimInstance->AccumulateRootMotion(Delta, Context.BlendWeight);
}

void FAnimNode_SequencePlayer::GatherNotifies(
    const FAnimationUpdateContext& Context, float Length, bool bIncludeStart)
{
	if (!Sequence || !Context.AnimInstance || Sequence->Notifies.IsEmpty())
	{
		return;
	}

	const FDeltaTimeRecord& Record = GetDeltaTimeRecord();
	const double Start = static_cast<double>(Record.Previous) * Length;
	const double End = Start + static_cast<double>(Record.Delta) * Length;

	for (const FAnimNotifyEvent& Notify : Sequence->Notifies)
	{
		if (IsNameNotifyState(Notify))
		{
			continue;
		}

		const int32 Count = CountNotifyCrossings(Notify.TriggerTime, Start, End, Length, bLoop, bIncludeStart);
		for (int32 i = 0; i < Count; ++i)
		{
			Context.AnimInstance->QueueAnimNotify(Notify, Sequence);
		}
	}
}

void FAnimNode_SequencePlayer::GatherNotifyStates(const FAnimationUpdateContext& Context)
{
	if (!Sequence || !Context.AnimInstance || Sequence->Notifies.IsEmpty())
	{
		return;
	}

	const float Length = GetEffectivePlayLength();
	if (!std::isfinite(Length) || Length <= 0.0f)
	{
		return;
	}

	const FDeltaTimeRecord& Record = GetDeltaTimeRecord();
	const double Start = static_cast<double>(Record.Previous) * Length;
	const double End = Start + static_cast<double>(Record.Delta) * Length;

	for (const FAnimNotifyEvent& Notify : Sequence->Notifies)
	{
		if (IsNameNotifyState(Notify) && IsNotifyStateActive(Notify.TriggerTime, Notify.Duration, Start, End, Length, bLoop, IsReversePlay()))
		{
			Context.AnimInstance->QueueAnimNotifyState(Notify, Sequence);
		}
	}
}

const UAnimationAsset* FAnimNode_SequencePlayer::GetAnimationAsset() const
{
	return Sequence;
}

float FAnimNode_SequencePlayer::GetEffectivePlayLength() const
{
	if (Sequence == nullptr)
		return 0.0f;
	return Sequence->GetPlayLength();
}

const UAnimationAsset* FAnimNode_BlendSpacePlayer::GetAnimationAsset() const
{
	return BlendSpace;
}

void FAnimNode_BlendSpacePlayer::GetNotifySamples(TArray<FBlendSampleWeight>& OutSamples) const
{
	OutSamples.Reset();
	if (!BlendSpace || BlendSpace->NotifyTriggerMode == ENotifyTriggerMode::None)
	{
		return;
	}

	for (const FBlendSampleWeight& Entry : SampleWeights)
	{
		if (!std::isfinite(Entry.Weight) || Entry.Weight <= 0.0f)
		{
			continue;
		}

		if (BlendSpace->NotifyTriggerMode == ENotifyTriggerMode::AllAnimations || OutSamples.IsEmpty())
		{
			OutSamples.Add(Entry);
		}
		else if (Entry.Weight > OutSamples[0].Weight)
		{
			OutSamples[0] = Entry;
		}
	}
}

void FAnimNode_BlendSpacePlayer::GatherNotifies(
    const FAnimationUpdateContext& Context, float Length, bool bIncludeStart)
{
	if (!Context.AnimInstance)
	{
		return;
	}

	TArray<FBlendSampleWeight> NotifySamples;
	GetNotifySamples(NotifySamples);

	const FDeltaTimeRecord& Record = GetDeltaTimeRecord();
	for (const FBlendSampleWeight& Entry : NotifySamples)
	{
		const FBlendSample* Sample = BlendSpace->GetSample(Entry.SampleIndex);
		UAnimSequence* SampleSequence = Sample ? Sample->Animation : nullptr;
		if (!SampleSequence || SampleSequence->Notifies.IsEmpty())
		{
			continue;
		}

		// 샘플들은 정규화 진행률을 공유하므로 같은 구간을 각 샘플 길이로 바꿔 검사한다.
		const double SampleLength = SampleSequence->GetPlayLength();
		const double Start = static_cast<double>(Record.Previous) * SampleLength;
		const double End = Start + static_cast<double>(Record.Delta) * SampleLength;

		for (const FAnimNotifyEvent& Notify : SampleSequence->Notifies)
		{
			if (IsNameNotifyState(Notify))
			{
				continue;
			}

			const int32 Count = CountNotifyCrossings(Notify.TriggerTime, Start, End, SampleLength, bLoop, bIncludeStart);
			for (int32 i = 0; i < Count; ++i)
			{
				Context.AnimInstance->QueueAnimNotify(Notify, SampleSequence, Entry.Weight);
			}
		}
	}
}

void FAnimNode_BlendSpacePlayer::GatherNotifyStates(const FAnimationUpdateContext& Context)
{
	if (!Context.AnimInstance)
	{
		return;
	}

	TArray<FBlendSampleWeight> NotifySamples;
	GetNotifySamples(NotifySamples);

	const FDeltaTimeRecord& Record = GetDeltaTimeRecord();
	for (const FBlendSampleWeight& Entry : NotifySamples)
	{
		const FBlendSample* Sample = BlendSpace->GetSample(Entry.SampleIndex);
		UAnimSequence* SampleSequence = Sample ? Sample->Animation : nullptr;
		if (!SampleSequence || SampleSequence->Notifies.IsEmpty())
		{
			continue;
		}

		const double SampleLength = SampleSequence->GetPlayLength();
		if (!std::isfinite(SampleLength) || SampleLength <= 0.0)
		{
			continue;
		}
		const double Start = static_cast<double>(Record.Previous) * SampleLength;
		const double End = Start + static_cast<double>(Record.Delta) * SampleLength;

		for (const FAnimNotifyEvent& Notify : SampleSequence->Notifies)
		{
			if (IsNameNotifyState(Notify) &&
				IsNotifyStateActive(Notify.TriggerTime, Notify.Duration, Start, End, SampleLength, bLoop, IsReversePlay()))
			{
				Context.AnimInstance->QueueAnimNotifyState(Notify, SampleSequence, Entry.Weight);
			}
		}
	}
}

void FAnimNode_BlendSpacePlayer::UpdateAssetPlayer(
    const FAnimationUpdateContext& Context)
{
	SampleWeights.Reset();

	if (BlendSpace == nullptr)
	{
		return;
	}

	if (!Context.bFreezeBlendSpaceInput || !bHasAppliedInput)
	{
		AppliedX = X;
		bHasAppliedInput = true;
	}

	if (!BlendSpace->GetSampleWeights(
	        FVector(AppliedX, 0.0f, 0.0f),
	        SampleWeights))
	{
		SampleWeights.Reset();
	}
}

void FAnimNode_BlendSpacePlayer::ExtractRootMotion(
    const FAnimationUpdateContext& Context, float PrevTime, float MovedDelta)
{
	const USkeletalMesh* Mesh = Context.AnimInstance->GetSkeletalMesh();
	if (!BlendSpace || !Mesh)
	{
		return;
	}

	// Evaluate와 같은 비율로 섞어야 포즈와 이동이 어긋나지 않으니 합이 1이 되게 맞춘다
	float WeightSum = 0.0f;
	for (const FBlendSampleWeight& Entry : SampleWeights)
	{
		WeightSum += Entry.Weight > 0.0f ? Entry.Weight : 0.0f;
	}
	if (WeightSum <= 0.0f)
	{
		return;
	}

	for (const FBlendSampleWeight& Entry : SampleWeights)
	{
		const FBlendSample* Sample = BlendSpace->GetSample(Entry.SampleIndex);
		if (!Sample || !Sample->Animation || Entry.Weight <= 0.0f)
		{
			continue;
		}

		// 샘플들은 같은 정규화 시간을 공유하니 구간도 그대로 쓴다
		const FRootMotionParams Delta = Sample->Animation->ExtractRootMotion(*Mesh, PrevTime, MovedDelta, bLoop);
		Context.AnimInstance->AccumulateRootMotion(Delta, Context.BlendWeight * (Entry.Weight / WeightSum));
	}
}

float FAnimNode_BlendSpacePlayer::GetEffectivePlayLength() const
{
	if (BlendSpace == nullptr)
		return 0.0f;

	return BlendSpace->GetEffectivePlayLength(SampleWeights);
}

void FAnimNode_BlendSpacePlayer::Evaluate(FPoseContext& Output)
{
	// 정상적인 AnimInstance 평가 경로에서는 대상 메시가 설정되어 있다.
	if (!Output.Pose.TargetMesh)
	{
		assert(false);
		return;
	}

	// 샘플 평가에 실패하면 기준 포즈와 빈 커브를 출력한다.
	Output.ResetToRefPose();

	if (!BlendSpace || SampleWeights.IsEmpty())
	{
		return;
	}

	// 현재 구현은 최대 두 샘플을 섞는 1D 전용이다.
	if (BlendSpace->GetDimension() != 1 || SampleWeights.Num() > 2)
	{
		assert(false);
		return;
	}

	const float NormalizedTime = GetNormalizedTime();

	// 선택된 샘플을 같은 정규화 진행률에서 평가한다.
	const auto EvaluateSample =
	    [this, NormalizedTime](
	        const FBlendSampleWeight& Entry,
	        FPoseContext& SamplePose) -> bool
	{
		if (!std::isfinite(Entry.Weight) || Entry.Weight <= 0.0f)
		{
			return false;
		}

		const FBlendSample* Sample =
		    BlendSpace->GetSample(Entry.SampleIndex);

		if (!Sample || !Sample->Animation)
		{
			return false;
		}

		UAnimSequence* Sequence = Sample->Animation;

		// GetPlayLength()가 DataModel을 직접 참조하므로 먼저 검사.
		if (!Sequence->GetDataModel())
		{
			return false;
		}

		const float Length = Sequence->GetPlayLength();

		if (!std::isfinite(Length) || Length <= 0.0f)
		{
			return false;
		}

		const float TimeSeconds = NormalizedTime * Length;

		// SamplePose는 Output과 같은 대상 메시를 가진다.
		// GetAnimationPose 내부에서 본 이름 매핑과 보간을 수행한다.
		Sequence->GetAnimationPose(TimeSeconds, SamplePose.Pose, ShouldLockRoot(bUseRootMotion, SamplePose.AnimInstance));

		// 현재 시퀀스 API는 본 포즈만 평가한다.
		SamplePose.Curve.Reset();

		return true;
	};

	// FPoseContext 복사 생성자는 같은 평가 환경과 대상 메시를 가진 기준 포즈 버퍼를 만든다.
	FPoseContext PoseA(Output);

	if (!EvaluateSample(SampleWeights[0], PoseA))
	{
		return;
	}

	// 샘플 좌표와 일치하거나 입력이 양 끝에 있으면 하나의 샘플만 사용한다.
	if (SampleWeights.Num() == 1)
	{
		Output.Pose = PoseA.Pose;
		Output.Curve = PoseA.Curve;
		return;
	}

	FPoseContext PoseB(Output);

	if (!EvaluateSample(SampleWeights[1], PoseB))
	{
		return;
	}

	// 두 가중치는 앞에서 유효한 양수인지 검사한다.
	const double WeightA = SampleWeights[0].Weight;
	const double WeightB = SampleWeights[1].Weight;

	const float NormalizedWeightA =
	    static_cast<float>(WeightA / (WeightA + WeightB));

	// 이 함수는 Alpha(B의 비율)가 아닌 A의 가중치를 받는다.
	FAnimationRuntime::BlendTwoPosesTogether(
	    PoseA.Pose, PoseB.Pose, PoseA.Curve, PoseB.Curve,
	    NormalizedWeightA, Output.Pose, Output.Curve);
}

void FAnimNode_TwoWayBlend::OnExit()
{
    if (A.LinkedNode) A.LinkedNode->OnExit();
    if (B.LinkedNode) B.LinkedNode->OnExit();
}
bool FAnimNode_TwoWayBlend::IsFinished() const
{
    return (Alpha >= 1.0f || (A.LinkedNode && A.LinkedNode->IsFinished())) &&
           (Alpha <= 0.0f || (B.LinkedNode && B.LinkedNode->IsFinished()));
}
