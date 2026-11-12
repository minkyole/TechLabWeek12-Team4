#pragma once
#include "Animation/StateMachineAnimInstance.h"
#include "Asset/RenderAsset.h"
#include "Animation/AnimGraph.h"
#include <vector>

// Editor-only authoring data, deliberately independent of ImGui and runtime indices.
struct FAnimRuleNode
{
	int Id = 0;
	FAnimConditionDefinition Condition;
	bool Result = false;
	int InputCount = 0;
	float X = 0, Y = 0;
};
struct FAnimRuleLink
{
	int Id = 0, From = 0, To = 0, Input = 0;
};
struct FAnimRuleGraph
{
	std::vector<FAnimRuleNode> Nodes;
	std::vector<FAnimRuleLink> Links;
	FAnimRuleNode *Find(int Id);
	const FAnimRuleNode *Find(int Id) const;
	int NextNodeId() const;
	int NextLinkId() const;
	static FAnimRuleGraph Disabled();
	bool CanConnect(int From, int To, int Input) const;
	bool Compile(const std::vector<FAnimParameter> &Parameters, std::vector<FAnimConditionDefinition> &Out, int &Root,
				 FString &Error, int *ErrorNode = nullptr) const;
	FString Summary() const;
};
struct FAnimAssetState
{
	FAnimPoseGraph PoseGraph;
	int Id = 0;
	FString Name = "State", AssetKey, BlendParameter;
	bool BlendSpace = false;
	FAnimPlayerOptions Options;
	float X = 0, Y = 0;
};
struct FAnimAssetTransition
{
	int From = -1, To = -1; // -1 source means AnyState
	FAnimTransitionOptions Options;
	int Id = 0;
	float X = 0, Y = 0;
	bool HasPosition = false;
	FAnimRuleGraph Rule;
};
struct FAnimStateMachineAssetData
{
	FString MeshKey, UpdateScript;
	int Entry = -1;
	float EntryX = -230, EntryY = 40, AnyX = -230, AnyY = -100;
	std::vector<FAnimAssetState> States;
	std::vector<FAnimParameter> Parameters;
	std::vector<FAnimConditionDefinition> Conditions;
	std::vector<FAnimAssetTransition> Transitions;
	void EnsureRuleGraphs();
	bool CompileRules(FString &Error);
	bool Validate(FString &Error) const;
	bool Build(UStateMachineAnimInstance &Instance, FString &Error) const;
};
class UAnimStateMachineAsset : public URenderAsset
{
	DECLARE_CLASS(UAnimStateMachineAsset, URenderAsset)
  public:
	FAnimStateMachineAssetData Data;
};
