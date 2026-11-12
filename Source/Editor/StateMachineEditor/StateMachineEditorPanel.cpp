#include "EnginePCH.h"
#include "StateMachineEditorPanel.h"
#include "Editor/AnimGraphEditor/AnimGraphEditorPanel.h"
#include "Animation/AnimSequence.h"
#include "Animation/AssetAnimInstance.h"
#include "Animation/BlendSpace.h"
#include "Asset/AssetManager.h"
#include "Core/Misc/FileHelper.h"
#include "Editor/SkeletalMeshViewer/SkeletalMeshViewerPanel.h"
#include "Render/Mesh.h"
#include <algorithm>
#include <cmath>
#include "imgui_node_editor.h"

namespace
{
bool TextEdit(const char *Label, FString &Value)
{
	char buffer[1024];
	strncpy_s(buffer, Value.c_str(), _TRUNCATE);
	if (ImGui::InputText(Label, buffer, sizeof(buffer)))
	{
		Value = buffer;
		return true;
	}
	return false;
}
bool EnumEdit(const char *Label, int &Value, const char *Choices)
{
	return ImGui::Combo(Label, &Value, Choices);
}
} // namespace
FStateMachineEditorPanel::~FStateMachineEditorPanel()
{
	ClearContexts();
}
FAnimAssetTransition *FStateMachineEditorPanel::FindTransition(int Id)
{
	for (auto &t : Working.Transitions)
		if (t.Id == Id)
			return &t;
	return nullptr;
}
FAnimAssetState *FStateMachineEditorPanel::FindState(int Id)
{
	for (auto &s : Working.States)
		if (s.Id == Id)
			return &s;
	return nullptr;
}
const char *FStateMachineEditorPanel::StateName(int Id)
{
	auto *s = FindState(Id);
	return s ? s->Name.c_str() : (Id == -1 ? "AnyState" : "Missing");
}
bool FStateMachineEditorPanel::StateCombo(const char *Label, int &Id, bool Any)
{
	bool changed = false;
	if (ImGui::BeginCombo(Label, StateName(Id)))
	{
		if (Any && ImGui::Selectable("AnyState", Id == -1))
		{
			Id = -1;
			changed = true;
		}
		for (auto &s : Working.States)
			if (ImGui::Selectable(s.Name.c_str(), s.Id == Id))
			{
				Id = s.Id;
				changed = true;
			}
		ImGui::EndCombo();
	}
	return changed;
}
void FStateMachineEditorPanel::SetOpen(bool Open)
{
    if(Open && OpenHost) { OpenHost(); return; }
	if (!Open && Dirty)
	{
		PendingClose = true;
		return;
	}
	if (!Open && Viewer)
		Viewer->StopStateMachinePreview();
	if (!Open)
		ClearContexts();
	IEditorPanel::SetOpen(Open);
}
void FStateMachineEditorPanel::ReleaseResources()
{
	if (Viewer)
		Viewer->StopStateMachinePreview();
	ClearContexts();
	Working = {};
	Dirty = false;
	PIEFrame={}; Observation={}; ObservedActor=ObservedComponent=0; ObservedSession=0; ObservePIE=false;
}
void FStateMachineEditorPanel::OpenAsset(const FString &P)
{
	PendingPath = P;
	PendingFolder.clear();
	IEditorPanel::SetOpen(true);
	if (!Dirty)
		LoadPending();
}
void FStateMachineEditorPanel::NewAsset(const FString &Folder)
{
	PendingPath.clear();
	PendingFolder = Folder;
	IEditorPanel::SetOpen(true);
	if (!Dirty)
		LoadPending();
}
void FStateMachineEditorPanel::LoadPending()
{
	if (!PendingPath.empty())
	{
		FString error;
		auto *a = UAssetManager::Get().LoadAnimStateMachine(PendingPath, error);
		if (!a)
		{
			Error = error;
			PendingPath.clear();
			return;
		}
		if (Viewer)
			Viewer->StopStateMachinePreview();
		Working = a->Data;
		Path = PendingPath;
		PendingPath.clear();
		Dirty = false;
	}
	else if (!PendingFolder.empty())
	{
		if (Viewer)
			Viewer->StopStateMachinePreview();
		Working = {};
		Path.clear();
		auto p = FFileHelper::ToUtf8Path(FFileHelper::ToNativePath(PendingFolder) / "NewStateMachine.animsm");
		strncpy_s(SavePath, p.c_str(), _TRUNCATE);
		PendingFolder.clear();
		Dirty = true;
	}
	Selected = SelectedTransition = -1;
	ActiveRule = SelectedRuleNode = 0;
	ClearContexts();
	Working.EnsureRuleGraphs();
	NextId = 1;
	for (auto &s : Working.States)
		NextId = std::max(NextId, s.Id + 1);

	Error.clear();
}
bool FStateMachineEditorPanel::Save()
{
	if (!Working.CompileRules(Error))
		return false;
	FString target = Path.empty() ? FString(SavePath) : Path;
	if (!UAssetManager::Get().SaveAnimStateMachine(target, Working, Error, Path.empty()))
		return false;
	Path = target;
    ReloadAnimationAssetInstances(Path);
	Dirty = false;
	return true;
}
void FStateMachineEditorPanel::AddState(ImVec2 Position)
{
	FAnimAssetState s;
	s.Id = NextId++;
	s.Name = "State_" + std::to_string(s.Id);
	s.X = Position.x;
	s.Y = Position.y;
	auto *mesh = UAssetManager::GetAssetByPath<USkeletalMesh>(Working.MeshKey);
	if (mesh)
	{
		auto assets = UAssetManager::Get().GetAnimationAssets(mesh);
		if (assets.Num())
			s.AssetKey = assets[0]->GetPath();
	}
	s.PoseGraph=FAnimPoseGraph::SinglePlayer(s.AssetKey,s.BlendSpace,s.BlendParameter,s.Options.bLoop,s.Options.bPlaying,s.Options.PlayRate);
	Working.States.push_back(s);
	Selected = s.Id;
	SelectedTransition = -1;
	if (Working.Entry == -1)
		Working.Entry = s.Id;
	Dirty = true;
}
FAnimPoseGraphEditor& FStateMachineEditorPanel::PoseEditor()
{
    auto& Editor = PoseEditors[ActivePose];
    if (!Editor) Editor = std::make_unique<FAnimPoseGraphEditor>();
    if(!Path.empty()) Editor->SetSettingsKey(Path+"/State/"+std::to_string(ActivePose));
    return *Editor;
}
void FStateMachineEditorPanel::DrawDetails()
{
    if (auto* State = FindState(ActivePose))
    {
        Dirty |= PoseEditor().DrawProperties(State->PoseGraph, Working.Parameters, Working.MeshKey);
        return;
    }
	if (auto *s = FindState(Selected))
	{
		ImGui::TextUnformatted("State");
		Dirty |= TextEdit("Name", s->Name);
		if (ImGui::Button("Set as Entry"))
		{
			Working.Entry = s->Id;
			Dirty = true;
		}
        if (ImGui::Button("Edit Pose Graph")) { ActivePose = s->Id; ActiveRule = 0; }
        if (!s->PoseGraph.Nodes.empty())
        {
            ImGui::TextWrapped("This state uses a pose graph. Double click the state to edit its nodes.");
            return;
        }
		if (ImGui::Checkbox("Use BlendSpace", &s->BlendSpace))
		{
			s->AssetKey.clear();
			Dirty = true;
		}
		auto *mesh = UAssetManager::GetAssetByPath<USkeletalMesh>(Working.MeshKey);
		if (ImGui::BeginCombo("Animation", s->AssetKey.c_str()))
		{
			if (s->BlendSpace)
			{
				for (auto *a : UAssetManager::Get().GetBlendSpaceAssets(mesh))
					if (ImGui::Selectable(a->GetPath().c_str(), a->GetPath() == s->AssetKey))
					{
						s->AssetKey = a->GetPath();
						Dirty = true;
					}
			}
			else
			{
				for (auto *a : UAssetManager::Get().GetAnimationAssets(mesh))
					if (ImGui::Selectable(a->GetPath().c_str(), a->GetPath() == s->AssetKey))
					{
						s->AssetKey = a->GetPath();
						Dirty = true;
					}
			}
			ImGui::EndCombo();
		}
		Dirty |= ImGui::Checkbox("Loop", &s->Options.bLoop);
		Dirty |= ImGui::Checkbox("Playing", &s->Options.bPlaying);
		Dirty |= ImGui::InputFloat("Play rate", &s->Options.PlayRate, 0.1f, 1.0f);
		if (ImGui::IsItemHovered())
			ImGui::SetTooltip("Negative values play in reverse; zero pauses playback.");
		if (s->BlendSpace && ImGui::BeginCombo("Axis parameter",
											   s->BlendParameter.empty() ? "Constant zero" : s->BlendParameter.c_str()))
		{
			if (ImGui::Selectable("Constant zero", s->BlendParameter.empty()))
			{
				s->BlendParameter.clear();
				Dirty = true;
			}
			for (auto &p : Working.Parameters)
				if (p.Type == EAnimParameterType::Float &&
					ImGui::Selectable(p.Name.ToString().c_str(), s->BlendParameter == p.Name.ToString()))
				{
					s->BlendParameter = p.Name.ToString();
					Dirty = true;
				}
			ImGui::EndCombo();
		}
	}
	else if (SelectedTransition >= 0 && SelectedTransition < (int)Working.Transitions.size())
	{
		auto &t = Working.Transitions[SelectedTransition];
		auto &o = t.Options;
		ImGui::TextUnformatted("Transition");
		Dirty |= StateCombo("From", t.From, true);
		Dirty |= StateCombo("To", t.To);
		ImGui::TextWrapped("Rule: %s", t.Rule.Summary().c_str());
		if (ImGui::Button("Edit Transition Rule"))
		{
			ActiveRule = t.Id;
			SelectedRuleNode = 0;
		}
		Dirty |= ImGui::InputInt("Priority", &o.Priority);
		Dirty |= ImGui::InputFloat("Blend seconds", &o.Blend.Duration);
		int curve = (int)o.Blend.Curve;
		if (EnumEdit("Curve", curve, "Linear\0SmoothStep\0"))
		{
			o.Blend.Curve = (EAnimBlendCurve)curve;
			Dirty = true;
		}
		int policy = (int)o.InterruptPolicy;
		if (EnumEdit("Interrupt", policy, "Never\0HigherPriorityOnly\0Always\0"))
		{
			o.InterruptPolicy = (EAnimTransitionInterruptPolicy)policy;
			Dirty = true;
		}
		Dirty |= ImGui::Checkbox("Restart same state", &o.bRestartIfSameState);
		ImGui::TextUnformatted("Consume triggers");
		for (auto &p : Working.Parameters)
			if (p.Type == EAnimParameterType::Trigger)
			{
				bool enabled = false;
				for (auto n : o.ConsumeTriggers)
					enabled |= n == p.Name;
				if (ImGui::Checkbox(p.Name.ToString().c_str(), &enabled))
				{
					if (enabled)
						o.ConsumeTriggers.Add(p.Name);
					else
					{
						TArray<FName> next;
						for (auto n : o.ConsumeTriggers)
							if (n != p.Name)
								next.Add(n);
						o.ConsumeTriggers = std::move(next);
					}
					Dirty = true;
				}
			}
	}
	else
		ImGui::TextDisabled("Select a state or transition.");
}
void FStateMachineEditorPanel::DrawPreview()
{
	if(ObservePIE){ImGui::TextDisabled("PIE observation is read-only. Select Preview to use preview controls.");return;}
	if (ImGui::Button("Apply to 3D Preview") && Viewer)
	{
		if (Working.CompileRules(Error) && Working.Validate(Error))
		{
			Viewer->PreviewStateMachine(Working, Error);
		}
	}
	auto *instance = Viewer ? Viewer->GetStateMachinePreview() : nullptr;
	if (!instance)
		return;
	ImGui::SameLine();
	if (ImGui::Button(instance->IsPaused() ? "Play" : "Pause"))
		instance->SetPaused(!instance->IsPaused());
	ImGui::SameLine();
	if (ImGui::Button("Reset"))
	{
		auto data = instance->GetAssetData();
		instance->Configure(data, false);
	}
	ImGui::Text("Current state: %s", StateName(instance->GetCurrentAssetState()));
	if (instance->IsTransitioning())
	{
		int idx = instance->GetActiveTransitionIndex();
		const auto &data = instance->GetAssetData();
		if (idx >= 0 && idx < (int)data.Transitions.size())
		{
			const auto &t = data.Transitions[idx];
			ImGui::Text("Transition: %s -> %s", StateName(t.From), StateName(t.To));
		}
		ImGui::ProgressBar(instance->GetTransitionProgress());
	}
	for (auto &p : instance->GetAssetData().Parameters)
	{
		ImGui::PushID(p.Name.ToString().c_str());
		auto name = p.Name.ToString();
		if (p.Type == EAnimParameterType::Float)
		{
			float v = 0;
			instance->TryGetFloat(name, v);
			if (ImGui::InputFloat(name.c_str(), &v))
				instance->SetFloat(name, v);
		}
		else if (p.Type == EAnimParameterType::Bool)
		{
			bool v = false;
			instance->TryGetBool(name, v);
			if (ImGui::Checkbox(name.c_str(), &v))
				instance->SetBool(name, v);
		}
		else if (ImGui::Button(name.c_str()))
			instance->SetTrigger(name);
		ImGui::PopID();
	}
	ImGui::TextDisabled("Manual parameters only; Lua update is disabled in preview.");
}
void FStateMachineEditorPanel::OnRender()
{
	if (!IsOpen())
		return;
	UpdateObservations();
	ImGuiWindowClass AssetEditorClass;
	ImGui::SetNextWindowClass(&AssetEditorClass);
	bool open = true;
	ImGui::SetNextWindowSize(ImVec2(1100, 750), ImGuiCond_FirstUseEver);
	bool visible = ImGui::Begin(GetPanelName(), &open, ImGuiWindowFlags_NoCollapse);
	Focused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
	if (!open)
		SetOpen(false);
	if (visible && IsOpen())
	{
		DrawObservationControls();
		if (ImGui::Button("Save"))
			Save();
		ImGui::SameLine();
		ImGui::Text("%s%s", Path.empty() ? "New state machine" : Path.c_str(), Dirty ? " *" : "");
		if (Path.empty())
			ImGui::InputText("Save path", SavePath, sizeof(SavePath));
		if (ImGui::BeginCombo("Skeletal Mesh", Working.MeshKey.c_str()))
		{
			for (auto *m : UAssetManager::Get().GetSkeletalMeshAssets())
				if (ImGui::Selectable(m->GetPath().c_str(), Working.MeshKey == m->GetPath()))
				{
					Working.MeshKey = m->GetPath();
					Dirty = true;
				}
			ImGui::EndCombo();
		}
		Dirty |= TextEdit("Optional update script", Working.UpdateScript);
		if (!Error.empty())
			ImGui::TextWrapped("%s", Error.c_str());
		if (ImGui::BeginTable("Layout", 2, ImGuiTableFlags_Resizable))
		{
			ImGui::TableSetupColumn("Graph", ImGuiTableColumnFlags_WidthStretch, 2);
			ImGui::TableSetupColumn("Properties", ImGuiTableColumnFlags_WidthStretch, 1);
			ImGui::TableNextColumn();
			DrawGraph();
			ImGui::TableNextColumn();
			if (ImGui::BeginChild("Properties"))
			{
				if (ImGui::CollapsingHeader("Selection", ImGuiTreeNodeFlags_DefaultOpen))
				{
					if (ActiveRule)
					{
						auto *t = FindTransition(ActiveRule);
						if (t)
						{
							if (auto *n = t->Rule.Find(SelectedRuleNode))
								DrawRuleProperties(*n);
							else
								ImGui::TextDisabled("Select a condition node.");
						}
					}
					else
						DrawDetails();
				}
				if (ImGui::CollapsingHeader("Parameters"))
				{
					ImGui::PushID("Parameters");
					DrawParameters();
					ImGui::PopID();
				}
				DrawRuleErrors();
				if (ImGui::CollapsingHeader("Preview", ImGuiTreeNodeFlags_DefaultOpen))
					DrawPreview();
			}
			ImGui::EndChild();
			ImGui::EndTable();
		}

		if (Focused && ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_S))
			Save();
	}
	if (Dirty && (PendingClose || !PendingPath.empty() || !PendingFolder.empty()))
		ImGui::OpenPopup("Unsaved state machine");
	if (ImGui::BeginPopupModal("Unsaved state machine", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
	{
		ImGui::TextUnformatted("Save changes before continuing?");
		auto finish = [&]() {
			if (PendingClose)
			{
				PendingClose = false;
				IEditorPanel::SetOpen(false);
				ClearContexts();
				if (Viewer)
					Viewer->StopStateMachinePreview();
			}
			else
				LoadPending();
			ImGui::CloseCurrentPopup();
		};
		if (ImGui::Button("Save and continue"))
		{
			if (Save())
				finish();
		}
		ImGui::SameLine();
		if (ImGui::Button("Discard"))
		{
			Dirty = false;
			finish();
		}
		ImGui::SameLine();
		if (ImGui::Button("Cancel"))
		{
			PendingClose = false;
			PendingPath.clear();
			PendingFolder.clear();
			ImGui::CloseCurrentPopup();
		}
		if (!Error.empty())
			ImGui::TextWrapped("%s", Error.c_str());
		ImGui::EndPopup();
	}
	ImGui::End();
}
void FStateMachineEditorPanel::UpdateObservations()
{
	ExpectedSignature.clear();
	ObservationDefinitionError.clear();
	StateMachineDefinitionSignature(Working, ExpectedSignature, ObservationDefinitionError);
	FString key = StateMachineAssetKey(Path);
	if (key != ObservedAssetKey)
	{
		ObservedAssetKey = key;
		ObservedActor = ObservedComponent = 0;
	}
	PIEFrame = PIEProvider ? PIEProvider(key, ExpectedSignature) : FStateMachinePIEFrame{};
	if (PIEFrame.Session != ObservedSession)
	{
		ObservedSession = PIEFrame.Session;
		ObservedActor = ObservedComponent = 0;
		ObservePIE = ObservedSession != 0;
	}
	bool found = false;
	for (const auto &target : PIEFrame.Targets)
		found |= target.ActorId == ObservedActor && target.ComponentId == ObservedComponent;
	if (!found)
	{
		ObservedActor = ObservedComponent = 0;
		if (PIEFrame.Targets.size() == 1)
		{
			ObservedActor = PIEFrame.Targets[0].ActorId;
			ObservedComponent = PIEFrame.Targets[0].ComponentId;
		}
	}
	ResolveObservation();
}
void FStateMachineEditorPanel::ResolveObservation()
{
	Observation = {};
	ObservationMessage.clear();
	if (!ObservationDefinitionError.empty())
	{
		ObservationMessage = "Edited graph is invalid: " + ObservationDefinitionError;
		return;
	}
	if (ObservePIE)
	{
		if (!PIEFrame.Session)
		{
			ObservationMessage = "PIE is not running.";
			return;
		}
		if (ObservedAssetKey.empty())
		{
			ObservationMessage = "Save the asset before choosing a PIE instance.";
			return;
		}
		if (PIEFrame.Targets.empty())
		{
			ObservationMessage = "No PIE instance uses this state machine asset.";
			return;
		}
		for (const auto &target : PIEFrame.Targets)
			if (target.ActorId == ObservedActor && target.ComponentId == ObservedComponent)
			{
				Observation = target.Observation;
				ObservationMessage = Observation.Message;
				return;
			}
		ObservationMessage = "Choose a PIE actor / mesh component to observe.";
	}
	else
	{
		auto *instance = Viewer ? Viewer->GetStateMachinePreview() : nullptr;
		if (!instance)
		{
			ObservationMessage = "Apply this graph to 3D Preview to observe playback.";
			return;
		}
		Observation = ObserveStateMachine(*instance, ExpectedSignature);
		ObservationMessage = Observation.Message;
	}
}
void FStateMachineEditorPanel::DrawObservationControls()
{
	int source = ObservePIE ? 1 : 0;
	ImGui::SetNextItemWidth(140);
	if (ImGui::Combo("Observe", &source, "Preview\0PIE\0"))
	{
		ObservePIE = source == 1;
		ResolveObservation();
	}
	if (ObservePIE)
	{
		const FStateMachinePIETarget *selected = nullptr;
		for (const auto &target : PIEFrame.Targets)
			if (target.ActorId == ObservedActor && target.ComponentId == ObservedComponent)
				selected = &target;
		ImGui::SameLine();
		ImGui::SetNextItemWidth(330);
		if (ImGui::BeginCombo("PIE Target", selected ? selected->Label.c_str() : "Select actor / mesh component"))
		{
			if (PIEFrame.Targets.size() != 1 && ImGui::Selectable("None", !selected))
			{
				ObservedActor = ObservedComponent = 0;
				ResolveObservation();
			}
			for (const auto &target : PIEFrame.Targets)
			{
				ImGui::PushID((int)target.ActorId);
				ImGui::PushID((int)target.ComponentId);
				if (ImGui::Selectable(target.Label.c_str(), selected == &target))
				{
					ObservedActor = target.ActorId;
					ObservedComponent = target.ComponentId;
					ResolveObservation();
				}
				ImGui::PopID();
				ImGui::PopID();
			}
			ImGui::EndCombo();
		}
		ImGui::TextDisabled("PIE observations are read-only.");
	}
	DrawObservationStatus();
	ImGui::Separator();
}
void FStateMachineEditorPanel::DrawObservationStatus()
{
	if (!ObservationMessage.empty())
	{
		ImGui::TextWrapped("%s", ObservationMessage.c_str());
		return;
	}
	if (!Observation.Ready || !Observation.Compatible)
		return;
	ImGui::TextColored(ImVec4(.35f, 1, .55f, 1), "Current state: %s", Observation.StateName.c_str());
	if (Observation.Transition)
	{
		ImGui::Text("Transition: %s -> %s", Observation.FromName.c_str(), Observation.ToName.c_str());
		ImGui::SetNextItemWidth(250);
		ImGui::ProgressBar(Observation.Progress, ImVec2(250, 0));
	}
}
