#pragma once
#include "Animation/AnimStateMachineAsset.h"
#include "StateMachineDebug.h"
#include "Editor/AnimGraphEditor/AnimPoseGraphEditor.h"
#include "Editor/EditorUI/EditorPanel.h"
#include <map>
#include <set>
namespace ax
{
namespace NodeEditor
{
struct EditorContext;
}
} // namespace ax
class FSkeletalMeshViewerPanel;
class UAssetAnimInstance;
class FStateMachineEditorPanel : public IEditorPanel
{
  public:
	~FStateMachineEditorPanel() override;
	bool Init() override
	{
		SetOpen(false);
		return true;
	}
	void Tick(float) override
	{
	}
	void OnRender() override;
	const char *GetPanelName() const override
	{
		return "State Machine Editor";
	}
	std::function<void()> OpenHost;
	void SetOpen(bool Open) override;
	void OpenAsset(const FString &Path);
	void NewAsset(const FString &Folder);
	void SetViewer(FSkeletalMeshViewerPanel *V)
	{
		Viewer = V;
	}
	bool IsKeyboardFocused() const
	{
		return IsOpen() && Focused;
	}
	void ReleaseResources();
	void SetPIEDebugProvider(FStateMachinePIEProvider Provider)
	{
		PIEProvider = std::move(Provider);
	}

  private:
    friend class FAnimGraphEditorPanel;
    void ClipboardActions(bool Copy, bool Paste, bool Duplicate);
    FString RuleSearch;
    int PendingFocusRule=0, RuleSearchSelection=0;
    int HoveredRuleTooltip=0;
    std::vector<int> PendingSelection;
	struct FGraphSession
	{
		ax::NodeEditor::EditorContext *Context = nullptr;
		std::set<uintptr_t> Placed;
		bool Fit = true;
        FString SettingsPath;
	};
	void ClearContexts();
	FGraphSession &GraphSession();
	void LoadPending();
	bool Save();
	void DrawGraph();
	void DrawStateGraph(FGraphSession &Session);
	void HandleStateGraphInteraction();
	void AutoArrange();
	void DrawRuleGraph(FGraphSession &Session);
	void DrawDetails();
	void DrawParameters();
	void DrawPreview();
	void UpdateObservations();
	void ResolveObservation();
	void DrawObservationControls();
	void DrawObservationStatus();
	void DrawRuleErrors();
	void DrawRuleProperties(FAnimRuleNode &Node, bool InCanvas = false);
	void DeleteSelection();
	void AddTransition(int From, int To);
	FAnimAssetState *FindState(int Id);
	FAnimAssetTransition *FindTransition(int Id);
	const char *StateName(int Id);
	bool StateCombo(const char *Label, int &Id, bool Any = false);
	bool ParameterCombo(const char *Label, FName &Name, EAnimParameterType Type, bool InCanvas);
	FString ParameterReferences(FName Name) const;
	void RenameParameter(FName Old, FName New);
	void AddState(ImVec2 Position = ImVec2(40, 40));
	int ActivePose = -1;
    std::map<int, std::unique_ptr<FAnimPoseGraphEditor>> PoseEditors;
    FAnimPoseGraphEditor& PoseEditor();
	FAnimStateMachineAssetData Working;
	FString Path, PendingPath, PendingFolder, Error;
	char SavePath[512] = "Assets/NewStateMachine.animsm";
	bool Dirty = false, PendingClose = false, Focused = false;
	int Selected = -1, SelectedTransition = -1, NextId = 1, ActiveRule = 0, SelectedRuleNode = 0;
	int PendingSource = -3, PendingStartFrame = -1, QueuedTransition = 0;
	uintptr_t ContextObject = 0, CreateFromPin = 0;
	ImVec2 CreationPosition{};
	std::map<int, FGraphSession> Sessions;
	FSkeletalMeshViewerPanel *Viewer = nullptr;
	FStateMachinePIEProvider PIEProvider;
	uint64 ObservedSession = 0;
	uint32 ObservedActor = 0, ObservedComponent = 0;
	bool ObservePIE = false;
	FString ObservedAssetKey, ExpectedSignature, ObservationMessage, ObservationDefinitionError;
	FStateMachinePIEFrame PIEFrame;
	FStateMachineObservation Observation;
};
