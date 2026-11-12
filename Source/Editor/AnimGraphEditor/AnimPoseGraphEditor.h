#pragma once
#include "Animation/AnimGraph.h"
#include "imgui_node_editor.h"
#include <set>
#include <functional>

// Same authoring widget for the top-level graph and a state's pose graph.
class FAnimPoseGraphEditor
{
public:
    ~FAnimPoseGraphEditor();
    void Reset();
    void SetSettingsKey(const FString& Key);
    FString SettingsPath;
    bool LoadView=false;
    int SearchSelection=0;
    void RestorePositions() { Placed.clear(); }
    void FocusNode(int Id) { Selected=Id; PendingFocus=Id; }
    void ClearSelection() { Selected=0; PendingClear=true; }
    int GetSelected() const { return Selected; }
    bool IsCanvasFocused() const { return CanvasFocused; }
    std::function<void(const FString&)> OnOpenAsset;
    bool Draw(FAnimPoseGraph& Graph, const std::vector<FAnimParameter>& Parameters, const FString& MeshKey);
    bool DrawProperties(FAnimPoseGraph& Graph, const std::vector<FAnimParameter>& Parameters, const FString& MeshKey);
private:
    ax::NodeEditor::EditorContext* Context = nullptr;
    std::set<int> Placed;
    int Selected=0, PendingFocus=0, ContextNode=0;
    uintptr_t ContextPin=0, CreateFromPin=0;
    ImVec2 CreationPosition{};
    char Search[128]{};
    bool CanvasFocused=false, PendingClear=false;
    std::vector<int> PendingSelection;
    bool Clipboard(FAnimPoseGraph& Graph, bool Copy, bool Paste, bool Duplicate);
    bool Fit = true;
};
bool AnimGraphTextEdit(const char* Label, FString& Value);
bool DrawAnimGraphParameters(std::vector<FAnimParameter>& Parameters);

struct FAnimationAssetDrag { int Type=0; char Key[1024]{}; };
inline constexpr const char* AnimationAssetPayload = "ANIMATION_GRAPH_ASSET";
