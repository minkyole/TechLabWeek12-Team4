#include "EnginePCH.h"
#include "Editor/BlendSpaceEditor/BlendSpaceEditorPanel.h"
#include "Asset/AssetManager.h"
#include "Animation/AnimSequence.h"
#include "Render/Mesh.h"
#include "UObject/UObjectGlobals.h"
#include <algorithm>
#include <cmath>

namespace
{
	constexpr const char* AnimationPayload = "BLENDSPACE_ANIMATION";
	void CopyText(char* Buffer, size_t Size, const FString& Text) { strncpy_s(Buffer, Size, Text.c_str(), _TRUNCATE); }
	FString AnimationLabel(const UAnimSequence* Animation)
	{
		if (!Animation) return "None";
		const FString& Key = Animation->GetPath();
		const size_t Slash = Key.find_last_of('/');
		// Keep the source FBX visible alongside the named animation sub-asset.
		const size_t Marker = Key.find("#Anim/");
		if (Marker != FString::npos)
		{
			const size_t SourceSlash = Key.rfind('/', Marker);
			return Key.substr(SourceSlash == FString::npos ? 0 : SourceSlash + 1);
		}
		return Key.substr(Slash == FString::npos ? 0 : Slash + 1);
	}
}

FBlendSpaceEditorPanel::FBlendSpaceEditorPanel() = default;
FBlendSpaceEditorPanel::~FBlendSpaceEditorPanel()
{
	ReleaseResources();
}

void FBlendSpaceEditorPanel::ReleaseResources()
{
	// NewObject registers the draft in GUObjectArray. Delete it while the
	// UObject registry is alive, before DestroyAllObjectsExceptEngine runs.
	Working.reset();
	Rows.clear();
	SelectedId = DragId = 0;
	Dirty = PendingClose = GraphFocused = KeyboardFocused = false;
	Path.clear();
	PendingPath.clear();
	IEditorPanel::SetOpen(false);
}

void FBlendSpaceEditorPanel::SetOpen(bool Open)
{
	if (!Open && Dirty) PendingClose = true;
	else IEditorPanel::SetOpen(Open);
}

void FBlendSpaceEditorPanel::OpenAsset(const FString& InPath)
{
	SetOpen(true);
	if (InPath == Path && Working) return;
	PendingPath = InPath;
	PendingClose = false;
	if (!Dirty) LoadPending();
}

void FBlendSpaceEditorPanel::LoadPending()
{
	auto* Asset = UAssetManager::Get().LoadBlendSpace(PendingPath, Error);
	if (!Asset) { PendingPath.clear(); return; }
	Working.reset(NewObject<UBlendSpace1D>());
	Working->CopyDefinitionFrom(*Asset);
	Path = PendingPath;
	PendingPath.clear();
	Rows.clear();
	SelectedId = DragId = 0;
	for (const FBlendSample& Sample : Working->GetSamples()) Rows.push_back({NextId++, Sample});
	const FBlendParameter& Axis = *Working->GetBlendParameter(0);
	CopyText(AxisName, sizeof(AxisName), Axis.Name.ToString());
	AxisMin = Axis.Min; AxisMax = Axis.Max;
	Dirty = false;
	GraphFocused = false;
}

bool FBlendSpaceEditorPanel::Save()
{
	if (!Working) return false;
	if (!UAssetManager::Get().SaveBlendSpace(Path, *Working, Error)) return false;
	Dirty = false;
	return true;
}

int FBlendSpaceEditorPanel::SelectedIndex() const
{
	for (int I = 0; I < static_cast<int>(Rows.size()); ++I) if (Rows[I].Id == SelectedId) return I;
	return -1;
}

void FBlendSpaceEditorPanel::Select(int Id)
{
	SelectedId = Id;
	const int Index = SelectedIndex();
	if (Index >= 0) EditX = Rows[Index].Sample.SampleValue.X;
}

bool FBlendSpaceEditorPanel::CommitRows(std::vector<FSampleRow> NewRows)
{
	std::unique_ptr<UBlendSpace1D> Candidate(NewObject<UBlendSpace1D>());
	Candidate->SetSkeletalMesh(Working->GetSkeletalMesh());
	Candidate->SetBlendParameter(0, *Working->GetBlendParameter(0));
	Candidate->NotifyTriggerMode = Working->NotifyTriggerMode;
	for (const auto& Row : NewRows)
	{
		if (!Candidate->AddSample(Row.Sample))
		{
			Error = "Invalid sample: check duplicate positions, axis range, mesh compatibility and animation duration.";
			Select(SelectedId);
			return false;
		}
	}
	std::sort(NewRows.begin(), NewRows.end(), [](const auto& A, const auto& B) { return A.Sample.SampleValue.X < B.Sample.SampleValue.X; });
	Rows = std::move(NewRows);
	Working->CopyDefinitionFrom(*Candidate);
	Dirty = true;
	Error.clear();
	Select(SelectedId);
	return true;
}

void FBlendSpaceEditorPanel::AddAnimation(UAnimSequence* Animation, float X)
{
	auto NewRows = Rows;
	const int Id = NextId++;
	FBlendSample Sample;
	Sample.Animation = Animation;
	Sample.SampleValue = FVector(X, 0, 0);
	NewRows.push_back({Id, Sample});
	if (CommitRows(std::move(NewRows))) Select(Id);
}

void FBlendSpaceEditorPanel::DrawAssetDetails()
{
	ImGui::SeparatorText("Asset Details");
	ImGui::TextDisabled("Skeletal Mesh");
	ImGui::TextWrapped("%s", Working->GetSkeletalMesh()->GetPath().c_str());
	if (ImGui::CollapsingHeader("Axis Settings", ImGuiTreeNodeFlags_DefaultOpen))
	{
		if (ImGui::TreeNodeEx("Horizontal Axis", ImGuiTreeNodeFlags_DefaultOpen))
		{
			bool Apply = ImGui::InputText("Name", AxisName, sizeof(AxisName), ImGuiInputTextFlags_EnterReturnsTrue);
			Apply |= ImGui::IsItemDeactivatedAfterEdit();
			ImGui::InputFloat("Minimum", &AxisMin); Apply |= ImGui::IsItemDeactivatedAfterEdit();
			ImGui::InputFloat("Maximum", &AxisMax); Apply |= ImGui::IsItemDeactivatedAfterEdit();
			if (Apply)
			{
				FBlendParameter Axis;
				Axis.Name = FName(AxisName); Axis.Min = AxisMin; Axis.Max = AxisMax;
				if (Working->SetBlendParameter(0, Axis)) { Dirty = true; Error.clear(); }
				else
				{
					Error = "Invalid axis range, or an existing sample would fall outside the new range.";
					const auto& Old = *Working->GetBlendParameter(0);
					CopyText(AxisName, sizeof(AxisName), Old.Name.ToString()); AxisMin = Old.Min; AxisMax = Old.Max;
				}
			}
			ImGui::TreePop();
		}
	}
	if (ImGui::CollapsingHeader("Animation Notify", ImGuiTreeNodeFlags_DefaultOpen))
	{
		static constexpr const char* ModeLabels[] = { "All Animations", "Highest Weighted Animation", "None" };
		static constexpr ENotifyTriggerMode Modes[] = {
			ENotifyTriggerMode::AllAnimations, ENotifyTriggerMode::HighestWeightedAnimation, ENotifyTriggerMode::None };

		int CurrentIndex = 1;
		for (int Index = 0; Index < 3; ++Index)
		{
			if (Modes[Index] == Working->NotifyTriggerMode) CurrentIndex = Index;
		}
		if (ImGui::BeginCombo("Notify Trigger Mode", ModeLabels[CurrentIndex]))
		{
			for (int Index = 0; Index < 3; ++Index)
			{
				if (ImGui::Selectable(ModeLabels[Index], Index == CurrentIndex) && Index != CurrentIndex)
				{
					Working->NotifyTriggerMode = Modes[Index];
					Dirty = true;
				}
			}
			ImGui::EndCombo();
		}
		if (ImGui::IsItemHovered())
		{
			ImGui::SetTooltip("Which samples fire their notifies while blending");
		}
	}
	ImGui::Spacing();
	ImGui::TextWrapped("Drag animations from the Asset Browser onto the graph. Select a sample to edit its position.");
}

void FBlendSpaceEditorPanel::DrawGraph()
{
	ImGui::SeparatorText("Blend Graph");
	const ImVec2 Origin = ImGui::GetCursorScreenPos();
	const ImVec2 Size(std::max(ImGui::GetContentRegionAvail().x, 100.0f), std::max(ImGui::GetContentRegionAvail().y - 40, 120.0f));
	ImGui::InvisibleButton("BlendGraphCanvas", Size, ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
	const bool Hovered = ImGui::IsItemHovered();
	auto* Draw = ImGui::GetWindowDrawList();
	Draw->AddRectFilled(Origin, ImVec2(Origin.x + Size.x, Origin.y + Size.y), IM_COL32(25, 27, 30, 255));
	const auto& Axis = *Working->GetBlendParameter(0);
	const float Left = Origin.x + 24, Width = std::max(Size.x - 48, 1.0f), Y = Origin.y + Size.y * 0.5f;
	const double Range = static_cast<double>(Axis.Max) - Axis.Min;
	auto ToScreen = [&](float X) { return Left + static_cast<float>((static_cast<double>(X) - Axis.Min) / Range) * Width; };
	auto FromScreen = [&](float X) { return static_cast<float>(Axis.Min + std::clamp(static_cast<double>((X - Left) / Width), 0.0, 1.0) * Range); };
	for (int I = 0; I <= 10; ++I)
	{
		const float X = Left + Width * I / 10.0f;
		Draw->AddLine(ImVec2(X, Origin.y + 16), ImVec2(X, Origin.y + Size.y - 35), IM_COL32(52, 54, 59, 255));
		char Label[40]; snprintf(Label, sizeof(Label), "%.3g", Axis.Min + Range * I / 10.0);
		Draw->AddText(ImVec2(X - ImGui::CalcTextSize(Label).x * .5f, Origin.y + Size.y - 28), IM_COL32(175, 178, 185, 255), Label);
	}
	Draw->AddLine(ImVec2(Left, Y), ImVec2(Left + Width, Y), IM_COL32(112, 116, 125, 255), 2);
	const ImVec2 Mouse = ImGui::GetMousePos();
	int HitId = 0;
	for (const auto& Row : Rows)
	{
		const float X = ToScreen(Row.Id == DragId ? DragX : Row.Sample.SampleValue.X);
		const ImU32 Color = Row.Id == SelectedId ? IM_COL32(255, 172, 51, 255) : IM_COL32(106, 183, 223, 255);
		Draw->AddQuadFilled(ImVec2(X, Y-7), ImVec2(X+7, Y), ImVec2(X, Y+7), ImVec2(X-7, Y), Color);
		if (Hovered && std::abs(Mouse.x-X) <= 10 && std::abs(Mouse.y-Y) <= 12) HitId = Row.Id;
	}
	if (Hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
	{
		GraphFocused = true;
		Select(HitId);
		DragId = HitId;
		if (DragId) DragX = EditX;
	}
	else if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) GraphFocused = false;
	if (HitId && !DragId && !ImGui::GetDragDropPayload())
	{
		for (const auto& Row : Rows) if (Row.Id == HitId)
		{
			ImGui::BeginTooltip();
			ImGui::TextUnformatted(AnimationLabel(Row.Sample.Animation).c_str());
			ImGui::TextUnformatted(Row.Sample.Animation->GetPath().c_str());
			ImGui::Text("%s: %.6g", Axis.Name.ToString().c_str(), Row.Sample.SampleValue.X);
			ImGui::EndTooltip();
		}
	}
	if (DragId)
	{
		if (ImGui::IsKeyPressed(ImGuiKey_Escape)) DragId = 0;
		else
		{
			if (ImGui::IsMouseDragging(ImGuiMouseButton_Left)) DragX = FromScreen(Mouse.x);
			if (ImGui::IsMouseReleased(ImGuiMouseButton_Left))
			{
				const int Index = SelectedIndex();
				if (Index >= 0 && DragX != Rows[Index].Sample.SampleValue.X)
				{
					auto NewRows = Rows; NewRows[Index].Sample.SampleValue.X = DragX; CommitRows(std::move(NewRows));
				}
				DragId = 0;
			}
		}
	}
	// The canvas remains the last submitted item, so the entire graph accepts drops.
	if (ImGui::BeginDragDropTarget())
	{
		if (const ImGuiPayload* Payload = ImGui::AcceptDragDropPayload(AnimationPayload))
			if (Payload->DataSize == sizeof(UAnimSequence*)) AddAnimation(*static_cast<UAnimSequence* const*>(Payload->Data), FromScreen(Mouse.x));
		ImGui::EndDragDropTarget();
	}
	if (Hovered && HitId && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) { Select(HitId); GraphFocused = true; ImGui::OpenPopup("SampleContext"); }
	bool Remove = false;
	if (ImGui::BeginPopup("SampleContext"))
	{
		Remove = ImGui::MenuItem("Delete Sample", "Delete");
		ImGui::EndPopup();
	}
	if (GraphFocused && !ImGui::GetIO().WantTextInput && ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && !ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId))
		Remove |= ImGui::IsKeyPressed(ImGuiKey_Delete, false);
	if (Remove && SelectedIndex() >= 0)
	{
		auto NewRows = Rows; NewRows.erase(NewRows.begin()+SelectedIndex()); CommitRows(std::move(NewRows)); SelectedId = DragId = 0;
	}
	ImGui::Text("%s  |  %d samples", Axis.Name.ToString().c_str(), static_cast<int>(Rows.size()));
}

void FBlendSpaceEditorPanel::DrawSampleDetails()
{
	ImGui::SeparatorText("Sample Details");
	int Index = SelectedIndex();
	if (Index < 0) { ImGui::TextDisabled("Select a sample in the graph."); return; }
	const auto Animations = UAssetManager::Get().GetAnimationAssets(Working->GetSkeletalMesh());
	if (ImGui::BeginCombo("Animation", AnimationLabel(Rows[Index].Sample.Animation).c_str()))
	{
		for (auto* Animation : Animations)
		{
			ImGui::PushID(Animation);
			if (ImGui::Selectable(AnimationLabel(Animation).c_str(), Animation == Rows[Index].Sample.Animation))
			{
				auto NewRows = Rows; NewRows[Index].Sample.Animation = Animation; CommitRows(std::move(NewRows));
			}
			if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", Animation->GetPath().c_str());
			ImGui::PopID();
		}
		ImGui::EndCombo();
	}
	ImGui::InputFloat("X", &EditX, 0, 0, "%.6g");
	if (ImGui::IsItemDeactivatedAfterEdit())
	{
		const auto& Axis = *Working->GetBlendParameter(0);
		auto NewRows = Rows;
		NewRows[Index].Sample.SampleValue.X = std::clamp(EditX, Axis.Min, Axis.Max);
		CommitRows(std::move(NewRows));
	}
	ImGui::TextWrapped("%s", Rows[SelectedIndex()].Sample.Animation->GetPath().c_str());
}

void FBlendSpaceEditorPanel::DrawAssetBrowser()
{
	ImGui::SeparatorText("Asset Browser");
	Search.Draw("Search", -1);
	const auto Animations = UAssetManager::Get().GetAnimationAssets(Working->GetSkeletalMesh());
	if (Animations.IsEmpty()) ImGui::TextWrapped("No compatible registered animations.");
	for (auto* Animation : Animations)
	{
		if (!Search.PassFilter(Animation->GetPath().c_str())) continue;
		ImGui::PushID(Animation);
		if (ImGui::SmallButton("+"))
		{
			const auto& Axis = *Working->GetBlendParameter(0);
			AddAnimation(Animation, static_cast<float>(static_cast<double>(Axis.Min) + (static_cast<double>(Axis.Max)-Axis.Min)*.5));
		}
		ImGui::SameLine();
		ImGui::Selectable(AnimationLabel(Animation).c_str());
		if (ImGui::BeginDragDropSource())
		{
			ImGui::SetDragDropPayload(AnimationPayload, &Animation, sizeof(Animation));
			ImGui::TextUnformatted(AnimationLabel(Animation).c_str());
			ImGui::EndDragDropSource();
		}
		if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", Animation->GetPath().c_str());
		ImGui::PopID();
	}
}

void FBlendSpaceEditorPanel::DrawUnsavedDialog()
{
	if (Dirty && (PendingClose || !PendingPath.empty())) ImGui::OpenPopup("Unsaved BlendSpace");
	if (ImGui::BeginPopupModal("Unsaved BlendSpace", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
	{
		ImGui::TextUnformatted("Save changes before continuing?");
		bool Continue = false;
		if (ImGui::Button("Save")) Continue = Save();
		ImGui::SameLine();
		if (ImGui::Button("Discard"))
		{
			// Restore the registered definition even when closing, so reopening the
			// same asset cannot resurrect an abandoned draft.
			const FString NextPath = PendingPath;
			PendingPath = Path;
			LoadPending();
			PendingPath = NextPath;
			Continue = true;
		}
		ImGui::SameLine();
		if (ImGui::Button("Cancel")) { PendingClose = false; PendingPath.clear(); ImGui::CloseCurrentPopup(); }
		if (!Error.empty()) ImGui::TextWrapped("%s", Error.c_str());
		if (Continue)
		{
			ImGui::CloseCurrentPopup();
			if (PendingClose) { PendingClose = false; SetOpen(false); }
			else LoadPending();
		}
		ImGui::EndPopup();
	}
}

void FBlendSpaceEditorPanel::OnRender()
{
	bool Open = true;
	ImGui::SetNextWindowSize(ImVec2(1100, 650), ImGuiCond_FirstUseEver);
	const bool Visible = ImGui::Begin(GetPanelName(), &Open, Dirty ? ImGuiWindowFlags_UnsavedDocument : ImGuiWindowFlags_None);
	KeyboardFocused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
	if (!Open) { if (Dirty) PendingClose = true; else SetOpen(false); }
	if (Visible)
	{
		if (Working)
		{
			if (ImGui::Button("Save")) Save();
			if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_S, false)) Save();
			ImGui::SameLine(); ImGui::Text("%s%s", Path.c_str(), Dirty ? " *" : "");
			if (!Error.empty()) { ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1,.45f,.35f,1)); ImGui::TextWrapped("%s", Error.c_str()); ImGui::PopStyleColor(); }
			if (ImGui::BeginTable("BlendSpaceLayout", 3, ImGuiTableFlags_Resizable | ImGuiTableFlags_BordersInnerV))
			{
				ImGui::TableSetupColumn("Asset Details", ImGuiTableColumnFlags_WidthStretch, .24f);
				ImGui::TableSetupColumn("Blend Graph", ImGuiTableColumnFlags_WidthStretch, .48f);
				ImGui::TableSetupColumn("Samples", ImGuiTableColumnFlags_WidthStretch, .28f);
				ImGui::TableNextRow();
				ImGui::TableSetColumnIndex(0); ImGui::BeginChild("AssetDetails"); DrawAssetDetails(); ImGui::EndChild();
				ImGui::TableSetColumnIndex(1); ImGui::BeginChild("Graph", ImVec2(0,0), ImGuiChildFlags_None, ImGuiWindowFlags_NoScrollbar); DrawGraph(); ImGui::EndChild();
				ImGui::TableSetColumnIndex(2);
				const float Height = ImGui::GetContentRegionAvail().y;
				DetailsHeight = std::clamp(DetailsHeight, 80.0f, std::max(80.0f, Height-100));
				ImGui::BeginChild("SampleDetails", ImVec2(0, DetailsHeight)); DrawSampleDetails(); ImGui::EndChild();
				ImGui::InvisibleButton("VerticalSplitter", ImVec2(ImGui::GetContentRegionAvail().x, 5));
				if (ImGui::IsItemActive()) DetailsHeight += ImGui::GetIO().MouseDelta.y;
				if (ImGui::IsItemHovered() || ImGui::IsItemActive()) ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNS);
				ImGui::BeginChild("AssetBrowser"); DrawAssetBrowser(); ImGui::EndChild();
				ImGui::EndTable();
			}
		}
		else { ImGui::TextWrapped("%s", Error.empty() ? "Open or create a BlendSpace 1D from the Content Drawer." : Error.c_str()); }
	}
	DrawUnsavedDialog();
	ImGui::End();
}
