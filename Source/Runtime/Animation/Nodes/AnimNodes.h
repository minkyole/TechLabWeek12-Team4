#pragma once

#include "Animation/AnimationRuntime.h"
#include "Animation/AnimationContext.h"
#include "Animation/Nodes/PoseLink.h"
#include "ANimation/BlendSpace.h"

struct FAnimationUpdateContext;
struct FPoseContext;
class UAnimSequence;
class UAnimationAsset;

// 모든 노드의 뿌리. 프레임마다 Update -> Evaluate 순서로 불린다.
struct FAnimNode_Base
{
	virtual ~FAnimNode_Base() = default;

	virtual void OnEnter() {}
	virtual void OnExit() {}

	virtual void Update(const FAnimationUpdateContext& Context) {}
	virtual void Evaluate(FPoseContext& Output) {}

	virtual bool IsFinished() const { return false; }
};

// 두 포즈를 Alpha 로 섞는다.
struct FAnimNode_TwoWayBlend : public FAnimNode_Base
{
	void OnExit() override;
    bool IsFinished() const override;
	FPoseLink A; // 입력 포즈 (다른 노드에 연결)
	FPoseLink B;
	float Alpha = 0.0f; // 0 = A, 1 = B

	void OnEnter() override;
	void Update(const FAnimationUpdateContext& Context) override;
	void Evaluate(FPoseContext& Output) override;
};

// 단일 포즈를 그대로 출력한다. 디버깅용.
struct FAnimNode_Constant : public FAnimNode_Base
{
	FPoseContext ConstantPose;

	void Update(const FAnimationUpdateContext& Context) override;
	void Evaluate(FPoseContext& Output) override;
};

struct FAnimNode_AssetPlayerBase : public FAnimNode_Base
{
	float PlayRate = 1.0f;

	bool bPlaying = true;
	bool bLoop = true;

	// 끄면 루트를 잠그지 않고 이동도 넘기지 않는다. 뷰어가 원본 궤적을 보여 줄 때 쓴다
	bool bUseRootMotion = true;

	// 파생 클래스가 재생하는 에셋을 반환한다.
	virtual const UAnimationAsset* GetAnimationAsset() const = 0;

	// 이번 프레임에 사용할 재생 길이.
	// Sequence: 시퀀스 길이
	// BlendSpace: 현재 샘플 가중치를 반영한 길이
	virtual float GetEffectivePlayLength() const = 0;

	void OnEnter() override;

	void Update(const FAnimationUpdateContext& Context) override;

	// 포즈 생성은 각 재생 노드에서 구현한다.
	void Evaluate(FPoseContext& Output) override = 0;

	bool IsFinished() const override { return !bLoop && (IsReversePlay() ? NormalizedTime <= 0.0f : NormalizedTime >= 1.0f); }
	float GetNormalizedTime() const { return NormalizedTime; }

	void SetNormalizedTime(float InTime);

	// 진행 방향의 시작점으로 돌린다. 역재생이면 1에서 시작해야 바로 끝나지 않는다.
	void ResetPlayback();

	// 방향은 PlayRate 부호 하나로만 정한다. bool을 따로 두면 두 값이 서로 어긋날 수 있다.
	void SetReversePlay(bool bReverse);
	bool IsReversePlay() const { return PlayRate < 0.0f; }

	// Notify·RootMotion이 이번 프레임에 지나간 구간을 읽는다.
	const FDeltaTimeRecord& GetDeltaTimeRecord() const { return DeltaTimeRecord; }
	ETypeAdvanceAnim GetLastAdvanceResult() const { return LastAdvanceResult; }

protected:
	// 시간 진행 전에 필요한 데이터를 갱신한다.
	// BlnedSpace는 여기서 입력에 따른 가중치를 계산한다.
	virtual void UpdateAssetPlayer(
	    const FAnimationUpdateContext& Context) {}

	// 시간을 움직인 직후 불린다. 지나간 구간의 루트 이동을 AnimInstance에 넣는다
	virtual void ExtractRootMotion(
	    const FAnimationUpdateContext& Context, float PrevTime, float MovedDelta) {}

	// 이번 프레임에 지나간 구간의 Notify를 인스턴스 큐에 넣는다. 시간이 실제로 움직인 프레임에만 불린다.
	virtual void GatherNotifies(
	    const FAnimationUpdateContext& Context, float Length, bool bIncludeStart) {}

	// 이번 프레임에 걸쳐 있는 구간 Notify(NotifyState)를 인스턴스에 알린다. 멈춘 프레임에도 매번 불린다.
	virtual void GatherNotifyStates(const FAnimationUpdateContext& Context) {}

private:
	void AdvancePlayback(const FAnimationUpdateContext& Context);

	float NormalizedTime = 0.0f;
	FDeltaTimeRecord DeltaTimeRecord;
	ETypeAdvanceAnim LastAdvanceResult = ETypeAdvanceAnim::Default;
	bool bIncludeStartInNextAdvance = true;
};

// 클립 하나를 재생한다.
struct FAnimNode_SequencePlayer : public FAnimNode_AssetPlayerBase
{
	UAnimSequence* Sequence = nullptr;

	const UAnimationAsset* GetAnimationAsset() const override;
	float GetEffectivePlayLength() const override;

	void Evaluate(FPoseContext& Output) override;

	bool IsFinished() const override;

protected:
	void ExtractRootMotion(
	    const FAnimationUpdateContext& Context, float PrevTime, float MovedDelta) override;
	void GatherNotifies(
	    const FAnimationUpdateContext& Context, float Length, bool bIncludeStart) override;
	void GatherNotifyStates(const FAnimationUpdateContext& Context) override;
};

struct FAnimNode_BlendSpacePlayer : public FAnimNode_AssetPlayerBase
{
	UBlendSpace* BlendSpace = nullptr;
	float X = 0.0f;

	const UAnimationAsset* GetAnimationAsset() const override;
	float GetEffectivePlayLength() const override;

	void Evaluate(FPoseContext& Output) override;

protected:
	void UpdateAssetPlayer(
	    const FAnimationUpdateContext& Context) override;

	void ExtractRootMotion(
	    const FAnimationUpdateContext& Context, float PrevTime, float MovedDelta) override;
	void GatherNotifies(
	    const FAnimationUpdateContext& Context, float Length, bool bIncludeStart) override;
	void GatherNotifyStates(const FAnimationUpdateContext& Context) override;

private:
	// NotifyTriggerMode에 따라 Notify를 검출할 샘플만 고른다.
	void GetNotifySamples(TArray<FBlendSampleWeight>& OutSamples) const;

	TArray<FBlendSampleWeight> SampleWeights;

	float AppliedX = 0.0f;
	bool bHasAppliedInput = false;
};