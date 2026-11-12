#include "EnginePCH.h"
#include "AnimGraphEditorPanel.h"
#include "AnimationPreviewSession.h"
#include "AnimationEditorStyle.h"
#include "Editor/StateMachineEditor/StateMachineEditorPanel.h"
#include "Asset/AssetManager.h"
#include "Core/Misc/FileHelper.h"
#include "UObject/UObjectIterator.h"
#include "Components/SkeletalMeshComponent.h"
#include "Animation/AnimSequence.h"
#include "Animation/BlendSpace.h"
#include "Render/Mesh.h"
#include <algorithm>
#include <map>

void ReloadAnimationAssetInstances(const FString& Path)
{
    // Collect first: recreating instances mutates GUObjectArray during iteration.
    std::vector<USkeletalMeshComponent*> Components;
    for(TObjectIterator<USkeletalMeshComponent> It;It;++It)
        if(!It->IsTemplate() && It->UsesAnimationAsset(Path)) Components.push_back(*It);
    for(auto* Component:Components)
    {
        if (auto* Graph = Cast<UAnimGraphInstance>(Component->GetAnimInstance()); Graph && Component->GetAnimGraphAssetPath().empty())
        { auto Snapshot=Graph->GetAssetData(); Graph->Configure(Snapshot,false); Component->RefreshAnimationPose(); }
        else if (auto* SM = Cast<UAssetAnimInstance>(Component->GetAnimInstance()); SM && Component->GetStateMachineAssetPath().empty())
        { auto Snapshot=SM->GetAssetData(); SM->Configure(Snapshot,false); Component->RefreshAnimationPose(); }
        else { Component->ClearAnimInstance(); Component->InitializeAnimInstance(); }
    }
}
namespace
{
json ParameterJson(const std::vector<FAnimParameter>& Parameters)
{
    json J=json::array(); for(auto& P:Parameters) J.push_back({P.Name.ToString(),(int)P.Type,P.BoolValue,P.FloatValue}); return J;
}
json ConditionJson(const FAnimConditionDefinition& C)
{
    json Children=json::array(); for(auto Id:C.Children) Children.push_back(Id);
    return { (int)C.Type,C.ParameterName.ToString(),C.BoolValue,(int)C.CompareOp,C.FloatValue,C.Tolerance,Children };
}
FString StateEditSignature(const FAnimStateMachineAssetData& D)
{
    json J={{"Mesh",D.MeshKey},{"Script",D.UpdateScript},{"Entry",D.Entry},{"Position",{D.EntryX,D.EntryY,D.AnyX,D.AnyY}},
        {"Parameters",ParameterJson(D.Parameters)},{"States",json::array()},{"Transitions",json::array()},{"Conditions",json::array()}};
    for(auto& S:D.States) J["States"].push_back({S.Id,S.Name,S.AssetKey,S.BlendSpace,S.BlendParameter,S.Options.bLoop,S.Options.bPlaying,S.Options.PlayRate,S.X,S.Y,AnimPoseGraphJson(S.PoseGraph)});
    for(auto& C:D.Conditions) J["Conditions"].push_back(ConditionJson(C));
    for(auto& T:D.Transitions)
    {
        json Nodes=json::array(),Links=json::array(),Consumed=json::array();
        for(auto& N:T.Rule.Nodes) Nodes.push_back({N.Id,N.Result,N.InputCount,N.X,N.Y,ConditionJson(N.Condition)});
        for(auto& L:T.Rule.Links) Links.push_back({L.Id,L.From,L.To,L.Input});
        for(auto N:T.Options.ConsumeTriggers) Consumed.push_back(N.ToString());
        J["Transitions"].push_back({T.Id,T.From,T.To,T.X,T.Y,T.HasPosition,T.Options.Condition,T.Options.Priority,T.Options.Blend.Duration,(int)T.Options.Blend.Curve,(int)T.Options.InterruptPolicy,T.Options.bRestartIfSameState,Consumed,Nodes,Links});
    }
    return J.dump();
}
FString CanonicalAnimationPath(const FString& Path)
{
    std::error_code EC; auto P=fs::weakly_canonical(FFileHelper::ToNativePath(Path),EC);
    return EC?Path:FFileHelper::ToUtf8Path(P);
}
bool Matches(const char* Filter,const FString& Value) { ImGuiTextFilter F(Filter); return F.PassFilter(Value.c_str()); }
}
struct FAnimGraphEditorPanel::FSnapshot
{
    FAnimGraphAssetData Graph;
    FAnimStateMachineAssetData State;
    FString Key;
};
struct FAnimGraphEditorPanel::FDocument
{
    int Id=0, Parent=0;
    std::vector<std::pair<int,int>> GraphTabs{{-1,0}};
    std::pair<int,int> DisplayedGraph{-2,-1};
    FAnimGraphAssetData Graph;
    FAnimPoseGraphEditor Editor;
    // Machine must be released before its non-owning preview viewer pointer.
    FAnimationPreviewSession Preview;
    std::unique_ptr<FStateMachineEditorPanel> Machine;
    FString Path, SavedKey, CompiledKey, CompileError;
    char SavePath[512]{}, TreeSearch[128]{}, AssetSearch[128]{};
    int SelectedParameter=-1, PendingParameter=-1, PendingType=-1;
    bool Dirty=false, CanvasFocused=false, CompileFailed=false;
    FSnapshot Previous;
    std::unique_ptr<FSnapshot> PendingEdit;
    std::vector<FSnapshot> Undo, Redo;
    std::vector<FAnimGraphDiagnostic> Diagnostics;
    std::vector<FAnimParameter>& Parameters() { return Machine?Machine->Working.Parameters:Graph.Parameters; }
    FString& Mesh() { return Machine?Machine->Working.MeshKey:Graph.MeshKey; }
    FString& Script() { return Machine?Machine->Working.UpdateScript:Graph.UpdateScript; }
    FString Title() const { return Path.empty()?(Machine?"New State Machine":"New AnimGraph"):FFileHelper::ToUtf8Path(FFileHelper::ToNativePath(Path).filename()); }
};
FAnimGraphEditorPanel::FAnimGraphEditorPanel() = default;
FAnimGraphEditorPanel::~FAnimGraphEditorPanel() { ReleaseResources(); }
bool FAnimGraphEditorPanel::Init()
{
    IEditorPanel::SetOpen(false);
    FString Text;
    if(FFileHelper::LoadFileToString(Text,"Saved/AnimationEditor/UI.json"))
        try { auto J=json::parse(Text); for(int I=0;I<6;++I) PaneOpen[I]=J.at("Panels").at(I).get<bool>(); } catch(const std::exception&) {}
    return true;
}
void FAnimGraphEditorPanel::SaveUISettings()
{
    std::error_code EC; fs::create_directories("Saved/AnimationEditor",EC);
    json Panels=json::array(); for(bool Open:PaneOpen) Panels.push_back(Open);
    FFileHelper::SaveStringToFile(json{{"Panels",Panels}}.dump(2),"Saved/AnimationEditor/UI.json");
}
FAnimGraphEditorPanel::FDocument* FAnimGraphEditorPanel::FindDocument(int Id)
{
    for(auto& D:Documents) if(D->Id==Id) return D.get(); return nullptr;
}
FAnimGraphEditorPanel::FDocument* FAnimGraphEditorPanel::Current() { return FindDocument(Active); }
void FAnimGraphEditorPanel::ReleaseResources() { Documents.clear(); Locations.clear(); Active=0; }
void FAnimGraphEditorPanel::Tick(float Delta)
{
    for(auto& D:Documents)
    {
        if(IsOpen() && Visible && D->Id==Active) D->Preview.Tick(Delta);
        else D->Preview.Hide();
    }
}
void FAnimGraphEditorPanel::RenderPreview() { if(IsOpen() && Visible) if(auto* D=Current()) D->Preview.Render(); }
void FAnimGraphEditorPanel::SetOpen(bool Open)
{
    if(!Open)
    {
        for(auto& D:Documents) if(D->Dirty) { CloseAll=true; CloseDocument=D->Id; return; }
        for(auto& D:Documents) D->Preview.Hide();
    }
    if(!Open) CloseAll=false;
    IEditorPanel::SetOpen(Open);
}
void FAnimGraphEditorPanel::OpenReference(const FString& Path) { OpenAsset(Path); PendingParent=Active; }
void FAnimGraphEditorPanel::OpenAsset(const FString& Path) { PendingParent=0; PendingPath=Path; PendingFolder.clear(); IEditorPanel::SetOpen(true); }
void FAnimGraphEditorPanel::NewAsset(const FString& Folder) { PendingParent=0; PendingFolder=Folder; PendingPath.clear(); NewMachine=false; IEditorPanel::SetOpen(true); }
void FAnimGraphEditorPanel::NewStateMachine(const FString& Folder) { PendingParent=0; PendingFolder=Folder; PendingPath.clear(); NewMachine=true; IEditorPanel::SetOpen(true); }
void FAnimGraphEditorPanel::OpenPending()
{
    if(PendingPath.empty() && PendingFolder.empty()) return;
    int Parent=PendingParent; PendingParent=0;
    auto Path=PendingPath,Folder=PendingFolder; PendingPath.clear(); PendingFolder.clear();
    if(!Path.empty()) for(auto& D:Documents) if(CanonicalAnimationPath(D->Path)==CanonicalAnimationPath(Path)) { Activate(D->Id); return; }
    bool Machine=Path.empty()?NewMachine:_stricmp(FFileHelper::ToNativePath(Path).extension().string().c_str(),".animsm")==0;
    auto D=std::make_unique<FDocument>(); D->Id=NextDocument++; D->Path=Path; D->Parent=Parent;
    if(Machine)
    {
        D->Machine=std::make_unique<FStateMachineEditorPanel>(); D->Machine->Init();
        if(StateTemplate) D->Machine->PIEProvider=StateTemplate->PIEProvider;
        if(!Path.empty()) { auto* Asset=UAssetManager::Get().LoadAnimStateMachine(Path,Error); if(!Asset) return; D->Machine->Working=Asset->Data; }
        D->Machine->Path=Path;
        D->Machine->NextId=1; for(auto& S:D->Machine->Working.States) D->Machine->NextId=std::max(D->Machine->NextId,S.Id+1);
    }
    else if(!Path.empty()) { auto* Asset=UAssetManager::Get().LoadAnimGraph(Path,Error); if(!Asset) return; D->Graph=Asset->Data; }
    else { FAnimGraphNode N; N.Type=EAnimGraphNodeType::Output; N.X=500; D->Graph.Graph.Nodes.push_back(N); }
    if(Path.empty())
    {
        auto Meshes=UAssetManager::Get().GetSkeletalMeshAssets(); if(Meshes.Num()) D->Mesh()=Meshes[0]->GetPath();
        auto Target=FFileHelper::ToUtf8Path(FFileHelper::ToNativePath(Folder)/(Machine?"NewStateMachine.animsm":"NewAnimGraph.animgraph"));
        strncpy_s(D->SavePath,Target.c_str(),_TRUNCATE);
    }
    else strncpy_s(D->SavePath,Path.c_str(),_TRUNCATE);
    D->Editor.SetSettingsKey(Path.empty()?FString{}:CanonicalAnimationPath(Path)+"/AnimGraph");
    D->Editor.OnOpenAsset=[this](const FString& P){OpenReference(P);};
    D->Previous=Capture(*D); D->SavedKey=Path.empty()?FString{}:D->Previous.Key; D->Dirty=Path.empty();
    int Id=D->Id; Documents.push_back(std::move(D)); Activate(Id); Error.clear();
}
FAnimGraphEditorPanel::FSnapshot FAnimGraphEditorPanel::Capture(FDocument& D)
{
    FSnapshot S;
    if(D.Machine) { S.State=D.Machine->Working; S.Key=StateEditSignature(S.State); }
    else { S.Graph=D.Graph; S.Key=json{{"Mesh",D.Graph.MeshKey},{"Script",D.Graph.UpdateScript},{"Parameters",ParameterJson(D.Graph.Parameters)},{"Graph",AnimPoseGraphJson(D.Graph.Graph)}}.dump(); }
    return S;
}
void FAnimGraphEditorPanel::Restore(FDocument& D,const FSnapshot& S)
{
    if(D.Machine)
    {
        D.Machine->Working=S.State;
        for(auto& [Id,Session]:D.Machine->Sessions) Session.Placed.clear();
        for(auto& [Id,Editor]:D.Machine->PoseEditors) Editor->RestorePositions();
        D.Machine->NextId=1; for(auto& State:S.State.States) D.Machine->NextId=std::max(D.Machine->NextId,State.Id+1);
        if(!D.Machine->FindState(D.Machine->ActivePose)) D.Machine->ActivePose=-1;
        if(!D.Machine->FindTransition(D.Machine->ActiveRule)) D.Machine->ActiveRule=0;
    }
    else { D.Graph=S.Graph; D.Editor.RestorePositions(); }
    D.SelectedParameter=-1; D.Previous=S; D.PendingEdit.reset(); D.Dirty=S.Key!=D.SavedKey; D.Diagnostics.clear();
}
void FAnimGraphEditorPanel::CommitEdits(FDocument& D)
{
    auto S=Capture(D);
    if(S.Key!=D.Previous.Key)
    {
        if(!D.PendingEdit) D.PendingEdit=std::make_unique<FSnapshot>(D.Previous);
        D.Redo.clear(); D.Previous=std::move(S); D.Dirty=D.Previous.Key!=D.SavedKey;
        D.Diagnostics.clear(); D.CompileFailed=false;
    }
    if(D.PendingEdit && !ImGui::IsMouseDown(ImGuiMouseButton_Left) && !ImGui::IsAnyItemActive())
    {
        D.Undo.push_back(std::move(*D.PendingEdit)); D.PendingEdit.reset();
        if(D.Undo.size()>100) D.Undo.erase(D.Undo.begin());
    }
}
void FAnimGraphEditorPanel::Undo(FDocument& D,bool Redo)
{
    if(D.PendingEdit) { D.Undo.push_back(*D.PendingEdit); D.PendingEdit.reset(); if(D.Undo.size()>100) D.Undo.erase(D.Undo.begin()); }
    auto& From=Redo?D.Redo:D.Undo; auto& To=Redo?D.Undo:D.Redo;
    if(From.empty()) return;
    auto Snapshot=std::move(From.back()); From.pop_back(); To.push_back(Capture(D)); Restore(D,Snapshot);
}
bool FAnimGraphEditorPanel::Save(FDocument& D)
{
    auto Target=D.Path.empty()?FString(D.SavePath):D.Path;
    bool OK=D.Machine?UAssetManager::Get().SaveAnimStateMachine(Target,D.Machine->Working,Error,D.Path.empty()):UAssetManager::Get().SaveAnimGraph(Target,D.Graph,Error,D.Path.empty());
    if(!OK) { CollectDiagnostics(D); if(D.Diagnostics.empty()) D.Diagnostics.push_back({Target,"",Error}); return false; }
    D.Path=Target;
    D.Editor.SetSettingsKey(CanonicalAnimationPath(Target)+"/AnimGraph");
    if(D.Machine)
    {
        D.Machine->Path=Target;
        for(auto& [Id,Session]:D.Machine->Sessions)
            if(Session.SettingsPath.empty()) Session.SettingsPath="Saved/AnimationEditor/"+std::to_string(std::hash<FString>{}(Target+"/Rule/"+std::to_string(Id)))+".json";
        for(auto& [Id,Editor]:D.Machine->PoseEditors) Editor->SetSettingsKey(Target+"/State/"+std::to_string(Id));
    }
    D.SavedKey=Capture(D).Key; D.Dirty=false;
    ReloadAnimationAssetInstances(Target); Error.clear(); return true;
}
void FAnimGraphEditorPanel::CollectDiagnostics(FDocument& D)
{
    D.Diagnostics.clear();
    auto Pose=[&](const FAnimPoseGraph& G,int State) {
        FString Message; int Bad=0;
        bool Valid=G.Validate(Message,&Bad);
        if(Valid)
        {
            Valid=ValidateAnimPoseGraphAssets(G,D.Mesh(),D.Parameters(),Message,D.Path);
            if(!Valid)
                for(auto& N:G.Nodes)
                    if((!N.AssetKey.empty() && Message.find(N.AssetKey)!=FString::npos && Message.find("node "+std::to_string(N.Id))!=FString::npos) ||
                       (!N.Parameter.empty() && Message=="Missing Float parameter: "+N.Parameter)) { Bad=N.Id; break; }
        }
        if(!Valid) { FAnimGraphDiagnostic V; V.AssetPath=D.Path; V.GraphPath=State<0?"AnimGraph":"State/Pose"; V.StateId=State; V.NodeId=Bad; V.Message=Message; D.Diagnostics.push_back(V); }
        std::set<int> ActiveNodes;
        std::function<void(int)> Mark=[&](int Id){if(!ActiveNodes.insert(Id).second)return; for(auto& L:G.Links) if(L.To==Id) Mark(L.From);};
        for(auto& N:G.Nodes) if(N.Type==EAnimGraphNodeType::Output) Mark(N.Id);
        for(auto& N:G.Nodes) if(!ActiveNodes.count(N.Id)) { FAnimGraphDiagnostic V; V.AssetPath=D.Path; V.GraphPath=State<0?"AnimGraph":"State/Pose"; V.StateId=State; V.NodeId=N.Id; V.Message="Disconnected node is excluded from playback."; V.Severity=EAnimDiagnosticSeverity::Warning; D.Diagnostics.push_back(V); }
    };
    FString Message;
    if(D.Machine)
    {
        for(auto& S:D.Machine->Working.States) if(!S.PoseGraph.Nodes.empty()) Pose(S.PoseGraph,S.Id);
        for(auto& T:D.Machine->Working.Transitions)
        {
            std::vector<FAnimConditionDefinition> Conditions; int Root=-1,Bad=0;
            if(!T.Rule.Compile(D.Parameters(),Conditions,Root,Message,&Bad)) { FAnimGraphDiagnostic V; V.AssetPath=D.Path; V.GraphPath="Transition Rule"; V.TransitionId=T.Id; V.NodeId=Bad; V.Message=Message; D.Diagnostics.push_back(V); }
        }
        auto Data=D.Machine->Working;
        if(!Data.CompileRules(Message) || !Data.Validate(Message))
            if(std::none_of(D.Diagnostics.begin(),D.Diagnostics.end(),[](auto& V){return V.Severity==EAnimDiagnosticSeverity::Error;})) D.Diagnostics.push_back({D.Path,"StateMachine",Message});
    }
    else
    {
        Pose(D.Graph.Graph,-1);
        if(!D.Graph.Validate(Message) && std::none_of(D.Diagnostics.begin(),D.Diagnostics.end(),[](auto& V){return V.Severity==EAnimDiagnosticSeverity::Error;})) D.Diagnostics.push_back({D.Path,"AnimGraph",Message});
    }
}
bool FAnimGraphEditorPanel::Compile(FDocument& D)
{
    CommitEdits(D); CollectDiagnostics(D);
    if(std::any_of(D.Diagnostics.begin(),D.Diagnostics.end(),[](auto& V){return V.Severity==EAnimDiagnosticSeverity::Error;})) { D.CompileFailed=true; return false; }
    if(!Viewer) { Error="Preview renderer is unavailable."; return false; }
    bool OK;
    if(D.Machine) { auto Copy=D.Machine->Working; OK=Copy.CompileRules(Error) && D.Preview.Compile(*Viewer,Copy,Error); }
    else OK=D.Preview.Compile(*Viewer,D.Graph,Error);
    D.CompileFailed=!OK;
    if(!OK) { D.Diagnostics.push_back({D.Path,D.Machine?"StateMachine":"AnimGraph",Error}); return false; }
    D.CompiledKey=Capture(D).Key; D.CompileError.clear(); Error.clear();
    if(D.Machine) { D.Machine->Viewer=D.Preview.GetBackend(); D.Machine->UpdateObservations(); }
    return true;
}
void FAnimGraphEditorPanel::Activate(int Id,bool Record)
{
    if(auto* D=Current()) D->Preview.Hide();
    Active=Id; SelectTab=Id; if(Record) RecordLocation();
}
void FAnimGraphEditorPanel::RecordLocation()
{
    auto* D=Current(); if(!D) return;
    FLocation L{D->Id,D->Machine?D->Machine->ActivePose:-1,D->Machine?D->Machine->ActiveRule:0};
    if(LocationIndex>=0 && Locations[LocationIndex]==L) return;
    Locations.resize(LocationIndex+1); Locations.push_back(L); LocationIndex=(int)Locations.size()-1;
}
void FAnimGraphEditorPanel::ShowLocation(const FLocation& L)
{
    if(auto* D=FindDocument(L.Document))
    { Activate(D->Id,false); if(D->Machine) { D->Machine->ActivePose=L.Pose; D->Machine->ActiveRule=L.Rule; } }
}
void FAnimGraphEditorPanel::Navigate(int Direction)
{
    int Next=LocationIndex+Direction;
    while(Next>=0 && Next<(int)Locations.size()) { if(FindDocument(Locations[Next].Document)) { LocationIndex=Next; ShowLocation(Locations[Next]); return; } Next+=Direction; }
}
void FAnimGraphEditorPanel::DrawToolbar(FDocument& D)
{
    if(ImGui::Button("Save")) Save(D); ImGui::SameLine();
    ImGui::PushStyleColor(ImGuiCol_Button,ImVec4(.22f,.37f,.18f,1));
    if(ImGui::Button("Compile")) Compile(D); ImGui::PopStyleColor(); ImGui::SameLine();
    bool Current=Capture(D).Key==D.CompiledKey;
    ImGui::TextColored(D.CompileFailed?ImVec4(1,.35f,.3f,1):Current?ImVec4(.55f,.85f,.4f,1):ImVec4(1,.7f,.25f,1),"%s",D.CompileFailed?"Compile Error":Current?"Up to date":"Needs Compile");
    ImGui::SameLine(); ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical); ImGui::SameLine();
    ImGui::BeginDisabled(!D.Preview.GetInstance() || (D.Machine && D.Machine->ObservePIE));
    if(ImGui::Button(D.Preview.IsPaused()?"Play":"Pause")) D.Preview.SetPaused(!D.Preview.IsPaused());
    ImGui::SameLine(); if(ImGui::Button("Stop")) { if(Compile(D)) D.Preview.SetPaused(true); }
    ImGui::SameLine(); if(ImGui::Button("Reset")) Compile(D);
    ImGui::EndDisabled(); ImGui::SameLine();
    ImGui::TextDisabled("%s",D.Machine && D.Machine->ObservePIE?"PIE (read-only)":"Preview");
    ImGui::SameLine(); ImGui::Text(" | %s%s",D.Title().c_str(),D.Dirty?" *":"");
}
void FAnimGraphEditorPanel::DrawTree(FDocument& D)
{
    ImGui::InputTextWithHint("##TreeSearch","Search graphs and variables...",D.TreeSearch,sizeof(D.TreeSearch));
    if(ImGui::CollapsingHeader("Graphs",ImGuiTreeNodeFlags_DefaultOpen))
    {
        if(ImGui::Selectable(D.Machine?"State Machine":"AnimGraph",D.SelectedParameter<0 && (!D.Machine || (D.Machine->ActivePose<0&&!D.Machine->ActiveRule))))
        {
            D.SelectedParameter=-1; if(D.Machine) { D.Machine->ActivePose=-1; D.Machine->ActiveRule=0; } else D.Editor.ClearSelection(); RecordLocation();
        }
        if(D.Machine)
        {
            for(auto& S:D.Machine->Working.States)
            {
                ImGui::PushID(S.Id); auto Label=S.Name+" / Pose";
                if(Matches(D.TreeSearch,Label) && ImGui::Selectable(Label.c_str(),D.Machine->ActivePose==S.Id)) { D.SelectedParameter=-1; D.Machine->ActivePose=S.Id; D.Machine->ActiveRule=0; RecordLocation(); }
                ImGui::PopID();
            }
            for(auto& T:D.Machine->Working.Transitions)
            {
                auto Label=FString(D.Machine->StateName(T.From))+" -> "+D.Machine->StateName(T.To)+" / Rule";
                ImGui::PushID(T.Id+100000);
                if(Matches(D.TreeSearch,Label) && ImGui::Selectable(Label.c_str(),D.Machine->ActiveRule==T.Id)) { D.SelectedParameter=-1; D.Machine->ActivePose=-1; D.Machine->ActiveRule=T.Id; RecordLocation(); }
                ImGui::PopID();
            }
        }
        auto References=[&](const FAnimPoseGraph& Graph) {
            for(auto& N:Graph.Nodes) if(N.Type==EAnimGraphNodeType::StateMachine && !N.AssetKey.empty() && Matches(D.TreeSearch,N.AssetKey))
            {
                auto Title="  [Asset] "+FFileHelper::ToUtf8Path(FFileHelper::ToNativePath(N.AssetKey).filename());
                ImGui::PushID(N.AssetKey.c_str()); if(ImGui::Selectable(Title.c_str())) OpenReference(N.AssetKey); ImGui::PopID();
                if(ImGui::IsItemHovered()) ImGui::SetTooltip("Open referenced asset: %s",N.AssetKey.c_str());
            }
        };
        if(D.Machine) for(auto& S:D.Machine->Working.States) References(S.PoseGraph); else References(D.Graph.Graph);
    }
    if(ImGui::CollapsingHeader("Variables",ImGuiTreeNodeFlags_DefaultOpen))
    {
        if(ImGui::SmallButton("+ Variable"))
        {
            int Number=1; FName Name;
            do { Name=FName(("Parameter_"+std::to_string(Number++)).c_str()); } while(std::any_of(D.Parameters().begin(),D.Parameters().end(),[&](auto& P){return P.Name==Name;}));
            FAnimParameter P; P.Name=Name; D.Parameters().push_back(P); D.SelectedParameter=(int)D.Parameters().size()-1;
        }
        for(int I=0;I<(int)D.Parameters().size();++I)
        {
            auto& P=D.Parameters()[I]; auto Name=P.Name.ToString(); if(!Matches(D.TreeSearch,Name)) continue;
            ImGui::PushID(I); ImGui::PushStyleColor(ImGuiCol_Text,P.Type==EAnimParameterType::Float?ImVec4(.45f,.85f,.38f,1):ImVec4(.95f,.3f,.35f,1));
            auto Label=(P.Type==EAnimParameterType::Trigger?"[!] ":P.Type==EAnimParameterType::Float?"[F] ":"[B] ")+Name;
            if(ImGui::Selectable(Label.c_str(),D.SelectedParameter==I)) D.SelectedParameter=I;
            ImGui::PopStyleColor(); ImGui::PopID();
        }
    }
}
void FAnimGraphEditorPanel::DrawDetails(FDocument& D)
{
    if(D.Path.empty()) ImGui::InputText("Save path",D.SavePath,sizeof(D.SavePath));
    auto References=[&](FName Name) {
        if(D.Machine) return D.Machine->ParameterReferences(Name);
        FString Result; for(auto& N:D.Graph.Graph.Nodes) if(N.Parameter==Name.ToString()) Result+="\nPose node #"+std::to_string(N.Id);
        return Result;
    };
    if(D.SelectedParameter>=0 && D.SelectedParameter<(int)D.Parameters().size())
    {
        auto& P=D.Parameters()[D.SelectedParameter];
        ImGui::SeparatorText("Variable");
        FString Name=P.Name.ToString();
        if(AnimGraphTextEdit("Name",Name))
        {
            FName New(Name.c_str()); bool Duplicate=Name.empty();
            for(int I=0;I<(int)D.Parameters().size();++I) if(I!=D.SelectedParameter && D.Parameters()[I].Name==New) Duplicate=true;
            if(Duplicate) Error="Choose a unique, nonempty variable name.";
            else
            {
                if(D.Machine) D.Machine->RenameParameter(P.Name,New);
                else for(auto& N:D.Graph.Graph.Nodes) if(N.Parameter==P.Name.ToString()) N.Parameter=Name;
                P.Name=New;
            }
        }
        int Type=(int)P.Type;
        if(ImGui::Combo("Type",&Type,"Bool\0Float\0Trigger\0"))
        {
            if(References(P.Name).empty()) P.Type=(EAnimParameterType)Type;
            else { D.PendingParameter=D.SelectedParameter; D.PendingType=Type; ImGui::OpenPopup("Change Variable References"); }
        }
        if(P.Type==EAnimParameterType::Float) ImGui::InputFloat("Default Value",&P.FloatValue);
        if(P.Type==EAnimParameterType::Bool) ImGui::Checkbox("Default Value",&P.BoolValue);
        if(ImGui::Button("Delete Variable"))
        {
            if(References(P.Name).empty()) { D.Parameters().erase(D.Parameters().begin()+D.SelectedParameter); D.SelectedParameter=-1; }
            else { D.PendingParameter=D.SelectedParameter; D.PendingType=-1; ImGui::OpenPopup("Change Variable References"); }
        }
        ImGui::TextDisabled("Lua references must be renamed in the script.");
    }
    else
    {
        bool Selected=false;
        if(D.Machine)
        {
            auto& S=*D.Machine;
            if(S.ActivePose>=0) { S.PoseEditor().SetSettingsKey(D.Path.empty()?FString{}:CanonicalAnimationPath(D.Path)+"/State/"+std::to_string(S.ActivePose)); S.PoseEditor().OnOpenAsset=[this](const FString& P){OpenReference(P);}; S.DrawDetails(); Selected=true; }
            else if(S.ActiveRule)
            {
                if(auto* T=S.FindTransition(S.ActiveRule)) if(auto* N=T->Rule.Find(S.SelectedRuleNode)) { S.DrawRuleProperties(*N); Selected=true; }
            }
            else if(S.FindState(S.Selected) || S.SelectedTransition>=0) { S.DrawDetails(); Selected=true; }
        }
        else if(D.Editor.GetSelected()) { D.Editor.DrawProperties(D.Graph.Graph,D.Parameters(),D.Mesh()); Selected=true; }
        if(!Selected)
        {
            ImGui::SeparatorText("Animation Asset");
            if(ImGui::BeginCombo("Preview Mesh",D.Mesh().c_str()))
            { for(auto* M:UAssetManager::Get().GetSkeletalMeshAssets()) if(ImGui::Selectable(M->GetPath().c_str(),D.Mesh()==M->GetPath())) D.Mesh()=M->GetPath(); ImGui::EndCombo(); }
            AnimGraphTextEdit("Update Script",D.Script());
            ImGui::TextWrapped("%s",D.Path.empty()?"Unsaved asset":D.Path.c_str());
        }
    }
    if(ImGui::BeginPopupModal("Change Variable References",nullptr,ImGuiWindowFlags_AlwaysAutoResize))
    {
        if(D.PendingParameter>=0 && D.PendingParameter<(int)D.Parameters().size())
        {
            auto& P=D.Parameters()[D.PendingParameter];
            ImGui::TextWrapped("This variable is used at:%s",References(P.Name).c_str());
            ImGui::TextWrapped("Continue and leave these references for manual correction? Compile will report unresolved or incompatible bindings.");
            if(ImGui::Button("Continue"))
            {
                if(D.PendingType<0) { D.Parameters().erase(D.Parameters().begin()+D.PendingParameter); D.SelectedParameter=-1; }
                else P.Type=(EAnimParameterType)D.PendingType;
                D.PendingParameter=-1; ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine(); if(ImGui::Button("Cancel")) { D.PendingParameter=-1; ImGui::CloseCurrentPopup(); }
        }
        else ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
}
void FAnimGraphEditorPanel::DrawPreviewParameters(FDocument& D)
{
    if(D.Machine && D.Machine->ObservePIE) { ImGui::TextDisabled("PIE observation is read-only."); D.Machine->DrawObservationStatus(); return; }
    auto* Instance=D.Preview.GetInstance();
    if(!Instance) { ImGui::TextDisabled("Compile to initialize preview parameters."); return; }
    ImGui::TextDisabled("Manual preview values; Lua update disabled.");
    if(Capture(D).Key!=D.CompiledKey) ImGui::TextWrapped("Showing the last successful compile. Current edits have not been applied.");
    for(auto P:Instance->GetParameters().Values)
    {
        auto Name=P.Name.ToString(); ImGui::PushID(Name.c_str());
        if(P.Type==EAnimParameterType::Float) { float V=P.FloatValue; if(ImGui::DragFloat(Name.c_str(),&V,.05f)) Instance->SetFloat(Name,V); }
        else if(P.Type==EAnimParameterType::Bool) { bool V=P.BoolValue; if(ImGui::Checkbox(Name.c_str(),&V)) Instance->SetBool(Name,V); }
        else if(ImGui::Button(("Trigger "+Name).c_str())) Instance->SetTrigger(Name);
        ImGui::PopID();
    }
    if(auto* Graph=Cast<UAnimGraphInstance>(Instance);Graph && Graph->GetRuntime()) for(auto& M:Graph->GetRuntime()->GetMachines())
    { ImGui::TextWrapped("%s | State #%d",FFileHelper::ToUtf8Path(FFileHelper::ToNativePath(M.Asset).filename()).c_str(),M.State); if(M.Transitioning) ImGui::ProgressBar(M.Progress); }
    if(D.Machine) D.Machine->DrawObservationStatus();
}
void FAnimGraphEditorPanel::DrawCanvas(FDocument& D)
{
    ImGui::BeginDisabled(LocationIndex<=0); if(ImGui::SmallButton("<")) Navigate(-1); ImGui::EndDisabled(); ImGui::SameLine();
    ImGui::BeginDisabled(LocationIndex+1>=(int)Locations.size()); if(ImGui::SmallButton(">")) Navigate(1); ImGui::EndDisabled(); ImGui::SameLine();
    if(auto* Parent=FindDocument(D.Parent)) { if(ImGui::SmallButton(Parent->Title().c_str())) Activate(Parent->Id); ImGui::SameLine(); ImGui::TextDisabled(" > "); ImGui::SameLine(); }
    ImGui::TextUnformatted(D.Title().c_str()); ImGui::SameLine();
    if(D.Machine)
    {
        auto& S=*D.Machine;
        std::pair<int,int> Location{S.ActivePose,S.ActiveRule};
        bool Changed=Location!=D.DisplayedGraph;
        if(std::find(D.GraphTabs.begin(),D.GraphTabs.end(),Location)==D.GraphTabs.end()) D.GraphTabs.push_back(Location);
        if(ImGui::BeginTabBar("AnimationGraphTabs",ImGuiTabBarFlags_FittingPolicyScroll))
        {
            std::pair<int,int> Closed{-2,-1};
            for(auto Tab:D.GraphTabs)
            {
                bool Open=true; FString Name="State Machine";
                if(Tab.first>=0) { auto* State=S.FindState(Tab.first); if(!State) continue; Name=State->Name+" / Pose"; }
                else if(Tab.second) { auto* T=S.FindTransition(Tab.second); if(!T) continue; Name=FString(S.StateName(T->From))+" -> "+S.StateName(T->To)+" / Rule"; }
                Name+="###Graph"+std::to_string(Tab.first)+"_"+std::to_string(Tab.second);
                if(ImGui::BeginTabItem(Name.c_str(),Tab==std::pair<int,int>{-1,0}?nullptr:&Open,Changed&&Location==Tab?ImGuiTabItemFlags_SetSelected:0)) { S.ActivePose=Tab.first; S.ActiveRule=Tab.second; ImGui::EndTabItem(); }
                if(!Open) Closed=Tab;
            }
            if(Closed.first!=-2) { std::erase(D.GraphTabs,Closed); if(Closed==std::pair<int,int>{S.ActivePose,S.ActiveRule}) { S.ActivePose=-1; S.ActiveRule=0; } }
            ImGui::EndTabBar();
        }
        D.DisplayedGraph={S.ActivePose,S.ActiveRule};
        S.Focused=ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
        S.UpdateObservations();
        ImGui::TextDisabled("/ State Machine");
        if(S.ActivePose>=0) S.PoseEditor().OnOpenAsset=[this](const FString& P){OpenReference(P);};
        ImGui::BeginDisabled(S.ObservePIE); S.DrawGraph(); ImGui::EndDisabled();
        if(S.ActivePose>=0) { S.PoseEditor().OnOpenAsset=[this](const FString& P){OpenAsset(P);}; D.CanvasFocused=S.PoseEditor().IsCanvasFocused(); }
        else { auto& Session=S.GraphSession(); auto* Old=ax::NodeEditor::GetCurrentEditor(); ax::NodeEditor::SetCurrentEditor(Session.Context); D.CanvasFocused=ax::NodeEditor::IsActive(); ax::NodeEditor::SetCurrentEditor(Old); }
    }
    else { ImGui::TextDisabled("/ AnimGraph"); D.Editor.Draw(D.Graph.Graph,D.Parameters(),D.Mesh()); D.CanvasFocused=D.Editor.IsCanvasFocused(); }
    if(D.CanvasFocused && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) D.SelectedParameter=-1;
    RecordLocation();
}
void FAnimGraphEditorPanel::DrawAssets(FDocument& D)
{
    ImGui::InputTextWithHint("##AssetSearch","Search compatible assets...",D.AssetSearch,sizeof(D.AssetSearch));
    auto* Mesh=UAssetManager::GetAssetByPath<USkeletalMesh>(D.Mesh());
    auto Item=[&](const FString& Key,int Type) {
        if(!Matches(D.AssetSearch,Key)) return;
        auto Name=FFileHelper::ToUtf8Path(FFileHelper::ToNativePath(Key).filename());
        ImGui::PushID(Key.c_str()); ImGui::Selectable(Name.c_str());
        if(ImGui::IsItemHovered()) ImGui::SetTooltip("%s\nDrag into a pose graph",Key.c_str());
        if(ImGui::BeginDragDropSource()) { FAnimationAssetDrag A; A.Type=Type; strncpy_s(A.Key,Key.c_str(),_TRUNCATE); ImGui::SetDragDropPayload(AnimationAssetPayload,&A,sizeof(A)); ImGui::TextUnformatted(Name.c_str()); ImGui::EndDragDropSource(); }
        if(Type==(int)EAnimGraphNodeType::StateMachine && ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) OpenReference(Key);
        ImGui::PopID();
    };
    if(!Mesh) { ImGui::TextDisabled("Choose a preview mesh in Asset Details."); return; }
    for(auto* A:UAssetManager::Get().GetAnimationAssets(Mesh)) Item(A->GetPath(),(int)EAnimGraphNodeType::Sequence);
    for(auto* A:UAssetManager::Get().GetBlendSpaceAssets(Mesh)) Item(A->GetPath(),(int)EAnimGraphNodeType::BlendSpace);
    // Only registered, validated StateMachine assets are offered. Content Drawer imports .animsm.
    for(TObjectIterator<UAnimStateMachineAsset> It;It;++It) if(It->Data.MeshKey==D.Mesh())
    {
        auto Key=It->GetPath();
        if(FFileHelper::ToNativePath(Key).is_relative() && Key.rfind("Assets/",0)!=0 && Key.rfind("Assets\\",0)!=0) Key="Assets/"+Key;
        Item(Key,(int)EAnimGraphNodeType::StateMachine);
    }
}
void FAnimGraphEditorPanel::DrawResults(FDocument& D)
{
    if(D.Diagnostics.empty()) ImGui::TextDisabled(D.CompiledKey.empty()?"Compile to validate this document.":"No compiler messages.");
    for(int I=0;I<(int)D.Diagnostics.size();++I)
    {
        auto& V=D.Diagnostics[I]; ImGui::PushID(I);
        bool Warning=V.Severity==EAnimDiagnosticSeverity::Warning;
        ImGui::PushStyleColor(ImGuiCol_Text,Warning?ImVec4(1,.72f,.25f,1):ImVec4(1,.4f,.35f,1));
        if(ImGui::Selectable(((Warning?"Warning: ":"Error: ")+V.Message).c_str()))
        {
            D.SelectedParameter=-1;
            if(D.Machine)
            {
                auto& S=*D.Machine; S.ActivePose=V.StateId; S.ActiveRule=V.TransitionId;
                if(V.StateId>=0) S.PoseEditor().FocusNode(V.NodeId);
                else if(V.TransitionId) { S.SelectedRuleNode=V.NodeId; S.PendingFocusRule=V.NodeId; auto& Session=S.GraphSession(); Session.Fit=true; }
            }
            else D.Editor.FocusNode(V.NodeId);
            RecordLocation();
        }
        ImGui::PopStyleColor(); ImGui::TextDisabled("%s / %s%s",D.Title().c_str(),V.GraphPath.c_str(),V.NodeId>0?(" / Node #"+std::to_string(V.NodeId)).c_str():"");
        ImGui::PopID();
    }
}
void FAnimGraphEditorPanel::SetupDockspace(ImGuiID DockId,ImVec2 Size)
{
    if(!ResetLayout && ImGui::DockBuilderGetNode(DockId)) return;
    ResetLayout=false;
    ImGui::DockBuilderRemoveNode(DockId); ImGui::DockBuilderAddNode(DockId,ImGuiDockNodeFlags_DockSpace); ImGui::DockBuilderSetNodeSize(DockId,Size);
    ImGuiID Center=DockId;
    ImGuiID Left=ImGui::DockBuilderSplitNode(Center,ImGuiDir_Left,.23f,nullptr,&Center);
    ImGuiID Right=ImGui::DockBuilderSplitNode(Center,ImGuiDir_Right,.22f/.77f,nullptr,&Center);
    ImGuiID Tree=ImGui::DockBuilderSplitNode(Left,ImGuiDir_Down,.46f,nullptr,&Left);
    ImGuiID Bottom=ImGui::DockBuilderSplitNode(Center,ImGuiDir_Down,.25f,nullptr,&Center);
    ImGui::DockBuilderDockWindow("Preview##Animation",Left);
    ImGui::DockBuilderDockWindow("My Animation##Animation",Tree);
    ImGui::DockBuilderDockWindow("Graph##Animation",Center);
    ImGui::DockBuilderDockWindow("Details##Animation",Right);
    ImGui::DockBuilderDockWindow("Compiler Results##Animation",Bottom);
    ImGui::DockBuilderDockWindow("Asset Browser##Animation",Bottom);
    ImGui::DockBuilderFinish(DockId);
}
void FAnimGraphEditorPanel::OnRender()
{
    if(!IsOpen()) { Visible=false; return; }
    OpenPending();
    FAnimationEditorStyle Style;
    ImGui::SetNextWindowSize(ImVec2(1400,850),ImGuiCond_FirstUseEver);
    bool Open=true;
    Visible=ImGui::Begin(GetPanelName(),&Open,ImGuiWindowFlags_NoCollapse|ImGuiWindowFlags_MenuBar);
    Focused=ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
    if(!Open) SetOpen(false);
    ImGuiID DockId=ImGui::GetID("AnimationEditorInnerDockspace");
    ImGuiWindowClass Class; Class.ClassId=ImHashStr("AnimationEditorWindowClass"); Class.DockingAllowUnclassed=false;
    if(Visible)
    {
        if(ImGui::BeginMenuBar())
        {
            if(ImGui::BeginMenu("Window"))
            {
                if(ImGui::MenuItem("Reset Layout")) { ResetLayout=true; for(bool& P:PaneOpen) P=true; SaveUISettings(); }
                const char* Names[]={"Preview","My Animation","Graph","Details","Compiler Results","Asset Browser"};
                for(int I=0;I<6;++I) if(ImGui::MenuItem(Names[I],nullptr,&PaneOpen[I])) SaveUISettings();
                if(ImGui::MenuItem("Asset Settings")) if(auto* D=Current()) { D->SelectedParameter=-1; D->Editor.ClearSelection(); if(D->Machine) { D->Machine->Selected=-1; D->Machine->SelectedTransition=-1; D->Machine->ActivePose=-1; D->Machine->ActiveRule=0; } }
                ImGui::EndMenu();
            }
            if(ImGui::BeginMenu("Edit"))
            {
                auto* D=Current();
                if(ImGui::MenuItem("Undo","Ctrl+Z",false,D&&!D->Undo.empty())) Undo(*D,false);
                if(ImGui::MenuItem("Redo","Ctrl+Y",false,D&&!D->Redo.empty())) Undo(*D,true);
                ImGui::EndMenu();
            }
            ImGui::EndMenuBar();
        }
        if(ImGui::BeginTabBar("AnimationDocuments",ImGuiTabBarFlags_Reorderable|ImGuiTabBarFlags_AutoSelectNewTabs|ImGuiTabBarFlags_FittingPolicyScroll))
        {
            for(auto& Doc:Documents)
            {
                bool TabOpen=true; auto Title=Doc->Title()+(Doc->Dirty?" *":"")+"###AnimationDocument"+std::to_string(Doc->Id);
                if(ImGui::BeginTabItem(Title.c_str(),&TabOpen,Doc->Id==SelectTab?ImGuiTabItemFlags_SetSelected:0))
                { if(Active!=Doc->Id) Activate(Doc->Id); ImGui::EndTabItem(); }
                if(!TabOpen) CloseDocument=Doc->Id;
            }
            ImGui::EndTabBar(); SelectTab=0;
        }
        if(auto* D=Current()) DrawToolbar(*D);
        else ImGui::TextDisabled("Open an .animgraph or .animsm asset from the Content Drawer.");
        if(!Error.empty()) ImGui::TextColored(ImVec4(1,.5f,.3f,1),"%s",Error.c_str());
        auto Size=ImGui::GetContentRegionAvail(); SetupDockspace(DockId,Size);
    }
    ImGui::DockSpace(DockId,ImVec2(0,0),Visible?ImGuiDockNodeFlags_None:ImGuiDockNodeFlags_KeepAliveOnly,&Class);
    ImGui::End();
    if(Visible)
    {
        auto* D=Current();
        auto Begin=[&](const char* Name,int Index) { ImGui::SetNextWindowClass(&Class); return ImGui::Begin(Name,&PaneOpen[Index],ImGuiWindowFlags_NoCollapse); };
        bool PreviousPanes[6]; std::copy(std::begin(PaneOpen),std::end(PaneOpen),PreviousPanes);
        if(PaneOpen[0]) { if(Begin("Preview##Animation",0)) { Focused|=ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows); if(D) D->Preview.Draw(); } else if(D) D->Preview.Hide(); ImGui::End(); } else if(D) D->Preview.Hide();
        if(PaneOpen[1]) { if(Begin("My Animation##Animation",1)) { Focused|=ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows); if(D) DrawTree(*D); } ImGui::End(); }
        if(PaneOpen[2]) { if(Begin("Graph##Animation",2)) { Focused|=ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows); if(D) DrawCanvas(*D); } else if(D) D->CanvasFocused=false; ImGui::End(); } else if(D) D->CanvasFocused=false;
        if(PaneOpen[3]) { if(Begin("Details##Animation",3))
        {
            Focused|=ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
            if(D && ImGui::BeginTabBar("AnimationDetailsTabs"))
            {
                if(ImGui::BeginTabItem("Details")) { DrawDetails(*D); ImGui::EndTabItem(); }
                if(ImGui::BeginTabItem("Preview Parameters")) { DrawPreviewParameters(*D); ImGui::EndTabItem(); }
                if(D->Machine && ImGui::BeginTabItem("Observe")) { D->Machine->DrawObservationControls(); ImGui::EndTabItem(); }
                ImGui::EndTabBar();
            }
        }
        ImGui::End(); }
        if(PaneOpen[4]) { if(Begin("Compiler Results##Animation",4)) { Focused|=ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows); if(D) DrawResults(*D); } ImGui::End(); }
        if(PaneOpen[5]) { if(Begin("Asset Browser##Animation",5)) { Focused|=ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows); if(D) DrawAssets(*D); } ImGui::End(); }
        if(!std::equal(std::begin(PaneOpen),std::end(PaneOpen),PreviousPanes)) SaveUISettings();
        if(D)
        {
            CommitEdits(*D);
            if(Focused && ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_S)) Save(*D);
            if(D->CanvasFocused && !ImGui::GetIO().WantTextInput && !(D->Machine&&D->Machine->ObservePIE))
            {
                if(ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Z)) Undo(*D,ImGui::GetIO().KeyShift);
                if(ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Y)) Undo(*D,true);
            }
        }
    }
    if(CloseDocument)
    {
        auto* Closing=FindDocument(CloseDocument);
        if(Closing && Closing->Dirty) ImGui::OpenPopup("Unsaved Animation Document");
        else
        {
            int Id=CloseDocument; CloseDocument=0; std::erase_if(Documents,[&](auto& D){return D->Id==Id;});
            if(Active==Id) Active=Documents.empty()?0:Documents.back()->Id;
            if(CloseAll) SetOpen(false);
        }
    }
    if(ImGui::BeginPopupModal("Unsaved Animation Document",nullptr,ImGuiWindowFlags_AlwaysAutoResize))
    {
        auto* D=FindDocument(CloseDocument);
        if(D)
        {
            ImGui::Text("Save changes to %s?",D->Title().c_str());
            if(D->Path.empty()) ImGui::InputText("Save path",D->SavePath,sizeof(D->SavePath));
            bool Finish=false;
            if(ImGui::Button("Save")) Finish=Save(*D);
            ImGui::SameLine(); if(ImGui::Button("Discard")) Finish=true;
            ImGui::SameLine(); if(ImGui::Button("Cancel")) { CloseDocument=0; CloseAll=false; ImGui::CloseCurrentPopup(); }
            if(Finish)
            {
                int Id=CloseDocument; CloseDocument=0; std::erase_if(Documents,[&](auto& V){return V->Id==Id;});
                if(Active==Id) Active=Documents.empty()?0:Documents.back()->Id;
                ImGui::CloseCurrentPopup(); if(CloseAll) SetOpen(false);
            }
            if(!Error.empty()) ImGui::TextWrapped("%s",Error.c_str());
        }
        else ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
}
