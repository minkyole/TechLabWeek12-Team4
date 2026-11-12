#include "EnginePCH.h"
#include "AnimPoseGraphEditor.h"
#include "imgui_internal.h"
#include "Asset/AssetManager.h"
#include "Animation/AnimSequence.h"
#include "Animation/BlendSpace.h"
#include "Render/Mesh.h"
#include "AnimationEditorStyle.h"
#include "Core/Misc/FileHelper.h"
#include <map>
#include <cmath>
#include <algorithm>
namespace ed = ax::NodeEditor;
namespace
{
const char* Names[] = {"Sequence Player","BlendSpace Player","Two-way Blend","State Machine","Output Pose"};
uintptr_t Pin(int Node, int Slot) { return uintptr_t(Node)*4+Slot; }
int Inputs(EAnimGraphNodeType Type) { return Type==EAnimGraphNodeType::TwoWayBlend?2:Type==EAnimGraphNodeType::Output?1:0; }
}
bool AnimGraphTextEdit(const char* Label, FString& Value)
{
    char Buffer[1024]; strncpy_s(Buffer,Value.c_str(),_TRUNCATE);
    if(!ImGui::InputText(Label,Buffer,sizeof(Buffer))) return false;
    Value=Buffer; return true;
}
bool DrawAnimGraphParameters(std::vector<FAnimParameter>& Parameters)
{
    bool Dirty=false; int Remove=-1;
    for(int I=0;I<(int)Parameters.size();++I)
    {
        auto& P=Parameters[I]; ImGui::PushID(I);
        FString Name=P.Name.ToString(); if(AnimGraphTextEdit("Name",Name)) { P.Name=FName(Name.c_str()); Dirty=true; }
        int Type=(int)P.Type; if(ImGui::Combo("Type",&Type,"Bool\0Float\0Trigger\0")) { P.Type=(EAnimParameterType)Type; Dirty=true; }
        if(P.Type==EAnimParameterType::Float) Dirty|=ImGui::InputFloat("Default",&P.FloatValue);
        if(P.Type==EAnimParameterType::Bool) Dirty|=ImGui::Checkbox("Default",&P.BoolValue);
        if(ImGui::Button("Remove parameter")) Remove=I;
        ImGui::Separator(); ImGui::PopID();
    }
    if(Remove>=0) { Parameters.erase(Parameters.begin()+Remove); Dirty=true; }
    if(ImGui::Button("Add parameter")) { FAnimParameter P; P.Name=FName(("Parameter"+std::to_string(Parameters.size()+1)).c_str()); Parameters.push_back(P); Dirty=true; }
    return Dirty;
}
FAnimPoseGraphEditor::~FAnimPoseGraphEditor() { Reset(); }
void FAnimPoseGraphEditor::Reset() { if(Context) { if(ed::GetCurrentEditor()==Context) ed::SetCurrentEditor(nullptr); ed::DestroyEditor(Context); } Context=nullptr; Placed.clear(); Selected=0; Fit=true; }
bool FAnimPoseGraphEditor::Draw(FAnimPoseGraph& Graph, const std::vector<FAnimParameter>& Parameters, const FString& MeshKey)
{
    bool Dirty=false;
    if(!Context) { ed::Config Config; Config.SettingsFile=nullptr; Config.UserPointer=this;
        Config.SaveSettings=[](const char* Data,size_t Size,ed::SaveReasonFlags,void* User) { auto* Editor=static_cast<FAnimPoseGraphEditor*>(User); return !Editor->SettingsPath.empty() && FFileHelper::SaveStringToFile(FString(Data,Size),Editor->SettingsPath); };
        Config.LoadSettings=[](char* Data,void* User)->size_t { auto* Editor=static_cast<FAnimPoseGraphEditor*>(User); FString Text; if(Editor->SettingsPath.empty() || !FFileHelper::LoadFileToString(Text,Editor->SettingsPath)) return 0; if(Data) memcpy(Data,Text.data(),Text.size()); return Text.size(); };
        Config.NavigateButtonIndex=2; Config.ContextMenuButtonIndex=1; Context=ed::CreateEditor(&Config); if(LoadView) Fit=false; }
    if(Graph.Nodes.empty()) { FAnimGraphNode N; N.Type=EAnimGraphNodeType::Output; N.X=500; Graph.Nodes.push_back(N); Dirty=true; }
    auto* Previous=ed::GetCurrentEditor(); ed::SetCurrentEditor(Context);
    FAnimationGraphStyle Style;
    if(ImGui::SmallButton("Frame All")) Fit=true;
    ImGui::SameLine(); if(ImGui::SmallButton("Frame Selection")) ed::NavigateToSelection();
    FString Error; int ErrorNode=0; Graph.Validate(Error,&ErrorNode);
    ImGui::SameLine(); ImGui::TextDisabled("RMB: Actions | MMB: Pan | F: Selection | Home: All");
    const auto CanvasOrigin=ImGui::GetCursorScreenPos();
    const ImVec2 CanvasSize(ImGui::GetContentRegionAvail().x,std::max(100.0f,ImGui::GetContentRegionAvail().y));
    ed::EnableShortcuts(!ImGui::GetIO().WantTextInput);
    ed::Begin("PoseGraphCanvas",CanvasSize);
    CanvasFocused=ed::IsActive() && !ImGui::GetIO().WantTextInput;
    if(PendingClear) { ed::ClearSelection(); PendingClear=false; }
    FString HoveredAsset;
    int HoveredAssetNode=0;
    for(auto& N:Graph.Nodes)
    {
        if(Placed.insert(N.Id).second) ed::SetNodePosition(ed::NodeId(N.Id),ImVec2(N.X,N.Y));
        if(N.Id==ErrorNode) ed::PushStyleColor(ed::StyleColor_NodeBorder,ImVec4(1,.2f,.2f,1));
        ed::BeginNode(ed::NodeId(N.Id));
        ImGui::PushID(N.Id);
        const auto Header=ImGui::GetCursorScreenPos(); const float NodeLeft=ImGui::GetCursorPosX();
        ImU32 Color=N.Type==EAnimGraphNodeType::Output?IM_COL32(120,43,47,255):N.Type==EAnimGraphNodeType::StateMachine?IM_COL32(42,76,118,255):N.Type==EAnimGraphNodeType::TwoWayBlend?IM_COL32(35,95,101,255):IM_COL32(53,91,54,255);
        const float Width=std::max(190.0f,ImGui::CalcTextSize(Names[(int)N.Type]).x+20);
        ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(Header.x-5,Header.y-4),ImVec2(Header.x+Width,Header.y+25),Color,4);
        ImGui::TextUnformatted(Names[(int)N.Type]); ImGui::Dummy(ImVec2(Width,7));
        if(!N.AssetKey.empty())
        {
            auto Name=fs::path(N.AssetKey).stem().string(); ImGui::TextUnformatted(Name.c_str());
            if(ImGui::IsItemHovered()) { HoveredAsset=N.AssetKey; HoveredAssetNode=N.Id; }
        }
        if(!N.Parameter.empty()) ImGui::TextColored(ImVec4(.45f,.85f,.38f,1),"%s",N.Parameter.c_str());
        else if(N.Type==EAnimGraphNodeType::TwoWayBlend || N.Type==EAnimGraphNodeType::BlendSpace)
        { ImGui::SetNextItemWidth(125); Dirty|=ImGui::DragFloat(N.Type==EAnimGraphNodeType::TwoWayBlend?"Alpha":"Axis",&N.Value,.01f); }
        auto DrawPin=[&](int Slot,ed::PinKind Kind,const char* Label) {
            if(Kind==ed::PinKind::Output) ImGui::SetCursorPosX(NodeLeft+Width-55);
            ed::BeginPin(ed::PinId(Pin(N.Id,Slot)),Kind);
            auto P=ImGui::GetCursorScreenPos(); ImGui::Dummy(ImVec2(14,14));
            auto* Draw=ImGui::GetWindowDrawList(); Draw->AddCircle(ImVec2(P.x+7,P.y+7),5,IM_COL32(235,235,240,255),12,2);
            bool Connected=std::any_of(Graph.Links.begin(),Graph.Links.end(),[&](auto& L){return Slot==0?L.From==N.Id:L.To==N.Id&&L.Input==Slot-1;});
            if(Connected) Draw->AddCircleFilled(ImVec2(P.x+7,P.y+7),3,IM_COL32(235,235,240,255));
            ImGui::SameLine(); ImGui::TextUnformatted(Label); ed::EndPin();
        };
        for(int I=0;I<Inputs(N.Type);++I) DrawPin(I+1,ed::PinKind::Input,N.Type==EAnimGraphNodeType::TwoWayBlend?(I==0?"A":"B"):"Result");
        if(N.Type!=EAnimGraphNodeType::Output) DrawPin(0,ed::PinKind::Output,"Pose");
        ImGui::PopID();
        ed::EndNode(); if(N.Id==ErrorNode) ed::PopStyleColor();
        auto Position=ed::GetNodePosition(ed::NodeId(N.Id)); if(std::abs(Position.x-std::floor(N.X))>.01f || std::abs(Position.y-std::floor(N.Y))>.01f) { N.X=Position.x; N.Y=Position.y; Dirty=true; }
    }
    for(auto& L:Graph.Links) ed::Link(ed::LinkId(L.Id),ed::PinId(Pin(L.From,0)),ed::PinId(Pin(L.To,L.Input+1)),ImVec4(.9f,.9f,.93f,1),2.5f);
    if(ed::BeginCreate())
    {
        ed::PinId A,B;
        if(ed::QueryNewLink(&A,&B))
        {
            auto From=A.Get(),To=B.Get(); if(From%4) std::swap(From,To);
            bool Valid=From && To && From%4==0 && To%4>0 && Graph.CanConnect(int(From/4),int(To/4),int(To%4)-1);
            if(Valid) { if(ed::AcceptNewItem()) { Graph.Links.push_back({Graph.NextLinkId(),int(From/4),int(To/4),int(To%4)-1}); Dirty=true; } }
            else ed::RejectNewItem(ImVec4(1,.2f,.2f,1));
        }
        ed::PinId NewPin;
        if(ed::QueryNewNode(&NewPin) && ed::AcceptNewItem())
        {
            SearchSelection=0; CreateFromPin=NewPin.Get(); CreationPosition=ed::ScreenToCanvas(ImGui::GetMousePos()); Search[0]=0;
            ed::Suspend(); ImGui::OpenPopup("Animation Actions"); ed::Resume();
        }
    }
    ed::EndCreate();
    if(ed::BeginDelete())
    {
        ed::LinkId Link; while(ed::QueryDeletedLink(&Link)) if(ed::AcceptDeletedItem()) { std::erase_if(Graph.Links,[&](auto& L){return L.Id==(int)Link.Get();}); Dirty=true; }
        ed::NodeId Node; while(ed::QueryDeletedNode(&Node))
        {
            auto* N=Graph.Find((int)Node.Get());
            if(!N || N->Type==EAnimGraphNodeType::Output) { ed::RejectDeletedItem(); continue; }
            if(ed::AcceptDeletedItem()) { int Id=N->Id; std::erase_if(Graph.Links,[&](auto& L){return L.From==Id||L.To==Id;}); std::erase_if(Graph.Nodes,[&](auto& V){return V.Id==Id;}); Placed.erase(Id); if(Selected==Id) Selected=0; Dirty=true; }
        }
    }
    ed::EndDelete();
    if(!PendingSelection.empty()) { ed::ClearSelection(); for(int Id:PendingSelection) ed::SelectNode(ed::NodeId(Id),true); PendingSelection.clear(); }
    if(ed::BeginShortcut())
    {
        bool Copy=ed::AcceptCopy(),Paste=ed::AcceptPaste(),Duplicate=ed::AcceptDuplicate();
        Dirty|=Clipboard(Graph,Copy,Paste,Duplicate);
    }
    ed::EndShortcut();
    if(ed::HasSelectionChanged()) { ed::NodeId Node; Selected=ed::GetSelectedNodes(&Node,1)?(int)Node.Get():0; }
    if(CanvasFocused)
    {
        if(ImGui::IsKeyPressed(ImGuiKey_F)) ed::NavigateToSelection();
        if(ImGui::IsKeyPressed(ImGuiKey_Home)) Fit=true;
    }
    if(PendingFocus && Graph.Find(PendingFocus)) { ed::ClearSelection(); ed::SelectNode(ed::NodeId(PendingFocus)); ed::NavigateToSelection(); PendingFocus=0; }
    auto Double=ed::GetDoubleClickedNode();
    FString OpenAsset;
    if(auto* N=Graph.Find((int)Double.Get()); N && N->Type==EAnimGraphNodeType::StateMachine) OpenAsset=N->AssetKey;
    ed::Suspend();
    ed::NodeId MenuNode; ed::PinId MenuPin;
    if(ed::ShowNodeContextMenu(&MenuNode)) { ContextNode=(int)MenuNode.Get(); ImGui::OpenPopup("Pose Node Actions"); }
    if(ed::ShowPinContextMenu(&MenuPin)) { ContextPin=MenuPin.Get(); ImGui::OpenPopup("Pose Pin Actions"); }
    if(ed::ShowBackgroundContextMenu()) { SearchSelection=0; CreationPosition=ed::ScreenToCanvas(ImGui::GetMousePos()); CreateFromPin=0; Search[0]=0; ImGui::OpenPopup("Animation Actions"); }
    if(ImGui::BeginPopup("Pose Pin Actions"))
    {
        if(ImGui::MenuItem("Break Connection(s)")) { int Id=(int)(ContextPin/4),Slot=(int)(ContextPin%4); std::erase_if(Graph.Links,[&](auto& L){return Slot==0?L.From==Id:L.To==Id&&L.Input==Slot-1;}); Dirty=true; }
        ImGui::EndPopup();
    }
    if(ImGui::BeginPopup("Pose Node Actions"))
    {
        auto* N=Graph.Find(ContextNode);
        if(N && N->Type!=EAnimGraphNodeType::Output)
        {
            auto Type=N->Type; auto Asset=N->AssetKey;
            if(ImGui::MenuItem("Duplicate","Ctrl+D")) { ed::ClearSelection(); ed::SelectNode(ed::NodeId(N->Id)); Dirty|=Clipboard(Graph,false,false,true); }
            if(ImGui::MenuItem("Delete","Delete")) ed::DeleteNode(ed::NodeId(ContextNode));
            if(Type==EAnimGraphNodeType::StateMachine && ImGui::MenuItem("Open Referenced Asset")) OpenAsset=Asset;
        }
        ImGui::EndPopup();
    }
    if(ImGui::BeginPopup("Animation Actions"))
    {
        if(ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
        if(ImGui::InputTextWithHint("##Search","Search animation actions...",Search,sizeof(Search))) SearchSelection=0;
        if(ImGui::IsKeyPressed(ImGuiKey_DownArrow)) ++SearchSelection;
        if(ImGui::IsKeyPressed(ImGuiKey_UpArrow)) SearchSelection=std::max(0,SearchSelection-1);
        ImGui::SeparatorText("Animation / Pose");
        int MatchIndex=0;
        for(int Type=0;Type<4;++Type)
        {
            ImGuiTextFilter Filter(Search);
            if(!Filter.PassFilter(Names[Type])) continue;
            if(CreateFromPin%4==0 && CreateFromPin && Inputs((EAnimGraphNodeType)Type)==0) continue;
            int ThisIndex=MatchIndex++;
            bool Choose=ImGui::Selectable(Names[Type],ThisIndex==SearchSelection);
            if((ThisIndex==SearchSelection && ImGui::IsKeyPressed(ImGuiKey_Enter)) || Choose)
            {
                FAnimGraphNode N; N.Id=Graph.NextNodeId(); N.Type=(EAnimGraphNodeType)Type; N.X=CreationPosition.x; N.Y=CreationPosition.y;
                Graph.Nodes.push_back(N);
                if(CreateFromPin)
                {
                    int From=CreateFromPin%4==0?int(CreateFromPin/4):N.Id;
                    int To=CreateFromPin%4==0?N.Id:int(CreateFromPin/4);
                    int Slot=CreateFromPin%4==0?0:int(CreateFromPin%4)-1;
                    if(Graph.CanConnect(From,To,Slot)) Graph.Links.push_back({Graph.NextLinkId(),From,To,Slot});
                }
                Selected=N.Id; PendingSelection={N.Id}; Dirty=true; CreateFromPin=0; ImGui::CloseCurrentPopup();
            }
        }
        if(MatchIndex>0) SearchSelection=std::min(SearchSelection,MatchIndex-1);
        ImGui::EndPopup();
    }
    ed::Resume();
    if(Fit) { ed::NavigateToContent(0); Fit=false; }
    ed::End();
    // Canvas::End restores ImGui's mouse position from graph space to screen space.
    // Tooltips are separate windows and must be created only after that restoration.
    if(HoveredAssetNode) ImGui::SetTooltip("%s\nNode #%d",HoveredAsset.c_str(),HoveredAssetNode);
    if(ImGui::BeginDragDropTargetCustom(ImRect(CanvasOrigin,ImVec2(CanvasOrigin.x+CanvasSize.x,CanvasOrigin.y+CanvasSize.y)),ImGui::GetID("AnimationCanvasDrop")))
    {
        if(auto* Payload=ImGui::AcceptDragDropPayload(AnimationAssetPayload))
        {
            if(Payload->DataSize==sizeof(FAnimationAssetDrag))
            {
                auto& Asset=*static_cast<const FAnimationAssetDrag*>(Payload->Data); auto P=ed::ScreenToCanvas(ImGui::GetMousePos());
                FAnimGraphNode N; N.Id=Graph.NextNodeId(); N.Type=(EAnimGraphNodeType)Asset.Type; N.AssetKey=Asset.Key; N.X=P.x; N.Y=P.y; Graph.Nodes.push_back(N); PendingSelection={N.Id}; Dirty=true;
            }
        }
        ImGui::EndDragDropTarget();
    }
    Style.End(); ed::SetCurrentEditor(Previous);
    if(!OpenAsset.empty() && OnOpenAsset) OnOpenAsset(OpenAsset);
    return Dirty;
}
bool FAnimPoseGraphEditor::Clipboard(FAnimPoseGraph& Graph,bool Copy,bool Paste,bool Duplicate)
{
    static FAnimPoseGraph Buffer;
    if(Copy || Duplicate)
    {
        Buffer={}; std::vector<ed::NodeId> Selection(ed::GetSelectedObjectCount());
        int Count=ed::GetSelectedNodes(Selection.data(),(int)Selection.size()); std::set<int> Ids;
        for(int I=0;I<Count;++I) if(auto* N=Graph.Find((int)Selection[I].Get());N && N->Type!=EAnimGraphNodeType::Output) { Buffer.Nodes.push_back(*N); Ids.insert(N->Id); }
        for(auto& L:Graph.Links) if(Ids.count(L.From)&&Ids.count(L.To)) Buffer.Links.push_back(L);
    }
    if(!(Paste||Duplicate) || Buffer.Nodes.empty()) return false;
    std::map<int,int> Remap; int Id=Graph.NextNodeId();
    ed::ClearSelection(); PendingSelection.clear();
    for(auto N:Buffer.Nodes) { int Old=N.Id; N.Id=Id++; N.X+=40; N.Y+=40; Remap[Old]=N.Id; Graph.Nodes.push_back(N); PendingSelection.push_back(N.Id); Selected=N.Id; }
    for(auto L:Buffer.Links) { L.Id=Graph.NextLinkId(); L.From=Remap[L.From]; L.To=Remap[L.To]; Graph.Links.push_back(L); }
    return true;
}
bool FAnimPoseGraphEditor::DrawProperties(FAnimPoseGraph& Graph, const std::vector<FAnimParameter>& Parameters, const FString& MeshKey)
{
    auto* N=Graph.Find(Selected); if(!N) { ImGui::TextDisabled("Select a pose node."); return false; }
    bool Dirty=false; ImGui::Text("%s #%d",Names[(int)N->Type],N->Id);
    auto* Mesh=UAssetManager::GetAssetByPath<USkeletalMesh>(MeshKey);
    if(N->Type==EAnimGraphNodeType::Sequence || N->Type==EAnimGraphNodeType::BlendSpace)
    {
        if(ImGui::BeginCombo("Animation",N->AssetKey.c_str()))
        {
            if(N->Type==EAnimGraphNodeType::Sequence) { for(auto* A:UAssetManager::Get().GetAnimationAssets(Mesh)) if(ImGui::Selectable(A->GetPath().c_str(),N->AssetKey==A->GetPath())) { N->AssetKey=A->GetPath(); Dirty=true; } }
            else { for(auto* A:UAssetManager::Get().GetBlendSpaceAssets(Mesh)) if(ImGui::Selectable(A->GetPath().c_str(),N->AssetKey==A->GetPath())) { N->AssetKey=A->GetPath(); Dirty=true; } }
            ImGui::EndCombo();
        }
        Dirty|=AnimGraphTextEdit("Asset key",N->AssetKey);
        Dirty|=ImGui::Checkbox("Loop",&N->Loop); Dirty|=ImGui::Checkbox("Playing",&N->Playing); Dirty|=ImGui::InputFloat("Play rate",&N->PlayRate);
    }
    if(N->Type==EAnimGraphNodeType::StateMachine)
    {
        Dirty|=AnimGraphTextEdit("StateMachine (.animsm)",N->AssetKey);
        ImGui::TextWrapped("Parameters map by name and type. Only the owning graph's UpdateScript runs; the referenced StateMachine's script is disabled.");
    }
    if(N->Type==EAnimGraphNodeType::TwoWayBlend || N->Type==EAnimGraphNodeType::BlendSpace)
    {
        if(ImGui::BeginCombo("Float parameter",N->Parameter.empty()?"Constant":N->Parameter.c_str()))
        {
            if(ImGui::Selectable("Constant",N->Parameter.empty())) { N->Parameter.clear(); Dirty=true; }
            for(auto& P:Parameters) if(P.Type==EAnimParameterType::Float && ImGui::Selectable(P.Name.ToString().c_str(),N->Parameter==P.Name.ToString())) { N->Parameter=P.Name.ToString(); Dirty=true; }
            ImGui::EndCombo();
        }
        // Embedded StateMachines may introduce a Float not yet declared at this level.
        Dirty|=AnimGraphTextEdit("Parameter name",N->Parameter);
        if(N->Parameter.empty()) Dirty|=ImGui::InputFloat(N->Type==EAnimGraphNodeType::TwoWayBlend?"Alpha":"Axis",&N->Value);
    }
    return Dirty;
}

void FAnimPoseGraphEditor::SetSettingsKey(const FString& Key)
{
    if(Key.empty() || !SettingsPath.empty()) return;
    std::error_code EC; fs::create_directories("Saved/AnimationEditor",EC);
    SettingsPath="Saved/AnimationEditor/"+std::to_string(std::hash<FString>{}(Key))+".json";
    LoadView=fs::exists(FFileHelper::ToNativePath(SettingsPath),EC);
}
