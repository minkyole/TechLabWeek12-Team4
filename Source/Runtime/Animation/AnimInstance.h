#pragma once

#include "UObject/Object.h"
#include "UObject/Class.h"
#include "Animation/Nodes/AnimNodes.h"
#include "Animation/AnimNotifyEvent.h"
#include "Animation/AnimParameters.h"

struct FAnimNode_Base;
class USkeletalMeshComponent;
class UAnimSequence;
struct FPoseContext;

class UAnimInstance : public UObject
{
	DECLARE_CLASS(UAnimInstance, UObject)
public:
    virtual FAnimParameterSet& GetParameters() { return Parameters; }
    virtual const FAnimParameterSet& GetParameters() const { return Parameters; }
    virtual bool SetBool(const FString& Name, bool Value);
    virtual bool SetFloat(const FString& Name, float Value);
    virtual bool SetTrigger(const FString& Name);
    virtual bool ResetTrigger(const FString& Name);
    virtual bool TryGetBool(const FString& Name, bool& Value) const;
    virtual bool TryGetFloat(const FString& Name, float& Value) const;
    virtual bool TryGetTrigger(const FString& Name, bool& Value) const;
	void InitializeAnimation(USkeletalMeshComponent* InComponent);
	void UnInitializeAnimation();

	virtual void UpdateAnimation(float DeltaSeconds);
	void EvaluateAnimation(FPoseContext& Output);

	USkeletalMeshComponent* GetSkeletalMeshComponent() const;
	USkeletalMesh* GetSkeletalMesh() const;

	bool IsInitialized() const;

	// 재생 노드가 루트 로컬 이동량을 비율과 함께 넣는다
	void AccumulateRootMotion(const FRootMotionParams& Delta, float Weight);

	// 꺼내면서 비워서, 이동 쪽이 몇 번 불러도 한 프레임 이동은 한 번만 적용된다
	FRootMotionParams ConsumeRootMotion();

	void SetRootMotionMode(ERootMotionMode InMode) { RootMotionMode = InMode; }
	ERootMotionMode GetRootMotionMode() const { return RootMotionMode; }

	// 콘솔 강제값이 있으면 그걸 쓴다. 노드는 항상 이 값을 본다
	ERootMotionMode GetEffectiveRootMotionMode() const;

	// rootmotion [none|ignore|on|default] : 모든 인스턴스에 강제값을 건다. default는 해제
	static bool ExecRootMotionCommand(const FString& CommandLine, FString& OutMessage);

	void QueueAnimNotify(const FAnimNotifyEvent& Event, UAnimSequenceBase* Sequence, float Weight = 1.0f);
	void QueueAnimNotifyState(const FAnimNotifyEvent& Event, UAnimSequenceBase* Sequence, float Weight = 1.0f);
	void TriggerAnimNotifies();
	void EndAllNotifyStates();
	const TArray<FQueuedAnimNotify>& GetQueuedNotifies() const { return NotifyQueue; }

protected:
	virtual void NativeInitializeAnimation() {};               // 그래프를 만든다 (한 번)
	virtual void NativeUnInitializeAnimation() {};             // 그래프를 정리한다 (한 번)
	virtual void NativeUpdateAnimation(float DeltaSeconds) {}; // 변수를 갱신한다 (매 프레임)


	// 파생 클래스가 소유한 노드를 참조한다. 여기서 delete하지 않는다.
	FAnimNode_Base* RootNode = nullptr; // 그래프의 시작 노드 (StateMachine 등)

private:
	// 비소유 참조. 컴포넌트가 인스턴스의 수명을 관리한다.
	FAnimParameterSet Parameters;
	USkeletalMeshComponent* OwningComponent = nullptr;
	bool bIsInitialized = false;

	// 그래프 안의 모든 재생 노드가 모으는 곳. 노드는 여럿이라 노드에 두면 합칠 곳이 없다
	FRootMotionParams ExtractedRootMotion;

	ERootMotionMode RootMotionMode = ERootMotionMode::RootMotionFromEverything;

	// 디버그 비교용이라 인스턴스마다 찾아다니지 않고 전역 하나로 덮어쓴다
	inline static bool bHasRootMotionOverride = false;
	inline static ERootMotionMode RootMotionOverride = ERootMotionMode::RootMotionFromEverything;

	TArray<FQueuedAnimNotify> NotifyQueue;
	TArray<FQueuedAnimNotify> NotifyStateQueue;
	TArray<FQueuedAnimNotify> ActiveNotifyStates;
};

class UAnimSingleNodeInstance : public UAnimInstance
{
	DECLARE_CLASS(UAnimSingleNodeInstance, UAnimInstance)
public:
	void SetAnimationAsset(UAnimSequence* InSequence);
	UAnimSequence* GetAnimationAsset() const;
	void SetBlendSpaceAsset(UBlendSpace1D* InBlendSpace);
	UBlendSpace1D* GetBlendSpaceAsset() const;
	void SetBlendSpaceInput(float X);

	void SetPlaying(bool bInPlaying);
	bool IsPlaying() const;
	void SetLooping(bool bInLooping);
	bool IsLooping() const;

	void SetPlayRate(float InPlayRate);
	float GetPlayRate() const;

	void SetReversePlay(bool bReverse);
	bool IsReversePlay() const;

	void SetPosition(float InPosition);
	float GetPosition() const;

	float GetLength() const;

	// 뷰어는 끄고 원본 궤적을 보여 주고, 게임에서는 켜서 Actor를 움직인다
	void SetUseRootMotion(bool bUse);
	bool IsUsingRootMotion() const;

	// 루프가 아니고 진행 방향의 끝에 닿았는지. 뷰어가 자동 정지할 때 쓴다.
	bool IsFinished() const;

	// 이번 프레임에 지나간 구간. 정규화 시간 단위라 초가 필요하면 GetLength()를 곱한다.
	const FDeltaTimeRecord& GetDeltaTimeRecord() const;

protected:
	void NativeInitializeAnimation() override;

private:
	FAnimNode_AssetPlayerBase& ActivePlayer();
	const FAnimNode_AssetPlayerBase& ActivePlayer() const;
	FAnimNode_SequencePlayer SequencePlayer;
	FAnimNode_BlendSpacePlayer BlendSpacePlayer;
};