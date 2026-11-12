#pragma once

#include "Animation/AnimInstance.h"
#include "Animation/Nodes/AnimNode_StateMachine.h"

#include <functional>
#include <memory>
#include <vector>

using FAnimNodeId = int32;

inline constexpr FAnimNodeId InvalidAnimNodeId = -1;

struct FAnimPlayerOptions
{
	bool bLoop = true;
	bool bPlaying = true;
	float PlayRate = 1.0f;
};

struct FAnimTransitionOptions
{
	FAnimConditionId Condition = InvalidAnimConditionId;

	int32 Priority = 0;

	FAnimBlendSettings Blend;

	bool bRestartIfSameState = false;

	EAnimTransitionInterruptPolicy InterruptPolicy = EAnimTransitionInterruptPolicy::Always;

	TArray<FName> ConsumeTriggers;
};

class UStateMachineAnimInstance : public UAnimInstance
{
	DECLARE_CLASS(UStateMachineAnimInstance, UAnimInstance)

  public:
    FAnimParameterSet& GetParameters() override { return StateMachine.Parameters; }
    const FAnimParameterSet& GetParameters() const override { return StateMachine.Parameters; }
    FAnimNodeId RegisterPoseRoot(TUniquePtr<FAnimNode_Base> Node);
	~UStateMachineAnimInstance() override = default;

	// 호스트 측 구성
	bool BeginBuild();
	bool RegisterAssetAlias(const FString &Name, const FString &AssetKey);

	// 노드 구성
	FAnimNodeId CreateSequencePlayer(const FString &ClipName, const FAnimPlayerOptions &Options);

	FAnimNodeId CreateBlendSpacePlayer(const FString &BlendSpaceName, const FAnimPlayerOptions &Options);

    FAnimNodeId CreateTwoWayBlend(FAnimNodeId A, FAnimNodeId B, float Alpha = 0.0f);
    bool SetBlendAlpha(FAnimNodeId NodeId, float Alpha);

	// 상태 머신 구성
	FAnimStateId AddState(const FString &Name, FAnimNodeId NodeId);

	bool SetEntryState(FAnimStateId StateId);

	FAnimConditionId AddCondition(const FAnimConditionDefinition &Condition);

	FAnimConditionId AddCustomCondition(FAnimConditionCallback Callback);

	FAnimTransitionId AddTransition(FAnimStateId From, FAnimStateId To, const FAnimTransitionOptions &Options);

	FAnimTransitionId AddAnyStateTransition(FAnimStateId To, const FAnimTransitionOptions &Options);

	bool Commit();

	// 파라미터
	bool SetBool(const FString &Name, bool Value);
	bool SetFloat(const FString &Name, float Value);

	bool DefineTrigger(const FString &Name);
	bool SetTrigger(const FString &Name);
	bool ResetTrigger(const FString &Name);

	bool TryGetBool(const FString &Name, bool &OutValue) const;
	bool TryGetFloat(const FString &Name, float &OutValue) const;
	bool TryGetTrigger(const FString &Name, bool &OutValue) const;

	// 재생 제어
	bool SetBlendSpaceInput(FAnimNodeId NodeId, float Value);
	bool SetPlayRate(FAnimNodeId NodeId, float Value);
	bool SetPlaying(FAnimNodeId NodeId, bool bPlaying);

	// 조회
	bool IsGraphReady() const
	{
		return bGraphReady;
	}
	FAnimStateId GetCurrentState() const;
	bool IsSourceFinished() const;
	bool IsTransitioning() const
	{
		return StateMachine.IsTransitioning();
	}
	int GetActiveTransitionIndex() const
	{
		return StateMachine.GetActiveTransitionIndex();
	}
	float GetTransitionProgress() const
	{
		return StateMachine.GetTransitionProgress();
	}
	const FString &GetLastError() const
	{
		return LastError;
	}

  protected:
	void NativeInitializeAnimation() override
	{
		ResetGraph();
	}
	void NativeUnInitializeAnimation() override
	{
		ResetGraph();
	}

  protected:
	enum class EPlayerKind : uint8
	{
		Graph,
		Sequence,
		BlendSpace,
	};

	struct FPlayerEntry
	{
		EPlayerKind Kind = EPlayerKind::Sequence;
		TUniquePtr<FAnimNode_Base> Node;
	};

	struct FAssetAlias
	{
		FString Name;
		FString AssetKey;
	};

	void ResetGraph();

	bool Fail(const FString &Message);
	bool RequireBuilding();

	FString ResolveAssetKey(const FString &Name) const;

	FPlayerEntry *FindPlayer(FAnimNodeId NodeId);

	bool ValidateAsset(const UAnimationAsset *Asset);
	bool ValidateParameters();

	FAnimTransitionId AddTransitionInternal(FAnimStateId From, FAnimStateId To, bool bAnyState,
											const FAnimTransitionOptions &Options);

  protected:
	// 에셋 키만 보관한다. 실제 에셋 수명은 외부에서 보장한다.
	TArray<FAssetAlias> AssetAliases;

	// 아래 두 객체보다 StateMachine을 먼저 해제해야 한다.
	FAnimStateMachineDefinition Definition;
	TArray<FPlayerEntry> Players;
    TArray<FAnimNodeId> LinkedPoseNodes;

	FAnimNode_StateMachine StateMachine;

	bool bBuilding = false;
	bool bGraphReady = false;

	FString LastError;
};

using FLuaAnimTransitionOptions = FAnimTransitionOptions;
