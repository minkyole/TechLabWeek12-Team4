#pragma once
#include "imgui.h"
#include "imgui_node_editor.h"

// Scoped to animation windows; never changes the rest of the editor's palette.
struct FAnimationEditorStyle
{
    FAnimationEditorStyle()
    {
        ImGui::PushStyleColor(ImGuiCol_WindowBg,ImVec4(.095f,.10f,.11f,1));
        ImGui::PushStyleColor(ImGuiCol_ChildBg,ImVec4(.105f,.11f,.12f,1));
        ImGui::PushStyleColor(ImGuiCol_FrameBg,ImVec4(.16f,.17f,.18f,1));
        ImGui::PushStyleColor(ImGuiCol_Header,ImVec4(.22f,.23f,.25f,1));
        ImGui::PushStyleColor(ImGuiCol_HeaderHovered,ImVec4(.32f,.25f,.16f,1));
        ImGui::PushStyleColor(ImGuiCol_HeaderActive,ImVec4(.42f,.28f,.12f,1));
        ImGui::PushStyleColor(ImGuiCol_Button,ImVec4(.19f,.20f,.22f,1));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered,ImVec4(.34f,.28f,.19f,1));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding,3);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,ImVec2(8,7));
    }
    ~FAnimationEditorStyle() { ImGui::PopStyleVar(2); ImGui::PopStyleColor(8); }
};
struct FAnimationGraphStyle
{
    bool Active=true;
    FAnimationGraphStyle()
    {
        namespace ed=ax::NodeEditor;
        ed::PushStyleColor(ed::StyleColor_Bg,ImVec4(.075f,.08f,.09f,1));
        ed::PushStyleColor(ed::StyleColor_Grid,ImVec4(.16f,.17f,.18f,.45f));
        ed::PushStyleColor(ed::StyleColor_NodeBg,ImVec4(.14f,.15f,.16f,1));
        ed::PushStyleColor(ed::StyleColor_SelNodeBorder,ImVec4(1,.55f,.12f,1));
        ed::PushStyleVar(ed::StyleVar_NodeRounding,5.0f);
    }
    void End() { if(Active) { ax::NodeEditor::PopStyleVar(); ax::NodeEditor::PopStyleColor(4); Active=false; } }
    ~FAnimationGraphStyle() { End(); }
};
