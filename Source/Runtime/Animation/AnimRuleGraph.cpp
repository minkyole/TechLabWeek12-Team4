#include "EnginePCH.h"
#include "AnimStateMachineAsset.h"
#include <algorithm>
#include <cmath>
#include <functional>
#include <map>
#include <set>

FAnimRuleNode *FAnimRuleGraph::Find(int Id)
{
	for (auto &n : Nodes)
		if (n.Id == Id)
			return &n;
	return nullptr;
}
const FAnimRuleNode *FAnimRuleGraph::Find(int Id) const
{
	for (auto &n : Nodes)
		if (n.Id == Id)
			return &n;
	return nullptr;
}
int FAnimRuleGraph::NextNodeId() const
{
	int id = 1;
	for (auto &n : Nodes)
		id = std::max(id, n.Id + 1);
	return id;
}
int FAnimRuleGraph::NextLinkId() const
{
	int id = 1;
	for (auto &l : Links)
		id = std::max(id, l.Id + 1);
	return id;
}
FAnimRuleGraph FAnimRuleGraph::Disabled()
{
	FAnimRuleGraph g;
	FAnimRuleNode result;
	result.Id = 1;
	result.Result = true;
	result.InputCount = 1;
	result.X = 320;
	FAnimRuleNode value;
	value.Id = 2;
	value.Condition.Type = EAnimConditionType::Always;
	value.Condition.BoolValue = false;
	g.Nodes = {result, value};
	g.Links.push_back({1, 2, 1, 0});
	return g;
}
bool FAnimRuleGraph::CanConnect(int From, int To, int Input) const
{
	auto *a = Find(From);
	auto *b = Find(To);
	if (!a || !b || a->Result || From == To || Input < 0 || Input >= b->InputCount)
		return false;
	// Adding From -> To is cyclic if To can already reach From.
	std::set<int> visited;
	std::function<bool(int)> reaches = [&](int id) {
		if (id == From)
			return true;
		if (!visited.insert(id).second)
			return false;
		for (auto &l : Links)
			if (l.From == id && !(l.To == To && l.Input == Input) && reaches(l.To))
				return true;
		return false;
	};
	return !reaches(To);
}
bool FAnimRuleGraph::Compile(const std::vector<FAnimParameter> &Parameters, std::vector<FAnimConditionDefinition> &Out,
							 int &Root, FString &Error, int *ErrorNode) const
{
	Error.clear();
	if (ErrorNode)
		*ErrorNode = 0;
	auto fail = [&](int id, const FString &message) {
		if (Error.empty())
		{
			Error = message;
			if (ErrorNode)
				*ErrorNode = id;
		}
		return -1;
	};
	std::set<int> ids, linkIds;
	std::set<std::pair<int, int>> inputs;
	int result = 0;
	for (auto &n : Nodes)
	{
		if (n.Id <= 0 || !ids.insert(n.Id).second)
		{
			fail(n.Id, "Duplicate or invalid rule node ID.");
			return false;
		}
		if (!std::isfinite(n.X) || !std::isfinite(n.Y))
		{
			fail(n.Id, "Invalid node position.");
			return false;
		}
		auto type = n.Condition.Type;
		int expected = (n.Result || type == EAnimConditionType::Not)
						   ? 1
						   : ((type == EAnimConditionType::And || type == EAnimConditionType::Or) ? n.InputCount : 0);
		if (n.InputCount != expected || n.InputCount < 0 || n.InputCount > 65534 ||
			((type == EAnimConditionType::And || type == EAnimConditionType::Or) && !n.Result && n.InputCount < 1))
		{
			fail(n.Id, "Invalid number of inputs.");
			return false;
		}
		if (n.Result)
		{
			if (result)
			{
				fail(n.Id, "Only one result node is allowed.");
				return false;
			}
			result = n.Id;
			continue;
		}
		if (type == EAnimConditionType::Custom || (int)type < 0 || (int)type > (int)EAnimConditionType::Not)
		{
			fail(n.Id, "Unsupported rule node.");
			return false;
		}
		if (type == EAnimConditionType::BoolEquals || type == EAnimConditionType::FloatCompare ||
			type == EAnimConditionType::TriggerActive)
		{
			auto required = type == EAnimConditionType::BoolEquals
								? EAnimParameterType::Bool
								: (type == EAnimConditionType::FloatCompare ? EAnimParameterType::Float
																			: EAnimParameterType::Trigger);
			bool found = false;
			for (auto &p : Parameters)
				found |= p.Name == n.Condition.ParameterName && p.Type == required;
			if (!found)
			{
				fail(n.Id, "Select a declared parameter of the correct type.");
				return false;
			}
		}
		if (type == EAnimConditionType::FloatCompare || type == EAnimConditionType::StateElapsedCompare)
		{
			if (!std::isfinite(n.Condition.FloatValue) || !std::isfinite(n.Condition.Tolerance) ||
				n.Condition.Tolerance < 0 || (int)n.Condition.CompareOp < 0 || (int)n.Condition.CompareOp > 5)
			{
				fail(n.Id, "Invalid comparison value, operator or tolerance.");
				return false;
			}
		}
	}
	if (!result)
	{
		fail(0, "Can Enter Transition output is missing.");
		return false;
	}
	for (auto &l : Links)
	{
		auto *a = Find(l.From);
		auto *b = Find(l.To);
		if (l.Id <= 0 || !linkIds.insert(l.Id).second || !a || !b || a->Result || l.Input < 0 ||
			l.Input >= b->InputCount || !inputs.insert({l.To, l.Input}).second || !CanConnect(l.From, l.To, l.Input))
		{
			fail(l.To, "Invalid or cyclic rule connection.");
			return false;
		}
	}
	std::map<int, int> compiled;
	std::set<int> visiting;
	std::vector<FAnimConditionDefinition> pending;
	std::function<int(int)> emit = [&](int id) -> int {
		if (compiled.contains(id))
			return compiled[id];
		const auto *n = Find(id);
		if (!n)
			return fail(id, "Missing rule node.");
		if (!visiting.insert(id).second)
			return fail(id, "Rule contains a cycle.");
		FAnimConditionDefinition c = n->Condition;
		c.Children.Reset();
		for (int slot = 0; slot < n->InputCount; ++slot)
		{
			auto it = std::find_if(Links.begin(), Links.end(), [&](auto &l) { return l.To == id && l.Input == slot; });
			if (it == Links.end())
				return fail(id, "Connect all inputs required by the transition output.");
			int child = emit(it->From);
			if (child < 0)
				return -1;
			c.Children.Add(child);
		}
		visiting.erase(id);
		if (n->Result)
		{
			compiled[id] = c.Children[0];
			return compiled[id];
		}
		if (c.Type == EAnimConditionType::Always && !c.BoolValue)
		{
			FAnimConditionDefinition yes;
			yes.Type = EAnimConditionType::Always;
			pending.push_back(yes);
			c.Type = EAnimConditionType::Not;
			c.Children.Add((int)Out.size() + (int)pending.size() - 1);
		}
		int index = (int)Out.size() + (int)pending.size();
		pending.push_back(c);
		compiled[id] = index;
		return index;
	};
	Root = emit(result);
	if (Root < 0)
		return false;
	Out.insert(Out.end(), pending.begin(), pending.end());
	return true;
}
FString FAnimRuleGraph::Summary() const
{
	std::set<int> visiting;
	int budget = 32;
	const char *ops[] = {"<", "<=", ">", ">=", "~=", "!~="};
	std::function<FString(int)> expression = [&](int id) -> FString {
		const auto *n = Find(id);
		if (!n)
			return "?";
		if (--budget < 0)
			return "...";
		if (!visiting.insert(id).second)
			return "[cycle]";
		auto c = n->Condition;
		auto child = [&](int slot) {
			for (auto &l : Links)
				if (l.To == id && l.Input == slot)
					return expression(l.From);
			return FString("?");
		};
		FString text;
		if (n->Result)
			text = child(0);
		else
			switch (c.Type)
			{
			case EAnimConditionType::Always:
				text = c.BoolValue ? "true" : "false";
				break;
			case EAnimConditionType::BoolEquals:
				text = (c.BoolValue ? "" : "!") + c.ParameterName.ToString();
				break;
			case EAnimConditionType::FloatCompare:
			case EAnimConditionType::StateElapsedCompare:
				text = (c.Type == EAnimConditionType::FloatCompare ? c.ParameterName.ToString()
																   : FString("StateElapsed")) +
					   " " + ops[std::clamp((int)c.CompareOp, 0, 5)] + " " + std::to_string(c.FloatValue);
				while (!text.empty() && text.back() == '0')
					text.pop_back();
				if (!text.empty() && text.back() == '.')
					text.pop_back();
				break;
			case EAnimConditionType::TriggerActive:
				text = c.ParameterName.ToString();
				break;
			case EAnimConditionType::SourceFinished:
				text = "SourceFinished";
				break;
			case EAnimConditionType::Not:
				text = "!(" + child(0) + ")";
				break;
			case EAnimConditionType::And:
			case EAnimConditionType::Or:
				text = "(";
				for (int slot = 0; slot < n->InputCount; ++slot)
				{
					if (slot)
						text += c.Type == EAnimConditionType::And ? " && " : " || ";
					text += child(slot);
				}
				text += ")";
				break;
			default:
				text = "?";
			}
		visiting.erase(id);
		return text;
	};
	for (auto &n : Nodes)
		if (n.Result)
			return expression(n.Id);
	return "No output";
}
void FAnimStateMachineAssetData::EnsureRuleGraphs()
{
	int next = 1;
	std::set<int> ids;
	for (auto &t : Transitions)
		if (t.Id > 0)
			next = std::max(next, t.Id + 1);
	for (auto &t : Transitions)
	{
		if (t.Id <= 0 || !ids.insert(t.Id).second)
		{
			t.Id = next++;
			ids.insert(t.Id);
		}
		if (!t.HasPosition)
		{
			const FAnimAssetState *a = nullptr, *b = nullptr;
			for (auto &s : States)
			{
				if (s.Id == t.From)
					a = &s;
				if (s.Id == t.To)
					b = &s;
			}
			float ax = a ? a->X : AnyX, ay = a ? a->Y : AnyY, bx = b ? b->X : 0, by = b ? b->Y : 0;
			float dx = bx - ax, dy = by - ay, len = std::sqrt(dx * dx + dy * dy);
			int parallel = 0;
			for (auto &prior : Transitions)
			{
				if (&prior == &t)
					break;
				if (prior.From == t.From && prior.To == t.To)
					++parallel;
			}
			if (t.From == t.To)
			{
				t.X = ax + 240 + parallel * 80;
				t.Y = ay - 100 - parallel * 55;
			}
			else
			{
				t.X = (ax + bx) * .5f + (len > 0 ? -dy / len : 0) * (60 + parallel * 65);
				t.Y = (ay + by) * .5f + (len > 0 ? dx / len : 1) * (60 + parallel * 65);
			}
			t.HasPosition = true;
		}
		if (!t.Rule.Nodes.empty())
			continue;
		FAnimRuleNode output;
		output.Id = 1;
		output.Result = true;
		output.InputCount = 1;
		output.X = 650;
		t.Rule.Nodes.push_back(output);
		std::map<int, int> imported;
		std::set<int> visiting;
		std::function<int(int, int)> import = [&](int index, int depth) -> int {
			if (imported.contains(index))
				return imported[index];
			if (index < 0 || index >= (int)Conditions.size() || !visiting.insert(index).second)
				return 0;
			FAnimRuleNode n;
			n.Id = t.Rule.NextNodeId();
			n.Condition = Conditions[index];
			n.InputCount = n.Condition.Children.Num();
			n.X = 400 - depth * 270.0f;
			n.Y = (n.Id - 2) * 110.0f;
			if (n.Condition.Type == EAnimConditionType::Always)
				n.Condition.BoolValue = true;
			auto children = n.Condition.Children;
			n.Condition.Children.Reset();
			int id = n.Id;
			imported[index] = id;
			t.Rule.Nodes.push_back(n);
			for (int slot = 0; slot < children.Num(); ++slot)
			{
				int child = import(children[slot], depth + 1);
				if (child)
					t.Rule.Links.push_back({t.Rule.NextLinkId(), child, id, slot});
			}
			visiting.erase(index);
			return id;
		};
		int root = import(t.Options.Condition, 0);
		if (root)
			t.Rule.Links.push_back({t.Rule.NextLinkId(), root, 1, 0});
	}
}
bool FAnimStateMachineAssetData::CompileRules(FString &Error)
{
	std::vector<FAnimConditionDefinition> conditions;
	std::vector<int> roots;
	std::set<int> ids;
	for (auto &t : Transitions)
	{
		if (t.Id <= 0 || !ids.insert(t.Id).second)
		{
			Error = "Duplicate or invalid transition ID.";
			return false;
		}
		int root = -1;
		FString detail;
		if (!t.Rule.Compile(Parameters, conditions, root, detail))
		{
			Error = "Transition " + std::to_string(t.From) + " -> " + std::to_string(t.To) + ": " + detail;
			return false;
		}
		roots.push_back(root);
	}
	Conditions = std::move(conditions);
	for (int i = 0; i < (int)Transitions.size(); ++i)
		Transitions[i].Options.Condition = roots[i];
	Error.clear();
	return true;
}
