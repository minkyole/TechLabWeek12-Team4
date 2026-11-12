#include "EnginePCH.h"
#include "StateMachineDebug.h"
#include "Animation/AssetAnimInstance.h"
#include "Core/Misc/FileHelper.h"
#include <algorithm>
#include <filesystem>

FString StateMachineAssetKey(const FString &Path)
{
	if (Path.empty())
		return {};
	auto native = FFileHelper::ToNativePath(Path).lexically_normal();
	if (native.is_relative() && native.begin() != native.end())
	{
		FString first = FFileHelper::ToUtf8Path(*native.begin());
		if (_stricmp(first.c_str(), "Assets") != 0)
			native = std::filesystem::path("Assets") / native;
	}
	std::error_code error;
	auto root = std::filesystem::weakly_canonical("Assets", error);
	if (error)
		return {};
	auto absolute = std::filesystem::weakly_canonical(native, error);
	if (error)
		return {};
	auto normalized = [](const std::filesystem::path &value) {
		auto text = FFileHelper::ToUtf8Path(value);
		for (char &c : text)
		{
			if (c == '\\')
				c = '/';
			else if (c >= 'A' && c <= 'Z')
				c = char(c - 'A' + 'a');
		}
		return text;
	};
	auto prefix = normalized(root) + "/", full = normalized(absolute);
	if (full.size() <= prefix.size() || full.compare(0, prefix.size(), prefix) != 0)
		return {};
	return full.substr(prefix.size());
}
bool StateMachineDefinitionSignature(const FAnimStateMachineAssetData &Source, FString &Signature, FString &Error)
{
	Signature.clear();
	auto data = Source;
	if (std::any_of(data.Transitions.begin(), data.Transitions.end(),
					[](const auto &t) { return !t.Rule.Nodes.empty(); }) &&
		!data.CompileRules(Error))
		return false;
	// Authoring positions and unused rule nodes intentionally do not participate.
	json definition = {{"Mesh", data.MeshKey},		  {"Entry", data.Entry},		 {"Script", data.UpdateScript},
					   {"States", json::array()},	  {"Parameters", json::array()}, {"Conditions", json::array()},
					   {"Transitions", json::array()}};
	std::sort(data.States.begin(), data.States.end(), [](const auto &a, const auto &b) { return a.Id < b.Id; });
    for (const auto &s : data.States)
    {
        auto Graph = s.PoseGraph.Nodes.empty() ? FAnimPoseGraph::SinglePlayer(s.AssetKey,s.BlendSpace,s.BlendParameter,s.Options.bLoop,s.Options.bPlaying,s.Options.PlayRate) : s.PoseGraph;
        std::sort(Graph.Nodes.begin(),Graph.Nodes.end(),[](auto& A,auto& B){return A.Id<B.Id;});
        std::sort(Graph.Links.begin(),Graph.Links.end(),[](auto& A,auto& B){return A.Id<B.Id;});
        auto Pose = AnimPoseGraphJson(Graph);
        for (auto& Node : Pose["Nodes"]) { Node.erase("X"); Node.erase("Y"); }
        definition["States"].push_back({s.Id,s.Name,Pose});
    }
	std::sort(data.Parameters.begin(), data.Parameters.end(),
			  [](const auto &a, const auto &b) { return a.Name.ToString() < b.Name.ToString(); });
	for (const auto &p : data.Parameters)
		definition["Parameters"].push_back({p.Name.ToString(), (int)p.Type, p.BoolValue, p.FloatValue});
	for (const auto &c : data.Conditions)
	{
		json children = json::array();
		for (int id : c.Children)
			children.push_back(id);
		definition["Conditions"].push_back({(int)c.Type, c.ParameterName.ToString(), c.BoolValue, (int)c.CompareOp,
											c.FloatValue, c.Tolerance, children});
	}
	// Transition order resolves priority ties and therefore is part of behavior.
	for (const auto &t : data.Transitions)
	{
		const auto &o = t.Options;
		json consumed = json::array();
		for (auto n : o.ConsumeTriggers)
			consumed.push_back(n.ToString());
		definition["Transitions"].push_back({t.Id, t.From, t.To, o.Condition, o.Priority, o.Blend.Duration,
											 (int)o.Blend.Curve, (int)o.InterruptPolicy, o.bRestartIfSameState,
											 consumed});
	}
	Signature = definition.dump();
	Error.clear();
	return true;
}
FStateMachineObservation ObserveStateMachine(const UAssetAnimInstance &Instance, const FString &ExpectedSignature)
{
	FStateMachineObservation result;
	if (!Instance.IsGraphReady())
	{
		result.Message = "Animation graph is not initialized: " + Instance.GetLastError();
		return result;
	}
	result.Ready = true;
	const auto &data = Instance.GetAssetData();
	FString signature, error;
	if (ExpectedSignature.empty() || !StateMachineDefinitionSignature(data, signature, error) ||
		signature != ExpectedSignature)
	{
		result.Message =
			"Graph differs from the running snapshot. Reapply preview or restart PIE to observe this definition.";
		return result;
	}
	result.State = Instance.GetCurrentAssetState();
	auto name = [&](int id) {
		for (const auto &state : data.States)
			if (state.Id == id)
				return state.Name;
		return id == -1 ? FString("AnyState") : FString("Unknown");
	};
	if (result.State < 0)
	{
		result.Message = "Animation instance has no active state.";
		return result;
	}
	result.Compatible = true;
	result.StateName = name(result.State);
	int index = Instance.GetActiveTransitionIndex();
	if (Instance.IsTransitioning() && index >= 0 && index < (int)data.Transitions.size())
	{
		const auto &t = data.Transitions[index];
		result.Transition = t.Id;
		result.From = t.From;
		result.To = t.To;
		result.FromName = name(t.From);
		result.ToName = name(t.To);
		result.Progress = Instance.GetTransitionProgress();
	}
	return result;
}
