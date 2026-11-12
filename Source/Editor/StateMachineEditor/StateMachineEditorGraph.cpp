#include "EnginePCH.h"
#include "StateMachineEditorPanel.h"
#include "Animation/AssetAnimInstance.h"
#include "Asset/AssetManager.h"
#include "Animation/AnimSequence.h"
#include "Render/Mesh.h"
#include "Editor/SkeletalMeshViewer/SkeletalMeshViewerPanel.h"
#include "imgui_node_editor.h"
#include "Editor/AnimGraphEditor/AnimationEditorStyle.h"
#include "Core/Misc/FileHelper.h"
#include <algorithm>
#include <cmath>
#include <functional>
#include <queue>
#include <limits>

namespace ed = ax::NodeEditor;
namespace
{
template <typename DrawItems>
void RuleCombo(const char* Label, const char* Preview, bool InCanvas, DrawItems Draw)
{
	if (!InCanvas)
	{
		if (ImGui::BeginCombo(Label, Preview))
		{
			Draw();
			ImGui::EndCombo();
		}
		return;
	}

	ImGui::PushID(Label);
	const float width = ImGui::CalcItemWidth();
	const std::string buttonLabel = std::string(Preview) + "  v###Value";
	const bool open = ImGui::Button(buttonLabel.c_str(), ImVec2(width, 0));
	ImVec2 popupPosition = ed::CanvasToScreen(ImGui::GetItemRectMin());
	popupPosition.y += ImGui::GetItemRectSize().y * ed::GetCurrentZoom();
	ImGui::SameLine();
	ImGui::TextUnformatted(Label);
	// Suspend and resume in the canvas window, outside the popup's Begin/End pair.
	ed::Suspend();
	if (open)
		ImGui::OpenPopup("Options");
	ImGui::SetNextWindowPos(popupPosition, ImGuiCond_Appearing);
	if (ImGui::BeginPopup("Options"))
	{
		Draw();
		ImGui::EndPopup();
	}
	ed::Resume();
	ImGui::PopID();
}

enum EGraphKind : uintptr_t
{
	StateNode = 1,
	SpecialNode = 2,
	TransitionLink = 6,
	EntryLink = 7,
	RuleNode = 8,
	RulePin = 9,
	RuleLink = 10,
	DirectPin = 12,
	InitialPin = 13
};
uintptr_t Key(EGraphKind Kind, int Id = 0, int Slot = 0)
{
	return (uintptr_t(Kind) << 56) | (uintptr_t(uint32_t(Id)) << 16) | uint16_t(Slot);
}
int Kind(uintptr_t Id)
{
	return int(Id >> 56);
}
int Object(uintptr_t Id)
{
	return int((Id >> 16) & 0xffffffff);
}
int Slot(uintptr_t Id)
{
	return int(Id & 0xffff);
}
ed::NodeId Node(EGraphKind k, int id = 0)
{
	return ed::NodeId(Key(k, id));
}
ed::PinId Pin(EGraphKind k, int id = 0, int slot = 0)
{
	return ed::PinId(Key(k, id, slot));
}
const char *TypeName(EAnimConditionType Type)
{
	switch (Type)
	{
	case EAnimConditionType::Always:
		return "Bool Constant";
	case EAnimConditionType::BoolEquals:
		return "Bool Compare";
	case EAnimConditionType::FloatCompare:
		return "Float Compare";
	case EAnimConditionType::TriggerActive:
		return "Trigger Active";
	case EAnimConditionType::SourceFinished:
		return "Source Finished";
	case EAnimConditionType::StateElapsedCompare:
		return "State Elapsed";
	case EAnimConditionType::And:
		return "AND";
	case EAnimConditionType::Or:
		return "OR";
	case EAnimConditionType::Not:
		return "NOT";
	default:
		return "Invalid";
	}
}
void PinLabel(ed::PinId Id, ed::PinKind Kind, const char *Text)
{
	ed::BeginPin(Id, Kind);
	auto P=ImGui::GetCursorScreenPos(); ImGui::Dummy(ImVec2(12,14));
    ImGui::GetWindowDrawList()->AddCircleFilled(ImVec2(P.x+6,P.y+7),4,IM_COL32(220,65,78,255));
    ImGui::SameLine(); ImGui::TextUnformatted(Text);
	ed::EndPin();
}
void Place(std::set<uintptr_t> &Placed, ed::NodeId Id, float X, float Y)
{
	if (Placed.insert(Id.Get()).second)
		ed::SetNodePosition(Id, ImVec2(X, Y));
}
bool Capture(ed::NodeId Id, float &X, float &Y)
{
	auto p = ed::GetNodePosition(Id);
	if (std::abs(p.x - std::floor(X)) > .01f || std::abs(p.y - std::floor(Y)) > .01f)
	{
		X = p.x;
		Y = p.y;
		return true;
	}
	return false;
}
constexpr int NoSource = -3;
constexpr int EntrySource = -2;
constexpr int AnySource = -1;
ImVec2 Add(ImVec2 a, ImVec2 b)
{
	return ImVec2(a.x + b.x, a.y + b.y);
}
ImVec2 Sub(ImVec2 a, ImVec2 b)
{
	return ImVec2(a.x - b.x, a.y - b.y);
}
ImVec2 Scale(ImVec2 a, float v)
{
	return ImVec2(a.x * v, a.y * v);
}
float Length(ImVec2 a)
{
	return std::sqrt(a.x * a.x + a.y * a.y);
}
ImVec2 Unit(ImVec2 a)
{
	float length = Length(a);
	return length > .001f ? Scale(a, 1 / length) : ImVec2(1, 0);
}
ImVec2 Snap(ImVec2 a)
{
	return ImVec2(std::floor(a.x), std::floor(a.y));
}
ed::NodeId StateId(int state)
{
	return state < 0 ? Node(SpecialNode, state == EntrySource ? 0 : 1) : Node(StateNode, state);
}
ImVec2 CompactSize(const char *name, bool special)
{
	float font = ImGui::GetFontSize();
	return ImVec2(std::max(special ? 86.f : 160.f, ImGui::CalcTextSize(name).x + 24.f),
				  20.f + font + (special ? 0.f : font * .82f + 4.f));
}
struct FStateBox
{
	ImVec2 Min, Size;
	ImVec2 Center() const
	{
		return Add(Min, Scale(Size, .5f));
	}
};
struct FStateConnection
{
	int Id = 0, From = 0, To = 0;
	bool Initial = false, Loop = false;
	ImVec2 P0, P1, P2, P3;
	float Strength = 0;
	ImVec2 Point(float t) const
	{
		float u = 1 - t;
		return Add(Add(Scale(P0, u * u * u), Scale(P1, 3 * u * u * t)),
				   Add(Scale(P2, 3 * u * t * t), Scale(P3, t * t * t)));
	}
	ed::LinkId LinkId() const
	{
		return ed::LinkId(Initial ? Key(EntryLink) : Key(TransitionLink, Id));
	}
	ed::PinId Endpoint(int slot) const
	{
		return Pin(Initial ? InitialPin : DirectPin, Id, slot);
	}
};
ImVec2 Boundary(const FStateBox &box, ImVec2 start, ImVec2 direction)
{
	float distance = std::numeric_limits<float>::max();
	if (std::abs(direction.x) > .0001f)
		distance = std::min(distance, ((direction.x > 0 ? box.Min.x + box.Size.x : box.Min.x) - start.x) / direction.x);
	if (std::abs(direction.y) > .0001f)
		distance = std::min(distance, ((direction.y > 0 ? box.Min.y + box.Size.y : box.Min.y) - start.y) / direction.y);
	return Snap(Add(start, Scale(direction, std::max(0.f, distance))));
}
std::vector<FStateConnection> Connections(const FAnimStateMachineAssetData &data, const std::map<int, FStateBox> &boxes)
{
	std::vector<FStateConnection> paths;
	if (data.Entry >= 0 && boxes.contains(data.Entry))
	{
		FStateConnection p;
		p.Initial = true;
		p.From = EntrySource;
		p.To = data.Entry;
		paths.push_back(p);
	}
	for (const auto &t : data.Transitions)
		if (boxes.contains(t.From) && boxes.contains(t.To))
		{
			FStateConnection p;
			p.Id = t.Id;
			p.From = t.From;
			p.To = t.To;
			paths.push_back(p);
		}
	for (auto &p : paths)
	{
		const auto &a = boxes.at(p.From);
		const auto &b = boxes.at(p.To);
		std::vector<const FStateConnection *> parallel;
		for (const auto &other : paths)
			if (other.Initial == p.Initial && std::min(other.From, other.To) == std::min(p.From, p.To) &&
				std::max(other.From, other.To) == std::max(p.From, p.To))
				parallel.push_back(&other);
		std::sort(parallel.begin(), parallel.end(),
				  [](auto *x, auto *y) { return x->From != y->From ? x->From < y->From : x->Id < y->Id; });
		int rank = 0;
		for (int i = 0; i < (int)parallel.size(); ++i)
			if (parallel[i] == &p)
				rank = i;
		if (p.From == p.To)
		{
			// Unit strength leaves the direction vectors as explicit cubic control offsets.
			p.Loop = true;
			p.Strength = 1;
			p.P0 = Snap(ImVec2(a.Min.x + a.Size.x, a.Center().y - 12));
			p.P3 = Snap(ImVec2(a.Min.x + a.Size.x, a.Center().y + 12));
			float reach = 85.f + rank * 45.f;
			p.P1 = Add(p.P0, ImVec2(reach, 0));
			p.P2 = Add(p.P3, ImVec2(reach, 0));
		}
		else
		{
			ImVec2 canonical =
				Unit(Sub(boxes.at(std::max(p.From, p.To)).Center(), boxes.at(std::min(p.From, p.To)).Center()));
			ImVec2 normal(-canonical.y, canonical.x);
			float available = std::numeric_limits<float>::max();
			for (const auto *box : {&a, &b})
			{
				if (std::abs(normal.x) > .001f)
					available = std::min(available, std::max(0.f, box->Size.x * .5f - 8) / std::abs(normal.x));
				if (std::abs(normal.y) > .001f)
					available = std::min(available, std::max(0.f, box->Size.y * .5f - 8) / std::abs(normal.y));
			}
			float maxRank = ((int)parallel.size() - 1) * .5f;
			float spacing = maxRank > 0 ? std::min(16.f, available / maxRank) : 0;
			ImVec2 offset = Scale(normal, (rank - maxRank) * spacing), direction = Unit(Sub(b.Center(), a.Center()));
			p.P0 = Boundary(a, Add(a.Center(), offset), direction);
			p.P3 = Boundary(b, Add(b.Center(), offset), Scale(direction, -1));
			p.P1 = p.P0;
			p.P2 = p.P3;
		}
	}
	return paths;
}
void HiddenEndpoint(const FStateConnection &path, int slot, ImVec2 layoutOrigin)
{
	ImVec2 point = slot ? path.P3 : path.P0;
	ImVec2 direction = slot ? Sub(path.P2, path.P3) : Sub(path.P1, path.P0);
	// Internal pins have no layout footprint. The node submits its sized Dummy afterwards.
	ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));
	ImGui::SetCursorScreenPos(layoutOrigin);
	ed::PushStyleVar(ed::StyleVar_LinkStrength, path.Strength);
	ed::PushStyleVar(slot ? ed::StyleVar_TargetDirection : ed::StyleVar_SourceDirection, direction);
	ed::PushStyleVar(ed::StyleVar_PinRadius, 0.f);
	ed::PushStyleVar(ed::StyleVar_PinArrowSize, 0.f);
	ed::PushStyleVar(ed::StyleVar_PinArrowWidth, 0.f);
	ed::PushStyleColor(ed::StyleColor_PinRect, ImVec4(0, 0, 0, 0));
	ed::PushStyleColor(ed::StyleColor_PinRectBorder, ImVec4(0, 0, 0, 0));
	ed::BeginPin(path.Endpoint(slot), slot ? ed::PinKind::Input : ed::PinKind::Output);
	// A zero-area hit rectangle keeps the internal pin from capturing state clicks.
	ed::PinRect(point, point);
	ed::PinPivotRect(point, point);
	// ItemAdd inside BeginPin does not call ItemSize or consume SetCursorScreenPos.
	ImGui::Dummy(ImVec2(0, 0));
	ed::EndPin();
	ed::PopStyleColor(2);
	ed::PopStyleVar(5);
	ImGui::PopStyleVar();
}
void DrawDirection(ImDrawList *draw, const FStateConnection &path, ImU32 color, bool diamond)
{
	ImVec2 direction = Unit(Sub(path.P3, path.Point(.96f))), normal(-direction.y, direction.x);
	float head = std::min(11.f, std::max(4.f, Length(Sub(path.P3, path.P0)) * .25f));
	ImVec2 back = Sub(path.P3, Scale(direction, head));
	draw->AddTriangleFilled(path.P3, Add(back, Scale(normal, head * .45f)), Sub(back, Scale(normal, head * .45f)),
							color);
	if (diamond)
	{
		auto mid = path.Point(.5f);
		ImVec2 top = Add(mid, ImVec2(0, -5)), right = Add(mid, ImVec2(5, 0)), bottom = Add(mid, ImVec2(0, 5)),
			   left = Add(mid, ImVec2(-5, 0));
		draw->AddQuadFilled(top, right, bottom, left, IM_COL32(35, 38, 44, 255));
		draw->AddQuad(top, right, bottom, left, color, 1.5f);
	}
}

} // namespace
void FStateMachineEditorPanel::ClearContexts()
{
    PoseEditors.clear(); ActivePose = -1; PendingSelection.clear(); PendingFocusRule=0;
	for (auto &[id, s] : Sessions)
	{
		if (ed::GetCurrentEditor() == s.Context)
			ed::SetCurrentEditor(nullptr);
		ed::DestroyEditor(s.Context);
	}
	Sessions.clear();
	CreateFromPin = ContextObject = 0;
	PendingSource = -3;
	PendingStartFrame = -1;
	QueuedTransition = 0;
}
FStateMachineEditorPanel::FGraphSession &FStateMachineEditorPanel::GraphSession()
{
	auto &session = Sessions[ActiveRule];
	if (!session.Context)
	{
		ed::Config config;
		if(!Path.empty())
        {
            std::error_code EC; fs::create_directories("Saved/AnimationEditor",EC);
            session.SettingsPath="Saved/AnimationEditor/"+std::to_string(std::hash<FString>{}(Path+"/Rule/"+std::to_string(ActiveRule)))+".json";
            session.Fit=!fs::exists(session.SettingsPath,EC);
        }
        config.SettingsFile=nullptr; config.UserPointer=&session;
        config.SaveSettings=[](const char* Data,size_t Size,ed::SaveReasonFlags,void* User) { auto* S=static_cast<FGraphSession*>(User); return !S->SettingsPath.empty() && FFileHelper::SaveStringToFile(FString(Data,Size),S->SettingsPath); };
        config.LoadSettings=[](char* Data,void* User)->size_t { auto* S=static_cast<FGraphSession*>(User); FString Text; if(S->SettingsPath.empty() || !FFileHelper::LoadFileToString(Text,S->SettingsPath)) return 0; if(Data) memcpy(Data,Text.data(),Text.size()); return Text.size(); };
		config.NavigateButtonIndex = 2;
		config.ContextMenuButtonIndex = 1;
		config.EnableSmoothZoom = true;
		session.Context = ed::CreateEditor(&config);
	}
	return session;
}
void FStateMachineEditorPanel::AddTransition(int From, int To)
{
	if (!FindState(To) || (From != -1 && !FindState(From)))
		return;
	FAnimAssetTransition t;
	t.From = From;
	t.To = To;
	int next = 1;
	for (auto &existing : Working.Transitions)
		next = std::max(next, existing.Id + 1);
	t.Id = next;
	t.Rule = FAnimRuleGraph::Disabled();
	Working.Transitions.push_back(t);
	Working.EnsureRuleGraphs();
	Selected = -1;
	SelectedTransition = (int)Working.Transitions.size() - 1;
	Dirty = true;
}
void FStateMachineEditorPanel::DeleteSelection()
{
	auto &session = GraphSession();
	auto *previous = ed::GetCurrentEditor();
	ed::SetCurrentEditor(session.Context);
	std::vector<ed::NodeId> nodes(ed::GetSelectedObjectCount());
	int count = ed::GetSelectedNodes(nodes.data(), (int)nodes.size());
	for (int i = 0; i < count; ++i)
		ed::DeleteNode(nodes[i]);
	std::vector<ed::LinkId> links(ed::GetSelectedObjectCount());
	count = ed::GetSelectedLinks(links.data(), (int)links.size());
	for (int i = 0; i < count; ++i)
		ed::DeleteLink(links[i]);
	ed::SetCurrentEditor(previous);
}
void FStateMachineEditorPanel::DrawGraph()
{
    if (auto* State = FindState(ActivePose))
    {
        if (ImGui::Button("State Machine")) { ActivePose = -1; return; }
        ImGui::SameLine(); ImGui::Text(" > %s > Pose", State->Name.c_str());
        if (State->PoseGraph.Nodes.empty()) { State->PoseGraph = FAnimPoseGraph::SinglePlayer(State->AssetKey,State->BlendSpace,State->BlendParameter,State->Options.bLoop,State->Options.bPlaying,State->Options.PlayRate); Dirty = true; }
        Dirty |= PoseEditor().Draw(State->PoseGraph,Working.Parameters,Working.MeshKey);
        return;
    }
	if (ActiveRule)
		PendingSource = -3;
	if (ActiveRule && !FindTransition(ActiveRule))
		ActiveRule = 0;
	if (ImGui::Button("State Machine"))
	{
		ActiveRule = 0;
		SelectedRuleNode = 0;
		PendingSource = -3;
	}
	if (auto *t = FindTransition(ActiveRule))
	{
		ImGui::SameLine();
		FString title = FString(" > ") + StateName(t->From) + " -> " + StateName(t->To);
		ImGui::TextUnformatted(title.c_str());
	}
	auto &session = GraphSession();
	auto *previous = ed::GetCurrentEditor();
	ed::SetCurrentEditor(session.Context);
    FAnimationGraphStyle GraphStyle;
	if (ImGui::Button("Frame All"))
		session.Fit = true;
	ImGui::SameLine();
	if (ImGui::Button("Frame Selection"))
		ed::NavigateToSelection();
	ImGui::SameLine();
	if (ImGui::Button("Delete Selection"))
		DeleteSelection();
	if (!ActiveRule)
	{
		ImGui::SameLine();
		if (ImGui::Button("Auto Arrange"))
			AutoArrange();
	}
	ImGui::TextDisabled(ActiveRule ? "Drag Bool pins to connect | Middle drag: pan | Wheel: zoom"
								   : "Right click a state: Make Transition | Middle drag: pan | Wheel: zoom");
	if (!ActiveRule && PendingSource != -3)
	{
		ImGui::TextColored(ImVec4(1, .8f, .3f, 1), "Choose target for %s. Escape or background click cancels.",
						   PendingSource == -2 ? "Entry" : StateName(PendingSource));
	}
	ed::EnableShortcuts(Focused && !ImGui::GetIO().WantTextInput);
	ed::Begin(ActiveRule ? "Transition Rule" : "State Machine Canvas",
			  ImVec2(0, std::max(260.0f, ImGui::GetContentRegionAvail().y)));
    bool CanvasActive=ed::IsActive()&&!ImGui::GetIO().WantTextInput;
    if(CanvasActive) { if(ImGui::IsKeyPressed(ImGuiKey_F)) ed::NavigateToSelection(); if(ImGui::IsKeyPressed(ImGuiKey_Home)) session.Fit=true; }
	const bool stateGraph = ActiveRule == 0;
    HoveredRuleTooltip=0;
	if (ActiveRule)
		DrawRuleGraph(session);
	else
		DrawStateGraph(session);
    if(!PendingSelection.empty()) { ed::ClearSelection(); for(int Id:PendingSelection) ed::SelectNode(ActiveRule?Node(RuleNode,Id):StateId(Id),true); PendingSelection.clear(); }
    if(ed::BeginShortcut()) { bool Copy=ed::AcceptCopy(),Paste=ed::AcceptPaste(),Duplicate=ed::AcceptDuplicate(); ClipboardActions(Copy,Paste,Duplicate); }
    ed::EndShortcut();
	if (session.Fit)
	{
		ed::NavigateToContent(0);
		session.Fit = false;
	}
	ed::End();
    // The canvas mouse position is local until End(); tooltip windows need screen space.
    if(HoveredRuleTooltip) ImGui::SetTooltip("Rule node #%d",HoveredRuleTooltip);
	if (stateGraph)
		HandleStateGraphInteraction();
    GraphStyle.End();
	ed::SetCurrentEditor(previous);
}
void FStateMachineEditorPanel::DrawStateGraph(FGraphSession &session)
{
	const bool observed=Observation.Ready && Observation.Compatible;
	int current=observed?Observation.State:-1;
	int activeTransition=observed?Observation.Transition:0;
	std::map<int, FStateBox> boxes;
	auto box = [&](int id, float x, float y, const char *name, bool special) {
		auto node = StateId(id);
		Place(session.Placed, node, x, y);
		auto size = ed::GetNodeSize(node);
		if (size.x <= 0 || size.y <= 0)
			size = CompactSize(name, special);
		boxes[id] = {ed::GetNodePosition(node), size};
	};
	box(EntrySource, Working.EntryX, Working.EntryY, "Entry", true);
	box(AnySource, Working.AnyX, Working.AnyY, "AnyState", true);
	for (auto &state : Working.States)
		box(state.Id, state.X, state.Y, state.Name.c_str(), false);
	auto paths = Connections(Working, boxes);
	ed::PushStyleVar(ed::StyleVar_NodePadding, ImVec4(12, 10, 12, 10));
	auto drawNode = [&](int id, const char *name, const char *type) {
		bool special = id < 0, source = id == PendingSource, target = PendingSource != NoSource && !special;
		ImVec4 background = id == EntrySource
								? ImVec4(.12f, .25f, .32f, 1)
								: (id == AnySource ? ImVec4(.3f, .19f, .1f, 1) : ImVec4(.13f, .15f, .19f, 1));
		if (!special && id == current)
			background = ImVec4(.13f, .36f, .22f, 1);
		ed::PushStyleColor(ed::StyleColor_NodeBg, background);
		if (source || target || (!special && id == current))
			ed::PushStyleColor(ed::StyleColor_NodeBorder, source ? ImVec4(1, .75f, .2f, 1) : ImVec4(.25f, .8f, .7f, 1));
		ed::BeginNode(StateId(id));
		auto origin = ImGui::GetCursorScreenPos(), size = CompactSize(name, special);
		for (const auto &path : paths)
		{
			if (path.From == id)
				HiddenEndpoint(path, 0, origin);
			if (path.To == id)
				HiddenEndpoint(path, 1, origin);
		}
		// Always consume the cursor move with a real layout item before EndNode/EndGroup.
		ImGui::SetCursorScreenPos(origin);
		ImGui::Dummy(ImVec2(size.x - 24, size.y - 20));
		auto *draw = ImGui::GetWindowDrawList();
		draw->AddText(origin, IM_COL32(235, 237, 242, 255), name);
		if (!special)
			draw->AddText(ImGui::GetFont(), ImGui::GetFontSize() * .82f,
						  Add(origin, ImVec2(0, ImGui::GetFontSize() + 4)), IM_COL32(145, 155, 170, 255), (!special && id==current) ? "ACTIVE" : type);

		ed::EndNode();
		if (source || target || (!special && id == current))
			ed::PopStyleColor();
		ed::PopStyleColor();
	};
	drawNode(EntrySource, "Entry", "");
	drawNode(AnySource, "AnyState", "");
	for (auto &s : Working.States)
		drawNode(s.Id, s.Name.c_str(), !s.PoseGraph.Nodes.empty() ? "Pose Graph" : s.BlendSpace ? "BlendSpace" : "Sequence");
	ed::PopStyleVar();
	for (const auto &path : paths)
	{
		bool active = !path.Initial && path.Id == activeTransition;
		bool highlight = ed::IsLinkSelected(path.LinkId()) || ed::GetHoveredLink() == path.LinkId();
		ImVec4 color = active ? ImVec4(.3f, 1, .5f, 1)
							  : (highlight ? ImVec4(1, .78f, .3f, 1)
										   : (path.Initial ? ImVec4(.3f, .7f, 1, 1) : ImVec4(.65f, .72f, .82f, 1)));
		ed::Link(path.LinkId(), path.Endpoint(0), path.Endpoint(1), color, active ? 3.f : 2.f);
		if (active)
			ed::Flow(path.LinkId());
		else
			ed::StopFlow(path.LinkId());
		if (auto *draw = ed::GetNodeBackgroundDrawList(StateId(path.From)))
			DrawDirection(draw, path, ImGui::ColorConvertFloat4ToU32(color), !path.Initial);
	}
	if (QueuedTransition)
	{
		if (FindTransition(QueuedTransition))
		{
			ed::ClearSelection();
			ed::SelectLink(ed::LinkId(Key(TransitionLink, QueuedTransition)));
		}
		QueuedTransition = 0;
	}
	if (PendingSource != NoSource && boxes.contains(PendingSource))
	{
		auto mouse = ImGui::GetMousePos();
		const auto &source = boxes.at(PendingSource);
		FStateConnection preview;
		preview.P0 = Boundary(source, source.Center(), Unit(Sub(mouse, source.Center())));
		preview.P1 = preview.P0;
		preview.P2 = preview.P3 = mouse;
		if (auto *draw = ed::GetNodeBackgroundDrawList(StateId(PendingSource)))
		{
			auto color = IM_COL32(255, 195, 70, 220);
			draw->AddLine(preview.P0, preview.P3, color, 2);
			DrawDirection(draw, preview, color, false);
		}
	}
	auto removeTransition = [&](int id) {
		auto it = Sessions.find(id);
		if (it != Sessions.end())
		{
			ed::DestroyEditor(it->second.Context);
			Sessions.erase(it);
		}
		std::erase_if(Working.Transitions, [&](const auto &t) { return t.Id == id; });
		SelectedTransition = -1;
		if (QueuedTransition == id)
			QueuedTransition = 0;
		Dirty = true;
	};
	if (ed::BeginDelete())
	{
		ed::LinkId link;
		while (ed::QueryDeletedLink(&link))
		{
			auto value = link.Get();
			if (Kind(value) == EntryLink)
			{
				if (ed::AcceptDeletedItem())
				{
					Working.Entry = -1;
					Dirty = true;
				}
			}
			else if (Kind(value) == TransitionLink)
			{
				if (ed::AcceptDeletedItem())
					removeTransition(Object(value));
			}
			else
				ed::RejectDeletedItem();
		}
		ed::NodeId node;
		while (ed::QueryDeletedNode(&node))
		{
			auto value = node.Get();
			if (Kind(value) != StateNode)
			{
				ed::RejectDeletedItem();
				continue;
			}
			if (ed::AcceptDeletedItem())
			{
				int id = Object(value);
				std::vector<int> dependencies;
				for (const auto &t : Working.Transitions)
					if (t.From == id || t.To == id)
						dependencies.push_back(t.Id);
				for (int t : dependencies)
					removeTransition(t);
				std::erase_if(Working.States, [&](const auto &s) { return s.Id == id; });
				session.Placed.erase(value);
				if (Working.Entry == id)
					Working.Entry = -1;
				if (PendingSource == id)
					PendingSource = NoSource;
				Selected = -1;
				Dirty = true;
			}
		}
	}
	ed::EndDelete();
	ed::Suspend();
	ed::NodeId menuNode;
	ed::LinkId menuLink;
	if (ed::ShowNodeContextMenu(&menuNode))
	{
		ContextObject = menuNode.Get();
		ImGui::OpenPopup("State Actions");
	}
	if (ed::ShowLinkContextMenu(&menuLink))
	{
		ContextObject = menuLink.Get();
		ImGui::OpenPopup("State Actions");
	}
	if (ed::ShowBackgroundContextMenu())
	{
		CreationPosition = ed::ScreenToCanvas(ImGui::GetMousePos());
		ImGui::OpenPopup("Create State");
	}
	if (ImGui::BeginPopup("Create State"))
	{
        if(ImGui::IsWindowAppearing()) RuleSearch.clear();
        AnimGraphTextEdit("Search",RuleSearch);
		if (ImGuiTextFilter(RuleSearch.c_str()).PassFilter("State") && (ImGui::MenuItem("State") || ImGui::IsKeyPressed(ImGuiKey_Enter)))
        { AddState(CreationPosition); ImGui::CloseCurrentPopup(); }
		ImGui::EndPopup();
	}
	if (ImGui::BeginPopup("State Actions"))
	{
		int kind = Kind(ContextObject), id = Object(ContextObject);
		bool transition = kind == TransitionLink;
		if ((kind == StateNode || kind == SpecialNode) && ImGui::MenuItem("Make Transition"))
		{
			PendingSource = kind == StateNode ? id : (id == 0 ? EntrySource : AnySource);
			PendingStartFrame = ImGui::GetFrameCount();
		}
		if(kind==StateNode && ImGui::MenuItem("Duplicate","Ctrl+D")) { ed::ClearSelection(); ed::SelectNode(StateId(id)); ClipboardActions(false,false,true); }
        if (transition && ImGui::MenuItem("Edit Transition Rule"))
		{
			if (FindTransition(id))
			{
				ActiveRule = id;
				SelectedRuleNode = 0;
				PendingSource = NoSource;
			}
		}
		if (kind == StateNode && ImGui::MenuItem("Set as Entry"))
		{
			Working.Entry = id;
			Dirty = true;
		}
		if (kind != SpecialNode && ImGui::MenuItem("Delete"))
		{
			if (kind == TransitionLink || kind == EntryLink)
				ed::DeleteLink(ed::LinkId(ContextObject));
			else if (kind == StateNode)
				ed::DeleteNode(ed::NodeId(ContextObject));
		}
		ImGui::EndPopup();
	}
	ed::Resume();
	for (auto &s : Working.States)
		if (session.Placed.contains(Key(StateNode, s.Id)))
			Dirty |= Capture(Node(StateNode, s.Id), s.X, s.Y);
	Dirty |= Capture(Node(SpecialNode, 0), Working.EntryX, Working.EntryY);
	Dirty |= Capture(Node(SpecialNode, 1), Working.AnyX, Working.AnyY);
}
void FStateMachineEditorPanel::HandleStateGraphInteraction()
{
	if (ActiveRule)
	{
		PendingSource = NoSource;
		return;
	}
	if (ed::HasSelectionChanged())
	{
		int total = ed::GetSelectedObjectCount();
		std::vector<ed::NodeId> nodes(total);
		int count = ed::GetSelectedNodes(nodes.data(), total);
		Selected = -1;
		SelectedTransition = -1;
		if (count && Kind(nodes[count - 1].Get()) == StateNode)
			Selected = Object(nodes[count - 1].Get());
		std::vector<ed::LinkId> links(total);
		count = ed::GetSelectedLinks(links.data(), total);
		if (count && Kind(links[count - 1].Get()) == TransitionLink)
		{
			int id = Object(links[count - 1].Get());
			for (int i = 0; i < (int)Working.Transitions.size(); ++i)
				if (Working.Transitions[i].Id == id)
					SelectedTransition = i;
		}
	}
	if (PendingSource >= 0 && !FindState(PendingSource))
		PendingSource = NoSource;
	if (PendingSource != NoSource)
	{
		if (ImGui::IsKeyPressed(ImGuiKey_Escape))
			PendingSource = NoSource;
		else if (ImGui::GetFrameCount() > PendingStartFrame &&
				 !ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel))
		{
			auto hovered = ed::GetHoveredNode();
			if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && Kind(hovered.Get()) == StateNode)
			{
				int target = Object(hovered.Get());
				if (FindState(target))
				{
					if (PendingSource == EntrySource)
					{
						Working.Entry = target;
						Dirty = true;
					}
					else
					{
						AddTransition(PendingSource, target);
						QueuedTransition = Working.Transitions.back().Id;
					}
					PendingSource = NoSource;
				}
			}
			else if (ed::IsBackgroundClicked() && ed::GetBackgroundClickButtonIndex() == ImGuiMouseButton_Left)
				PendingSource = NoSource;
		}
	}
    auto DoubleNode = ed::GetDoubleClickedNode();
    if (Kind(DoubleNode.Get()) == StateNode && FindState(Object(DoubleNode.Get())))
    { ActivePose = Object(DoubleNode.Get()); ActiveRule = 0; PendingSource = NoSource; }
	auto doubleLink = ed::GetDoubleClickedLink();
	if (Kind(doubleLink.Get()) == TransitionLink && FindTransition(Object(doubleLink.Get())))
	{
		ActiveRule = Object(doubleLink.Get());
		SelectedRuleNode = 0;
		PendingSource = NoSource;
	}
	auto hoveredLink = ed::GetHoveredLink();
	if (PendingSource == NoSource && Kind(hoveredLink.Get()) == TransitionLink)
	{
		if (auto *t = FindTransition(Object(hoveredLink.Get())))
		{
			ImGui::BeginTooltip();
			ImGui::PushTextWrapPos(ImGui::GetFontSize() * 28);
			ImGui::Text("%s -> %s", StateName(t->From), StateName(t->To));
			ImGui::TextUnformatted(t->Rule.Summary().c_str());
			ImGui::TextDisabled("Double click to edit the transition rule.");
			ImGui::PopTextWrapPos();
			ImGui::EndTooltip();
		}
	}
}
void FStateMachineEditorPanel::AutoArrange()
{
	if (Working.States.empty())
		return;
	PendingSource = NoSource;
	std::map<int, std::vector<int>> adjacency;
	std::map<int, ImVec2> sizes;
	for (const auto &state : Working.States)
	{
		adjacency[state.Id];
		auto size = ed::GetNodeSize(Node(StateNode, state.Id));
		if (size.x <= 0 || size.y <= 0)
			size = CompactSize(state.Name.c_str(), false);
		sizes[state.Id] = size;
	}
	for (const auto &t : Working.Transitions)
		if (t.From >= 0 && adjacency.contains(t.From) && adjacency.contains(t.To))
		{
			adjacency[t.From].push_back(t.To);
			adjacency[t.To].push_back(t.From);
		}
	for (auto &[id, neighbors] : adjacency)
	{
		std::sort(neighbors.begin(), neighbors.end());
		neighbors.erase(std::unique(neighbors.begin(), neighbors.end()), neighbors.end());
	}
	std::vector<int> roots;
	if (adjacency.contains(Working.Entry))
		roots.push_back(Working.Entry);
	for (const auto &[id, neighbors] : adjacency)
		if (id != Working.Entry)
			roots.push_back(id);
	std::set<int> visited;
	float componentTop = 0;
	for (int root : roots)
	{
		if (visited.contains(root))
			continue;
		std::map<int, std::vector<int>> layers;
		std::queue<std::pair<int, int>> queue;
		queue.push({root, 0});
		visited.insert(root);
		while (!queue.empty())
		{
			auto [id, depth] = queue.front();
			queue.pop();
			layers[depth].push_back(id);
			for (int neighbor : adjacency[id])
				if (visited.insert(neighbor).second)
					queue.push({neighbor, depth + 1});
		}
		float componentHeight = 0;
		std::map<int, float> heights, widths;
		for (auto &[depth, ids] : layers)
		{
			std::sort(ids.begin(), ids.end());
			float height = -64, width = 0;
			for (int id : ids)
			{
				height += sizes[id].y + 64;
				width = std::max(width, sizes[id].x);
			}
			heights[depth] = height;
			widths[depth] = width;
			componentHeight = std::max(componentHeight, height);
		}
		float x = 0;
		for (const auto &[depth, ids] : layers)
		{
			float y = componentTop + (componentHeight - heights[depth]) * .5f;
			for (int id : ids)
			{
				auto *state = FindState(id);
				state->X = x;
				state->Y = y;
				ed::SetNodePosition(Node(StateNode, id), ImVec2(x, y));
				y += sizes[id].y + 64;
			}
			x += widths[depth] + 180;
		}
		componentTop += componentHeight + 160;
	}
	int root = adjacency.contains(Working.Entry) ? Working.Entry : adjacency.begin()->first;
	auto *start = FindState(root);
	auto entrySize = CompactSize("Entry", true), anySize = CompactSize("AnyState", true);
	Working.EntryX = start->X - entrySize.x - 100;
	Working.EntryY = start->Y + (sizes[root].y - entrySize.y) * .5f;
	float left = std::numeric_limits<float>::max(), top = left;
	for (const auto &s : Working.States)
	{
		left = std::min(left, s.X);
		top = std::min(top, s.Y);
	}
	Working.AnyX = left - anySize.x - 100;
	Working.AnyY = top - anySize.y - 100;
	ed::SetNodePosition(Node(SpecialNode, 0), ImVec2(Working.EntryX, Working.EntryY));
	ed::SetNodePosition(Node(SpecialNode, 1), ImVec2(Working.AnyX, Working.AnyY));
	Sessions[0].Fit = true;
	Dirty = true;
}

void FStateMachineEditorPanel::DrawRuleGraph(FGraphSession& session)
{
	auto* transition = FindTransition(ActiveRule);
	if (!transition)
		return;
	auto& graph = transition->Rule;
	FString compileError;
	std::vector<FAnimConditionDefinition> temporary;
	int root = -1, errorNode = 0;
	graph.Compile(Working.Parameters, temporary, root, compileError, &errorNode);
	for (auto& n : graph.Nodes)
	{
		auto id = Node(RuleNode, n.Id);
		Place(session.Placed, id, n.X, n.Y);
		bool invalid = n.Id == errorNode;
		ed::PushStyleColor(ed::StyleColor_NodeBg, ImVec4(.14f,.15f,.16f,1));
		if (invalid)
			ed::PushStyleColor(ed::StyleColor_NodeBorder, ImVec4(1, .25f, .2f, 1));
		ed::BeginNode(id);
		ImGui::PushID(n.Id);
        auto Header=ImGui::GetCursorScreenPos();
        auto HeaderColor=n.Condition.Type==EAnimConditionType::FloatCompare?IM_COL32(53,91,54,255):IM_COL32(120,43,47,255);
        ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(Header.x-5,Header.y-4),ImVec2(Header.x+200,Header.y+23),HeaderColor,4);
        ImGui::TextUnformatted(n.Result ? "Can Enter Transition" : TypeName(n.Condition.Type));
        if(ImGui::IsItemHovered()) HoveredRuleTooltip=n.Id;
        ImGui::Dummy(ImVec2(190,5));
		ImGui::Separator();
		ImGui::PushItemWidth(180);
		if (!n.Result)
			DrawRuleProperties(n, true);
		ImGui::PopItemWidth();
		for (int slot = 0; slot < n.InputCount; ++slot)
		{
			auto label = "Input " + std::to_string(slot + 1);
			PinLabel(Pin(RulePin, n.Id, slot + 1), ed::PinKind::Input, n.Result ? "Result" : label.c_str());
		}
		if (!n.Result)
			PinLabel(Pin(RulePin, n.Id, 0), ed::PinKind::Output, "Bool");
		if (invalid)
			ImGui::TextColored(ImVec4(1, .4f, .3f, 1), "%s", compileError.c_str());
		ImGui::PopID();
		ed::EndNode();
		if (invalid)
			ed::PopStyleColor();
		ed::PopStyleColor();
	}
	for (auto& link : graph.Links)
		ed::Link(ed::LinkId(Key(RuleLink, link.Id)), Pin(RulePin, link.From, 0), Pin(RulePin, link.To, link.Input + 1),
		         ImVec4(.88f,.27f,.32f,1), 2);
	if (ed::HasSelectionChanged())
	{
		std::vector<ed::NodeId> nodes(ed::GetSelectedObjectCount());
		int count = ed::GetSelectedNodes(nodes.data(), (int)nodes.size());
		SelectedRuleNode = count ? Object(nodes[count - 1].Get()) : 0;
	}
	auto connect = [&](int from, int to, int slot)
	{
		if (!graph.CanConnect(from, to, slot))
			return false;
		int id = graph.NextLinkId();
		std::erase_if(graph.Links, [&](auto& l)
		              { return l.To == to && l.Input == slot; });
		graph.Links.push_back({ id, from, to, slot });
		Dirty = true;
		return true;
	};
	if (ed::BeginCreate())
	{
		ed::PinId a, b;
		if (ed::QueryNewLink(&a, &b))
		{
			auto from = a.Get(), to = b.Get();
			if (Slot(from) != 0)
				std::swap(from, to);
			bool valid = Kind(from) == RulePin && Kind(to) == RulePin && Slot(from) == 0 && Slot(to) > 0 &&
			             graph.CanConnect(Object(from), Object(to), Slot(to) - 1);
			if (valid)
			{
				if (ed::AcceptNewItem())
					connect(Object(from), Object(to), Slot(to) - 1);
			}
			else
				ed::RejectNewItem(ImVec4(1, .25f, .2f, 1));
		}
		ed::PinId pin;
		if (ed::QueryNewNode(&pin) && ed::AcceptNewItem())
		{
			CreateFromPin = pin.Get();
			CreationPosition = ed::ScreenToCanvas(ImGui::GetMousePos());
			ed::Suspend();
			RuleSearch.clear(); RuleSearchSelection=0; ImGui::OpenPopup("Create Rule Node");
			ed::Resume();
		}
	}
	// BeginCreate marks the action active even when it returns false.
	ed::EndCreate();
	if (ed::BeginDelete())
	{
		ed::LinkId link;
		while (ed::QueryDeletedLink(&link))
			if (ed::AcceptDeletedItem())
			{
				int id = Object(link.Get());
				std::erase_if(graph.Links, [&](auto& l)
				              { return l.Id == id; });
				Dirty = true;
			}
		ed::NodeId node;
		while (ed::QueryDeletedNode(&node))
		{
			int id = Object(node.Get());
			auto* n = graph.Find(id);
			if (!n || n->Result)
			{
				ed::RejectDeletedItem();
				continue;
			}
			if (ed::AcceptDeletedItem())
			{
				std::erase_if(graph.Links, [&](auto& l)
				              { return l.From == id || l.To == id; });
				std::erase_if(graph.Nodes, [&](auto& x)
				              { return x.Id == id; });
				session.Placed.erase(node.Get());
				if (SelectedRuleNode == id)
					SelectedRuleNode = 0;
				Dirty = true;
			}
		}
	}
	ed::EndDelete();
	ed::Suspend();
	ed::NodeId menuNode;
	ed::LinkId menuLink;
    ed::PinId MenuPin;
    if(ed::ShowPinContextMenu(&MenuPin)) { ContextObject=MenuPin.Get(); ImGui::OpenPopup("Rule Pin Actions"); }
    if(ImGui::BeginPopup("Rule Pin Actions")) { if(ImGui::MenuItem("Break Connection(s)")) { int Id=Object(ContextObject),Input=Slot(ContextObject); std::erase_if(graph.Links,[&](auto& L){return Input==0?L.From==Id:L.To==Id&&L.Input==Input-1;}); Dirty=true; } ImGui::EndPopup(); }
	if (ed::ShowNodeContextMenu(&menuNode))
	{
		ContextObject = menuNode.Get();
		ImGui::OpenPopup("Rule Actions");
	}
	if (ed::ShowLinkContextMenu(&menuLink))
	{
		ContextObject = menuLink.Get();
		ImGui::OpenPopup("Rule Actions");
	}
	if (ed::ShowBackgroundContextMenu())
	{
		CreateFromPin = 0;
		CreationPosition = ed::ScreenToCanvas(ImGui::GetMousePos());
        RuleSearch.clear(); RuleSearchSelection=0;
		ImGui::OpenPopup("Create Rule Node");
	}
	if (ImGui::BeginPopup("Create Rule Node"))
	{
        if(ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
        if(AnimGraphTextEdit("Search",RuleSearch)) RuleSearchSelection=0;
        if(ImGui::IsKeyPressed(ImGuiKey_DownArrow)) ++RuleSearchSelection;
        if(ImGui::IsKeyPressed(ImGuiKey_UpArrow)) RuleSearchSelection=std::max(0,RuleSearchSelection-1);
        ImGuiTextFilter SearchFilter(RuleSearch.c_str());
        int MatchIndex=0;
		for (int value = 0; value <= (int)EAnimConditionType::Not; ++value)
		{
			auto type = (EAnimConditionType)value;
            if(!SearchFilter.PassFilter(TypeName(type))) continue;
			int inputs = (type == EAnimConditionType::And || type == EAnimConditionType::Or)
			                 ? 2
			                 : (type == EAnimConditionType::Not ? 1 : 0);
			bool allowed = !CreateFromPin || Slot(CreateFromPin) != 0 || inputs > 0;
			int ThisIndex=allowed?MatchIndex++:-1;
            if (ImGui::MenuItem(TypeName(type), nullptr, ThisIndex==RuleSearchSelection, allowed) || (allowed && ThisIndex==RuleSearchSelection && ImGui::IsKeyPressed(ImGuiKey_Enter)))
			{
				FAnimRuleNode n;
				n.Id = graph.NextNodeId();
				n.Condition.Type = type;
				n.Condition.BoolValue = type != EAnimConditionType::Always;
				n.InputCount = inputs;
				n.X = CreationPosition.x;
				n.Y = CreationPosition.y;
				if (type == EAnimConditionType::BoolEquals || type == EAnimConditionType::FloatCompare ||
				    type == EAnimConditionType::TriggerActive)
				{
					auto required = type == EAnimConditionType::BoolEquals
					                    ? EAnimParameterType::Bool
					                    : (type == EAnimConditionType::FloatCompare ? EAnimParameterType::Float
					                                                                : EAnimParameterType::Trigger);
					for (auto& p : Working.Parameters)
						if (p.Type == required)
						{
							n.Condition.ParameterName = p.Name;
							break;
						}
				}
				graph.Nodes.push_back(n);
				SelectedRuleNode = n.Id;
                PendingSelection={n.Id};
				if (CreateFromPin)
				{
					if (Slot(CreateFromPin) == 0)
						connect(Object(CreateFromPin), n.Id, 0);
					else
						connect(n.Id, Object(CreateFromPin), Slot(CreateFromPin) - 1);
				}
				CreateFromPin = 0;
                ImGui::CloseCurrentPopup();
				Dirty = true;
			}
		}
        if(MatchIndex>0) RuleSearchSelection=std::min(RuleSearchSelection,MatchIndex-1);
		ImGui::EndPopup();
	}
	if (ImGui::BeginPopup("Rule Actions"))
	{
        if(Kind(ContextObject)==RuleNode)
        {
            auto* Source=graph.Find(Object(ContextObject));
            if(Source && !Source->Result && ImGui::MenuItem("Duplicate","Ctrl+D")) { ed::ClearSelection(); ed::SelectNode(Node(RuleNode,Source->Id)); ClipboardActions(false,false,true); }
        }
		bool canDelete = Kind(ContextObject) == RuleLink;
		auto* n = graph.Find(Object(ContextObject));
		if (n && !n->Result)
			canDelete = true;
		if (ImGui::MenuItem("Delete", nullptr, false, canDelete))
		{
			if (Kind(ContextObject) == RuleLink)
				ed::DeleteLink(ed::LinkId(ContextObject));
			else
				ed::DeleteNode(ed::NodeId(ContextObject));
		}
		ImGui::EndPopup();
	}
	ed::Resume();
    if(PendingFocusRule>0 && graph.Find(PendingFocusRule)) { ed::ClearSelection(); ed::SelectNode(Node(RuleNode,PendingFocusRule)); ed::NavigateToSelection(); PendingFocusRule=0; }
	for (auto& n : graph.Nodes)
		if (session.Placed.contains(Key(RuleNode, n.Id)))
			Dirty |= Capture(Node(RuleNode, n.Id), n.X, n.Y);
}
bool FStateMachineEditorPanel::ParameterCombo(const char* Label, FName& Name, EAnimParameterType Type, bool InCanvas)
{
	bool changed = false;
	bool valid = false;
	for (auto& p : Working.Parameters)
		valid |= p.Name == Name && p.Type == Type;
	if (!valid)
		ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, .4f, .3f, 1));
	RuleCombo(Label, valid ? Name.ToString().c_str() : "Select parameter", InCanvas, [&]()
	{
		for (auto& p : Working.Parameters)
			if (p.Type == Type && ImGui::Selectable(p.Name.ToString().c_str(), p.Name == Name))
			{
				Name = p.Name;
				changed = true;
			}
	});
	if (!valid)
		ImGui::PopStyleColor();
	return changed;
}
void FStateMachineEditorPanel::DrawRuleProperties(FAnimRuleNode& n, bool InCanvas)
{
	if (n.Result)
	{
		ImGui::TextDisabled("Connect a Bool rule to allow this transition.");
		return;
	}
	auto& c = n.Condition;
	auto type = c.Type;
	if (type == EAnimConditionType::BoolEquals || type == EAnimConditionType::FloatCompare ||
	    type == EAnimConditionType::TriggerActive)
	{
		auto required =
		    type == EAnimConditionType::BoolEquals
		        ? EAnimParameterType::Bool
		        : (type == EAnimConditionType::FloatCompare ? EAnimParameterType::Float : EAnimParameterType::Trigger);
		Dirty |= ParameterCombo("Parameter", c.ParameterName, required, InCanvas);
	}
	if (type == EAnimConditionType::Always)
		Dirty |= ImGui::Checkbox("Value", &c.BoolValue);
	if (type == EAnimConditionType::BoolEquals)
		Dirty |= ImGui::Checkbox("Equals true", &c.BoolValue);
	if (type == EAnimConditionType::FloatCompare || type == EAnimConditionType::StateElapsedCompare)
	{
		int op = (int)c.CompareOp;
		const char* operators[] = {"<", "<=", ">", ">=", "Nearly Equal", "Not Nearly Equal"};
		RuleCombo("Compare", op >= 0 && op < 6 ? operators[op] : "Select comparison", InCanvas, [&]()
		{
			for (int i = 0; i < 6; ++i)
				if (ImGui::Selectable(operators[i], op == i))
				{
					c.CompareOp = (EAnimCompareOp)i;
					Dirty = true;
				}
		});
		Dirty |= ImGui::InputFloat("Value", &c.FloatValue);
		if (c.CompareOp == EAnimCompareOp::NearlyEqual || c.CompareOp == EAnimCompareOp::NotNearlyEqual)
			Dirty |= ImGui::InputFloat("Tolerance", &c.Tolerance);
	}
	if (type == EAnimConditionType::And || type == EAnimConditionType::Or)
	{
		if (n.InputCount < 64 && ImGui::SmallButton("+ Input"))
		{
			++n.InputCount;
			Dirty = true;
		}
		ImGui::SameLine();
		ImGui::BeginDisabled(n.InputCount <= 1);
		if (ImGui::SmallButton("- Input"))
		{
			--n.InputCount;
			if (auto* t = FindTransition(ActiveRule))
				std::erase_if(t->Rule.Links, [&](auto& l)
				              { return l.To == n.Id && l.Input >= n.InputCount; });
			Dirty = true;
		}
		ImGui::EndDisabled();
	}
}
FString FStateMachineEditorPanel::ParameterReferences(FName Name) const
{
	FString references;
    for (auto& State : Working.States) for (auto& Node : State.PoseGraph.Nodes)
        if (Node.Parameter == Name.ToString()) references += "\nPose graph: " + State.Name + " / node " + std::to_string(Node.Id);
	for (auto& s : Working.States)
		if (s.BlendParameter == Name.ToString())
			references += "\nBlendSpace: " + s.Name;
	for (auto& t : Working.Transitions)
	{
		bool uses = false;
		for (auto& n : t.Rule.Nodes)
		{
			auto type = n.Condition.Type;
			if (!n.Result &&
			    (type == EAnimConditionType::BoolEquals || type == EAnimConditionType::FloatCompare ||
			     type == EAnimConditionType::TriggerActive) &&
			    n.Condition.ParameterName == Name)
				uses = true;
		}
		for (auto trigger : t.Options.ConsumeTriggers)
			uses |= trigger == Name;
		if (uses)
		{
			FString from = "AnyState", to = "Missing";
			for (auto& s : Working.States)
			{
				if (s.Id == t.From)
					from = s.Name;
				if (s.Id == t.To)
					to = s.Name;
			}
			references += "\nTransition: " + from + " -> " + to;
		}
	}
	return references;
}
void FStateMachineEditorPanel::RenameParameter(FName Old, FName New)
{
    for (auto& State : Working.States) for (auto& Node : State.PoseGraph.Nodes)
        if (Node.Parameter == Old.ToString()) Node.Parameter = New.ToString();
	for (auto& s : Working.States)
		if (s.BlendParameter == Old.ToString())
			s.BlendParameter = New.ToString();
	for (auto& t : Working.Transitions)
	{
		for (auto& n : t.Rule.Nodes)
			if (n.Condition.ParameterName == Old)
				n.Condition.ParameterName = New;
		for (auto& trigger : t.Options.ConsumeTriggers)
			if (trigger == Old)
				trigger = New;
	}
	for (auto& c : Working.Conditions)
		if (c.ParameterName == Old)
			c.ParameterName = New;
}
void FStateMachineEditorPanel::DrawParameters()
{
	if (ImGui::Button("Add Parameter"))
	{
		int number = 1;
		FName name;
		bool exists;
		do
		{
			name = FName(("Parameter_" + std::to_string(number++)).c_str());
			exists = false;
			for (auto& p : Working.Parameters)
				exists |= p.Name == name;
		} while (exists);
		FAnimParameter p;
		p.Name = name;
		Working.Parameters.push_back(p);
		Dirty = true;
	}
	for (int i = 0; i < (int)Working.Parameters.size(); ++i)
	{
		auto& p = Working.Parameters[i];
		ImGui::PushID(i);
		char name[256];
		strncpy_s(name, p.Name.ToString().c_str(), _TRUNCATE);
		if (ImGui::InputText("Name", name, sizeof(name)))
		{
			FName next(name);
			bool duplicate = next.IsNone();
			for (int j = 0; j < (int)Working.Parameters.size(); ++j)
				if (j != i && Working.Parameters[j].Name == next)
					duplicate = true;
			if (duplicate)
				Error = "Choose a unique, nonempty parameter name.";
			else
			{
				RenameParameter(p.Name, next);
				p.Name = next;
				Dirty = true;
			}
		}
		int type = (int)p.Type;
		if (ImGui::Combo("Type", &type, "Bool\0Float\0Trigger\0"))
		{
			FString refs = ParameterReferences(p.Name);
			if (!refs.empty())
				Error = "Parameter is in use. Update these references before changing its type:" + refs;
			else
			{
				p.Type = (EAnimParameterType)type;
				Dirty = true;
			}
		}
		if (p.Type == EAnimParameterType::Bool)
			Dirty |= ImGui::Checkbox("Default", &p.BoolValue);
		if (p.Type == EAnimParameterType::Float)
			Dirty |= ImGui::InputFloat("Default", &p.FloatValue);
		bool remove = ImGui::SmallButton("Remove parameter");
		ImGui::Separator();
		ImGui::PopID();
		if (remove)
		{
			FString refs = ParameterReferences(p.Name);
			if (!refs.empty())
				Error = "Parameter is in use. Update these references before removing it:" + refs;
			else
			{
				Working.Parameters.erase(Working.Parameters.begin() + i);
				Dirty = true;
				break;
			}
		}
	}
	ImGui::TextDisabled("Lua parameter names in script files must be updated manually after renaming.");
}
void FStateMachineEditorPanel::DrawRuleErrors()
{
	bool any = false;
	for (auto& t : Working.Transitions)
	{
		std::vector<FAnimConditionDefinition> conditions;
		int root = -1, bad = 0;
		FString error;
		if (!t.Rule.Compile(Working.Parameters, conditions, root, error, &bad))
		{
			if (!any)
			{
				ImGui::SeparatorText("Rule Errors");
				any = true;
			}
			FString title = FString(StateName(t.From)) + " -> " + StateName(t.To);
			ImGui::PushID(t.Id);
			if (ImGui::Selectable(title.c_str(), ActiveRule == t.Id))
			{
				ActiveRule = t.Id;
				SelectedRuleNode = bad;
				auto& session = GraphSession();
				auto* previous = ed::GetCurrentEditor();
				ed::SetCurrentEditor(session.Context);
				ed::ClearSelection();
				if (bad)
					ed::SelectNode(Node(RuleNode, bad));
				session.Fit = true;
				ed::SetCurrentEditor(previous);
			}
			ImGui::TextColored(ImVec4(1, .4f, .3f, 1), "%s", error.c_str());
			ImGui::PopID();
		}
	}
}
void FStateMachineEditorPanel::ClipboardActions(bool Copy,bool Paste,bool Duplicate)
{
    if(!Copy&&!Paste&&!Duplicate) return;
    static std::vector<FAnimAssetState> StateClipboard;
    static std::vector<FAnimAssetTransition> TransitionClipboard;
    static FAnimRuleGraph RuleClipboard;
    auto* Transition=FindTransition(ActiveRule);
    if(Transition)
    {
        auto& Graph=Transition->Rule;
        if(Copy||Duplicate)
        {
            RuleClipboard={}; std::set<int> Ids;
            std::vector<ed::NodeId> Nodes(ed::GetSelectedObjectCount()); int Count=ed::GetSelectedNodes(Nodes.data(),(int)Nodes.size());
            for(int I=0;I<Count;++I) if(Kind(Nodes[I].Get())==RuleNode) if(auto* N=Graph.Find(Object(Nodes[I].Get()));N&&!N->Result) { RuleClipboard.Nodes.push_back(*N); Ids.insert(N->Id); }
            for(auto& L:Graph.Links) if(Ids.count(L.From)&&Ids.count(L.To)) RuleClipboard.Links.push_back(L);
        }
        if((Paste||Duplicate)&&!RuleClipboard.Nodes.empty())
        {
            std::map<int,int> Remap; int Id=Graph.NextNodeId(); ed::ClearSelection();
            for(auto N:RuleClipboard.Nodes) { int Old=N.Id; N.Id=Id++; N.X+=40; N.Y+=40; Remap[Old]=N.Id; Graph.Nodes.push_back(N); PendingSelection.push_back(N.Id); SelectedRuleNode=N.Id; }
            for(auto L:RuleClipboard.Links) { L.Id=Graph.NextLinkId(); L.From=Remap[L.From]; L.To=Remap[L.To]; Graph.Links.push_back(L); }
            Dirty=true;
        }
        return;
    }
    if(Copy||Duplicate)
    {
        StateClipboard.clear(); TransitionClipboard.clear(); std::set<int> Ids;
        std::vector<ed::NodeId> Nodes(ed::GetSelectedObjectCount()); int Count=ed::GetSelectedNodes(Nodes.data(),(int)Nodes.size());
        for(int I=0;I<Count;++I) if(Kind(Nodes[I].Get())==StateNode) if(auto* S=FindState(Object(Nodes[I].Get()))) { StateClipboard.push_back(*S); Ids.insert(S->Id); }
        for(auto& T:Working.Transitions) if(Ids.count(T.From)&&Ids.count(T.To)) TransitionClipboard.push_back(T);
    }
    if((Paste||Duplicate)&&!StateClipboard.empty())
    {
        std::map<int,int> Remap; ed::ClearSelection();
        for(auto S:StateClipboard)
        {
            int Old=S.Id; S.Id=NextId++; S.X+=40; S.Y+=40;
            auto Base=S.Name+"_Copy"; S.Name=Base; int Suffix=2;
            while(std::any_of(Working.States.begin(),Working.States.end(),[&](auto& V){return V.Name==S.Name;})) S.Name=Base+std::to_string(Suffix++);
            Remap[Old]=S.Id; Working.States.push_back(S); PendingSelection.push_back(S.Id); Selected=S.Id;
        }
        int Next=1; for(auto& T:Working.Transitions) Next=std::max(Next,T.Id+1);
        for(auto T:TransitionClipboard) { T.Id=Next++; T.From=Remap[T.From]; T.To=Remap[T.To]; T.X+=40; T.Y+=40; Working.Transitions.push_back(T); }
        Dirty=true;
    }
}
