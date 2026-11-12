#include "EnginePCH.h"
#include "StateMachineAnimInstance.h"
#include "Animation/AnimSequence.h"
#include "Animation/BlendSpace.h"
#include "Asset/AssetManager.h"
#include <cmath>
bool UStateMachineAnimInstance::Fail(const FString &Message)
{
	LastError = Message;
	return false;
}

bool UStateMachineAnimInstance::RequireBuilding()
{
	if (!bBuilding)
		return Fail("Graph is not build mode");

	return true;
}

void UStateMachineAnimInstance::ResetGraph()
{
    EndAllNotifyStates();
    ConsumeRootMotion();
	RootNode = nullptr;

	bGraphReady = false;
	bBuilding = false;

	// 상태 머신이 가진 정의/노드 참조를 먼저 제거한다.
	StateMachine = FAnimNode_StateMachine{};

	// 그 다음 실제 노드와 정의를 제거한다.
	Players.Reset();
    LinkedPoseNodes.Reset();
	Definition = FAnimStateMachineDefinition{};
}

bool UStateMachineAnimInstance::BeginBuild()
{
	if (!IsInitialized() || !GetSkeletalMesh())
		return Fail("AnimInstance must be initialized before building.");

	ResetGraph();

	LastError.clear();
	bBuilding = true;
	return true;
}

bool UStateMachineAnimInstance::RegisterAssetAlias(const FString &Name, const FString &AssetKey)
{
	if (bGraphReady)
		return Fail("Cannot change asset aliases while graph is running.");

	if (Name.empty() || AssetKey.empty())
		return Fail("Asset alias and asset key must not be empty.");

	for (const FAssetAlias &Alias : AssetAliases)
	{
		if (Alias.Name == Name)
			return Fail("Duplicate asset alias.");
	}

	FAssetAlias Alias;
	Alias.Name = Name;
	Alias.AssetKey = AssetKey;

	AssetAliases.Add(Alias);
	return true;
}

FString UStateMachineAnimInstance::ResolveAssetKey(const FString &Name) const
{
	for (const FAssetAlias &Alias : AssetAliases)
	{
		if (Alias.Name == Name)
			return Alias.AssetKey;
	}

	return Name;
}

bool UStateMachineAnimInstance::ValidateAsset(const UAnimationAsset *Asset)
{
	if (!Asset)
		return Fail("Animation asset was not found or has the wrong type.");

	// 첫 구현은 동일한 메시를 사용하는 에셋으로 제한한다.
	if (Asset->GetSkeletalMesh() != GetSkeletalMesh())
		return Fail("Animation asset uses a different skeletal mesh.");

	const float Length = Asset->GetPlayLength();

	if (!std::isfinite(Length) || Length <= 0.0f)
		return Fail("Animation asest has an invalid play length.");

	return true;
}

FAnimNodeId UStateMachineAnimInstance::CreateSequencePlayer(const FString &ClipName, const FAnimPlayerOptions &Options)
{
	if (!RequireBuilding())
		return InvalidAnimNodeId;

	// Asset players reset to the end and check the start boundary when playing in reverse.
	if (!std::isfinite(Options.PlayRate))
	{
		Fail("Play rate must be finite.");
		return InvalidAnimNodeId;
	}

	UAnimSequence *Sequence = UAssetManager::GetAssetByPath<UAnimSequence>(ResolveAssetKey(ClipName));

	if (!ValidateAsset(Sequence))
		return InvalidAnimNodeId;

	auto Node = MakeUnique<FAnimNode_SequencePlayer>();

	Node->Sequence = Sequence;
	Node->bLoop = Options.bLoop;
	Node->bPlaying = Options.bPlaying;
	Node->PlayRate = Options.PlayRate;

	FPlayerEntry Entry;
	Entry.Kind = EPlayerKind::Sequence;
	Entry.Node = std::move(Node);

	const FAnimNodeId Id = static_cast<FAnimNodeId>(Players.size());

	Players.Add(std::move(Entry));
	return Id;
}

FAnimNodeId UStateMachineAnimInstance::CreateBlendSpacePlayer(const FString &BlendSpaceName,
															  const FAnimPlayerOptions &Options)
{
	if (!RequireBuilding())
		return InvalidAnimNodeId;

	if (!std::isfinite(Options.PlayRate))
	{
		Fail("Play rate must be finite.");
		return InvalidAnimNodeId;
	}

	UBlendSpace *BlendSpace = UAssetManager::GetAssetByPath<UBlendSpace>(ResolveAssetKey(BlendSpaceName));

	if (!ValidateAsset(BlendSpace))
		return InvalidAnimNodeId;

	if (BlendSpace->GetDimension() != 1 || BlendSpace->GetNumSamples() == 0)
	{
		Fail("A non-empty 1D BlendSpace is required.");
		return InvalidAnimNodeId;
	}

	// BlendSpace에 포함된 각 Clip도 검증한다.
	for (const FBlendSample &Sample : BlendSpace->GetSamples())
	{
		if (!ValidateAsset(Sample.Animation))
			return InvalidAnimNodeId;
	}

	auto Node = MakeUnique<FAnimNode_BlendSpacePlayer>();

	Node->BlendSpace = BlendSpace;
	Node->bLoop = Options.bLoop;
	Node->bPlaying = Options.bPlaying;
	Node->PlayRate = Options.PlayRate;

	FPlayerEntry Entry;
	Entry.Kind = EPlayerKind::BlendSpace;
	Entry.Node = std::move(Node);

	const FAnimNodeId Id = static_cast<FAnimNodeId>(Players.size());

	Players.Add(std::move(Entry));
	return Id;
}

UStateMachineAnimInstance::FPlayerEntry *UStateMachineAnimInstance::FindPlayer(FAnimNodeId NodeId)
{
	if (NodeId < 0 || static_cast<size_t>(NodeId) >= Players.size())
	{
		return nullptr;
	}

	return &Players[NodeId];
}

FAnimStateId UStateMachineAnimInstance::AddState(const FString &Name, FAnimNodeId NodeId)
{
	if (!RequireBuilding())
		return InvalidAnimStateId;

	if (Name.empty() || !FindPlayer(NodeId) || (LinkedPoseNodes.Find(NodeId) != -1))
	{
		Fail("State requires a name and an unshared pose root.");
		return InvalidAnimStateId;
	}

	const FName StateName(Name.c_str());

	for (const FAnimStateDefinition &Existing : Definition.States)
	{
		if (Existing.Name == StateName)
		{
			Fail("Duplicate state name.");
			return InvalidAnimStateId;
		}

		// 상태마다 독립적인 재생 시간을 갖게 한다.
		// 같은 에셋을 쓰더라도 플레이어는 별도로 생성한다.
		if (Existing.RootNodeIndex == NodeId)
		{
			Fail("A player node cannot be shared by multiple states.");
			return InvalidAnimStateId;
		}
	}

	FAnimStateDefinition State;
	State.Name = StateName;
	State.RootNodeIndex = NodeId;

	const FAnimStateId Id = Definition.States.Num();
	Definition.States.Add(State);

	return Id;
}

bool UStateMachineAnimInstance::SetEntryState(FAnimStateId StateId)
{
	if (!RequireBuilding())
		return false;

	if (!Definition.States.IsValidIndex(StateId))
		return Fail("Invalid entry state.");

	Definition.EntryState = StateId;
	return true;
}

FAnimConditionId UStateMachineAnimInstance::AddCondition(const FAnimConditionDefinition &Condition)
{
	if (!RequireBuilding())
		return InvalidAnimConditionId;

	const FAnimConditionId Id = Definition.Conditions.Num();

	// 기존 상태 머신은 자식 조건이 먼저 등록되어야 한다.
	for (FAnimConditionId Child : Condition.Children)
	{
		if (Child < 0 || Child >= Id)
		{
			Fail("Condition children must refer to earlier conditions.");
			return InvalidAnimConditionId;
		}
	}

	if (Condition.Type == EAnimConditionType::Custom && !Condition.Callback)
	{
		Fail("Custom condition requires a callback.");
		return InvalidAnimConditionId;
	}

	Definition.Conditions.Add(Condition);
	return Id;
}

FAnimConditionId UStateMachineAnimInstance::AddCustomCondition(FAnimConditionCallback Callback)
{
	FAnimConditionDefinition Condition;
	Condition.Type = EAnimConditionType::Custom;
	Condition.Callback = std::move(Callback);

	return AddCondition(Condition);
}

FAnimTransitionId UStateMachineAnimInstance::AddTransition(FAnimStateId From, FAnimStateId To,
														   const FAnimTransitionOptions &Options)
{
	return AddTransitionInternal(From, To, false, Options);
}

FAnimTransitionId UStateMachineAnimInstance::AddAnyStateTransition(FAnimStateId To,
																   const FAnimTransitionOptions &Options)
{
	return AddTransitionInternal(InvalidAnimStateId, To, true, Options);
}

FAnimTransitionId UStateMachineAnimInstance::AddTransitionInternal(FAnimStateId From, FAnimStateId To, bool bAnyState,
																   const FAnimTransitionOptions &Options)
{
	if (!RequireBuilding())
		return InvalidAnimTransitionId;

	if (!Definition.States.IsValidIndex(To) || (!bAnyState && !Definition.States.IsValidIndex(From)))
	{
		Fail("Transition refers to an invalid state.");
		return InvalidAnimTransitionId;
	}

	if (!Definition.Conditions.IsValidIndex(Options.Condition))
	{
		Fail("Transition requires a valid condition.");
		return InvalidAnimTransitionId;
	}

	if (!std::isfinite(Options.Blend.Duration) || Options.Blend.Duration < 0.0f)
	{
		Fail("Blend duration must be finite and non-negative.");
		return InvalidAnimTransitionId;
	}

	FAnimTransitionDefinition Transition;
	Transition.From = From;
	Transition.To = To;
	Transition.bFromAnyState = bAnyState;

	Transition.Condition = Options.Condition;
	Transition.Priority = Options.Priority;
	Transition.Blend = Options.Blend;

	Transition.bRestartIfSameState = Options.bRestartIfSameState;

	Transition.InterruptPolicy = Options.InterruptPolicy;
	Transition.ConsumeTriggers = Options.ConsumeTriggers;

	const FAnimTransitionId Id = Definition.Transitions.Num();
	Definition.Transitions.Add(Transition);

	return Id;
}

bool UStateMachineAnimInstance::SetBool(const FString &Name, bool Value)
{
	if ((!bBuilding && !bGraphReady) || Name.empty())
		return Fail("Invalid parameter write.");

	if (!StateMachine.Parameters.SetBool(FName(Name.c_str()), Value))
		return Fail("Parameter type mismatch.");

	return true;
}

bool UStateMachineAnimInstance::SetFloat(const FString &Name, float Value)
{
	if ((!bBuilding && !bGraphReady) || Name.empty())
		return Fail("Invalid parameter write.");

	if (!StateMachine.Parameters.SetFloat(FName(Name.c_str()), Value))
		return Fail("Invalid float value or parameter type mismatch.");

	return true;
}

bool UStateMachineAnimInstance::DefineTrigger(const FString &Name)
{
	if (!RequireBuilding())
		return false;

	if (Name.empty())
		return Fail("Trigger name must not be emptry.");

	const FName Key(Name.c_str());

	// 현재 ParameterSet에는 비활성 Trigger 생성 API가 없으므로
	// 생성한 뒤 즉시 비활성화한다.
	if (!StateMachine.Parameters.Trigger(Key))
		return Fail("Parameter type mismatch");

	StateMachine.Parameters.ConsumeTrigger(Key);
	return true;
}

bool UStateMachineAnimInstance::SetTrigger(const FString &Name)
{
	if ((!bBuilding && !bGraphReady) || Name.empty())
		return Fail("Invalid trigger write.");

	const FName Key(Name.c_str());
	bool Value = false;

	// 오타로 새로운 Trigger가 생기지 않도록 사전 정의를 요구한다.
	if (!StateMachine.Parameters.TryGetTrigger(Key, Value))
		return Fail("Trigger has not been defined.");

	return StateMachine.Parameters.Trigger(Key);
}

bool UStateMachineAnimInstance::ResetTrigger(const FString &Name)
{
	if ((!bBuilding && !bGraphReady) || Name.empty())
		return Fail("Invalid trigger write.");

	const FName Key(Name.c_str());
	bool Value = false;

	if (!StateMachine.Parameters.TryGetTrigger(Key, Value))
		return Fail("Trigger has not been defined.");

	StateMachine.Parameters.ConsumeTrigger(Key);
	return true;
}

bool UStateMachineAnimInstance::TryGetBool(const FString &Name, bool &OutValue) const
{
	return StateMachine.Parameters.TryGetBool(FName(Name.c_str()), OutValue);
}

bool UStateMachineAnimInstance::TryGetFloat(const FString &Name, float &OutValue) const
{
	return StateMachine.Parameters.TryGetFloat(FName(Name.c_str()), OutValue);
}

bool UStateMachineAnimInstance::TryGetTrigger(const FString &Name, bool &OutValue) const
{
	return StateMachine.Parameters.TryGetTrigger(FName(Name.c_str()), OutValue);
}

bool UStateMachineAnimInstance::ValidateParameters()
{
	for (const FAnimConditionDefinition &Condition : Definition.Conditions)
	{
		bool BoolValue = false;
		float FloatValue = 0.0f;
		bool bValid = true;

		switch (Condition.Type)
		{
		case EAnimConditionType::BoolEquals:
			bValid = StateMachine.Parameters.TryGetBool(Condition.ParameterName, BoolValue);
			break;

		case EAnimConditionType::FloatCompare:
			bValid = StateMachine.Parameters.TryGetFloat(Condition.ParameterName, FloatValue);
			break;

		case EAnimConditionType::TriggerActive:
			bValid = StateMachine.Parameters.TryGetTrigger(Condition.ParameterName, BoolValue);
			break;

		default:
			break;
		}

		if (!bValid)
			return Fail("Condition parameter is missing or has the wrong type.");
	}

	for (const FAnimTransitionDefinition &Transition : Definition.Transitions)
	{
		for (const FName &Name : Transition.ConsumeTriggers)
		{
			bool Value = false;

			if (!StateMachine.Parameters.TryGetTrigger(Name, Value))
				return Fail("Consumed trigger has not been defined.");
		}
	}

	return true;
}

bool UStateMachineAnimInstance::Commit()
{
	if (!RequireBuilding())
		return false;

	if (!IsInitialized() || !GetSkeletalMesh())
		return Fail("AnimInstance is not initialized.");

	if (!ValidateParameters())
		return false;

	TArray<FAnimNode_Base *> NodePointers;

	for (const FPlayerEntry &Entry : Players)
		NodePointers.Add(Entry.Node.get());

	if (!StateMachine.Initialize(Definition, NodePointers))
		return Fail("State machine definition validation failed.");

	StateMachine.OnEnter();

	// 최초 Evaluate 전 BlendSpace의 샘플 가중치 등을 준비한다.
	// StateMachine.Update()를 호출하면 전환까지 평가되므로
	// 여기서는 재생 노드들만 DeltaSeconds=0으로 갱신한다.
	FAnimationUpdateContext Context;
	Context.AnimInstance = nullptr;
	Context.DeltaSeconds = 0.0f;

	for (const FPlayerEntry &Entry : Players)
        if (Entry.Kind != EPlayerKind::Graph) Entry.Node->Update(Context);

	RootNode = &StateMachine;

	bBuilding = false;
	bGraphReady = true;

	LastError.clear();
	return true;
}

bool UStateMachineAnimInstance::SetBlendSpaceInput(FAnimNodeId NodeId, float Value)
{
	if (!std::isfinite(Value))
		return Fail("BlendSpace input must be finite.");

	FPlayerEntry *Entry = FindPlayer(NodeId);

	if (!Entry || Entry->Kind != EPlayerKind::BlendSpace)
		return Fail("Node is not a BlendSpace player.");

	auto *Node = static_cast<FAnimNode_BlendSpacePlayer *>(Entry->Node.get());

	Node->X = Value;
	return true;
}

bool UStateMachineAnimInstance::SetPlayRate(FAnimNodeId NodeId, float Value)
{
	if (!std::isfinite(Value))
		return Fail("Play rate must be finite.");

	FPlayerEntry *Entry = FindPlayer(NodeId);

	if (!Entry || Entry->Kind == EPlayerKind::Graph)
		return Fail("Invalid player node.");

	static_cast<FAnimNode_AssetPlayerBase*>(Entry->Node.get())->PlayRate = Value;
	return true;
}

bool UStateMachineAnimInstance::SetPlaying(FAnimNodeId NodeId, bool bPlaying)
{
	FPlayerEntry *Entry = FindPlayer(NodeId);

	if (!Entry || Entry->Kind == EPlayerKind::Graph)
		return Fail("Invalid player node.");

	static_cast<FAnimNode_AssetPlayerBase*>(Entry->Node.get())->bPlaying = bPlaying;
	return true;
}

FAnimStateId UStateMachineAnimInstance::GetCurrentState() const
{
	return bGraphReady ? StateMachine.GetCurrentState() : InvalidAnimStateId;
}

bool UStateMachineAnimInstance::IsSourceFinished() const
{
	return bGraphReady && StateMachine.IsClipFinished();
}

FAnimNodeId UStateMachineAnimInstance::RegisterPoseRoot(TUniquePtr<FAnimNode_Base> Node)
{
    if (!RequireBuilding() || !Node) return InvalidAnimNodeId;
    FPlayerEntry Entry;
    Entry.Kind = EPlayerKind::Graph;
    Entry.Node = std::move(Node);
    Players.Add(std::move(Entry));
    return static_cast<int>(Players.size()) - 1;
}

FAnimNodeId UStateMachineAnimInstance::CreateTwoWayBlend(FAnimNodeId A, FAnimNodeId B, float Alpha)
{
    if (!RequireBuilding()) return InvalidAnimNodeId;
    if (A == B || !FindPlayer(A) || !FindPlayer(B) || !std::isfinite(Alpha) ||
        (LinkedPoseNodes.Find(A) != -1) || (LinkedPoseNodes.Find(B) != -1))
    {
        Fail("TwoWayBlend requires two distinct, unshared inputs and a finite alpha.");
        return InvalidAnimNodeId;
    }
    for (const auto& State : Definition.States)
        if (State.RootNodeIndex == A || State.RootNodeIndex == B)
        {
            Fail("A state root cannot also be used as a blend input.");
            return InvalidAnimNodeId;
        }
    auto Blend = MakeUnique<FAnimNode_TwoWayBlend>();
    Blend->A.LinkedNode = FindPlayer(A)->Node.get();
    Blend->B.LinkedNode = FindPlayer(B)->Node.get();
    Blend->Alpha = std::clamp(Alpha, 0.0f, 1.0f);
    const auto Id = RegisterPoseRoot(std::move(Blend));
    if (Id != InvalidAnimNodeId) { LinkedPoseNodes.Add(A); LinkedPoseNodes.Add(B); }
    return Id;
}

bool UStateMachineAnimInstance::SetBlendAlpha(FAnimNodeId NodeId, float Alpha)
{
    auto* Entry = FindPlayer(NodeId);
    auto* Blend = Entry ? dynamic_cast<FAnimNode_TwoWayBlend*>(Entry->Node.get()) : nullptr;
    if (!Blend || !std::isfinite(Alpha)) return Fail("Expected a TwoWayBlend node and a finite alpha.");
    Blend->Alpha = std::clamp(Alpha, 0.0f, 1.0f);
    return true;
}
