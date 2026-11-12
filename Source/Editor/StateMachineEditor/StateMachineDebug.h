#pragma once
#include "Animation/AnimStateMachineAsset.h"
#include <functional>
#include <vector>
class UAssetAnimInstance;

// Value-only observations: PIE objects never escape the provider callback.
struct FStateMachineObservation
{
	bool Ready = false, Compatible = false;
	int State = -1, Transition = 0, From = -1, To = -1;
	float Progress = 0;
	FString StateName, FromName, ToName, Message;
};
struct FStateMachinePIETarget
{
	uint32 ActorId = 0, ComponentId = 0;
	FString Label;
	FStateMachineObservation Observation;
};
struct FStateMachinePIEFrame
{
	uint64 Session = 0;
	std::vector<FStateMachinePIETarget> Targets;
};
using FStateMachinePIEProvider = std::function<FStateMachinePIEFrame(const FString &, const FString &)>;
FString StateMachineAssetKey(const FString &Path);
bool StateMachineDefinitionSignature(const FAnimStateMachineAssetData &Data, FString &Signature, FString &Error);
FStateMachineObservation ObserveStateMachine(const UAssetAnimInstance &Instance, const FString &ExpectedSignature);
