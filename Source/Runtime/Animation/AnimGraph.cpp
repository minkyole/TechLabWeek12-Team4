#include "EnginePCH.h"
#include "AnimGraph.h"
#include "Animation/AnimStateMachineAsset.h"
#include "Animation/AnimSequence.h"
#include "Animation/BlendSpace.h"
#include "Asset/AssetManager.h"
#include "Components/SkeletalMeshComponent.h"
#include "Core/Misc/FileHelper.h"
#include <map>
#include <functional>
#include <cmath>

FAnimGraphNode* FAnimPoseGraph::Find(int Id)
{
    for (auto& N : Nodes) if (N.Id == Id) return &N;
    return nullptr;
}
const FAnimGraphNode* FAnimPoseGraph::Find(int Id) const
{
    for (const auto& N : Nodes) if (N.Id == Id) return &N;
    return nullptr;
}
int FAnimPoseGraph::NextNodeId() const { int Id = 1; for (auto& N : Nodes) Id = std::max(Id, N.Id + 1); return Id; }
int FAnimPoseGraph::NextLinkId() const { int Id = 1; for (auto& L : Links) Id = std::max(Id, L.Id + 1); return Id; }
namespace
{
int InputCount(EAnimGraphNodeType Type)
{
    return Type == EAnimGraphNodeType::TwoWayBlend ? 2 : Type == EAnimGraphNodeType::Output ? 1 : 0;
}
}
bool FAnimPoseGraph::Validate(FString& Error, int* ErrorNode) const
{
    Error.clear();
    if (ErrorNode) *ErrorNode = -1;
    auto Fail = [&](const char* Message, int Id) { Error = FString(Message) + " (node " + std::to_string(Id) + ")"; if(ErrorNode) *ErrorNode = Id; return false; };
    std::set<int> Ids, LinkIds, Sources;
    std::set<std::pair<int,int>> Inputs;
    int Outputs = 0;
    for (auto& N : Nodes)
    {
        if (N.Id <= 0 || N.Id > 10000000 || !Ids.insert(N.Id).second) return Fail("Invalid or duplicate ID", N.Id);
        if ((int)N.Type < 0 || (int)N.Type > (int)EAnimGraphNodeType::Output) return Fail("Unknown node type", N.Id);
        if (!std::isfinite(N.Value) || !std::isfinite(N.PlayRate) || !std::isfinite(N.X) || !std::isfinite(N.Y)) return Fail("Non-finite value", N.Id);
        if (N.Type == EAnimGraphNodeType::Output) ++Outputs;
    }
    if (Outputs != 1) return Fail("Exactly one Output Pose is required", -1);
    for (auto& L : Links)
    {
        auto* From = Find(L.From); auto* To = Find(L.To);
        if (!From || !To || L.Id <= 0 || !LinkIds.insert(L.Id).second) return Fail("Invalid link ID or endpoint", L.To);
        if (From->Type == EAnimGraphNodeType::Output || L.Input < 0 || L.Input >= InputCount(To->Type)) return Fail("Invalid pose pin", L.To);
        if (!Sources.insert(L.From).second || !Inputs.insert({L.To,L.Input}).second) return Fail("A pose pin can only have one connection", L.To);
    }
    std::map<int,int> Color;
    std::function<bool(int)> Visit = [&](int Id) {
        if (Color[Id] == 1) return Fail("Pose graph cycle", Id);
        if (Color[Id] == 2) return true;
        Color[Id] = 1;
        for (auto& L : Links) if (L.To == Id && !Visit(L.From)) return false;
        Color[Id] = 2; return true;
    };
    for (auto& N : Nodes) if (!Visit(N.Id)) return false;
    // Disconnected authoring nodes may be incomplete; the output's tree must be complete.
    std::function<bool(int)> Complete = [&](int Id) {
        auto* N = Find(Id);
        for (int Pin = 0; Pin < InputCount(N->Type); ++Pin)
        {
            auto It = std::find_if(Links.begin(), Links.end(), [&](auto& L){ return L.To == Id && L.Input == Pin; });
            if (It == Links.end()) return Fail("Required pose input is missing", Id);
            if (!Complete(It->From)) return false;
        }
        return true;
    };
    for (auto& N : Nodes) if (N.Type == EAnimGraphNodeType::Output) return Complete(N.Id);
    return false;
}
bool FAnimPoseGraph::CanConnect(int From, int To, int Input) const
{
    // For interactive linking permit incomplete graphs, but reject cycles and occupied pins.
    if (!Find(From) || !Find(To) || From == To || Find(From)->Type == EAnimGraphNodeType::Output || Input < 0 || Input >= InputCount(Find(To)->Type)) return false;
    for (auto& L : Links) if (L.From == From || (L.To == To && L.Input == Input)) return false;
    std::set<int> Seen;
    std::function<bool(int)> Reaches = [&](int Id) { if(Id == From) return true; if(!Seen.insert(Id).second) return false; for(auto& L:Links) if(L.From == Id && Reaches(L.To)) return true; return false; };
    return !Reaches(To);
}
FAnimPoseGraph FAnimPoseGraph::SinglePlayer(const FString& Asset, bool BS, const FString& Parameter, bool Loop, bool Playing, float Rate)
{
    FAnimPoseGraph G;
    FAnimGraphNode Player; Player.AssetKey = Asset; Player.Type = BS ? EAnimGraphNodeType::BlendSpace : EAnimGraphNodeType::Sequence;
    Player.Parameter = Parameter; Player.Loop = Loop; Player.Playing = Playing; Player.PlayRate = Rate;
    FAnimGraphNode Output; Output.Id = 2; Output.Type = EAnimGraphNodeType::Output; Output.X = 300;
    G.Nodes = {Player, Output}; G.Links.push_back({1,1,2,0}); return G;
}
json AnimPoseGraphJson(const FAnimPoseGraph& G)
{
    json J = {{"Nodes",json::array()},{"Links",json::array()}};
    for(auto& N:G.Nodes) J["Nodes"].push_back({{"Id",N.Id},{"Type",(int)N.Type},{"Asset",N.AssetKey},{"Parameter",N.Parameter},{"Value",N.Value},{"Rate",N.PlayRate},{"Loop",N.Loop},{"Playing",N.Playing},{"X",N.X},{"Y",N.Y}});
    for(auto& L:G.Links) J["Links"].push_back({{"Id",L.Id},{"From",L.From},{"To",L.To},{"Input",L.Input}});
    return J;
}
FAnimPoseGraph ReadAnimPoseGraph(const json& J)
{
    FAnimPoseGraph G;
    for(auto& V:J.at("Nodes"))
    {
        FAnimGraphNode N; N.Id=V.at("Id"); int Type=V.at("Type").get<int>();
        if(Type<0 || Type>(int)EAnimGraphNodeType::Output) throw std::invalid_argument("Unknown pose node type");
        N.Type=(EAnimGraphNodeType)Type;
        N.AssetKey=V.value("Asset",FString{}); N.Parameter=V.value("Parameter",FString{});
        N.Value=V.value("Value",0.0f); N.PlayRate=V.value("Rate",1.0f); N.Loop=V.value("Loop",true); N.Playing=V.value("Playing",true);
        N.X=V.value("X",0.0f); N.Y=V.value("Y",0.0f); G.Nodes.push_back(N);
    }
    for(auto& V:J.at("Links")) G.Links.push_back({V.at("Id"),V.at("From"),V.at("To"),V.at("Input")});
    return G;
}
namespace
{
bool MergeParameters(FAnimParameterSet& Store, const std::vector<FAnimParameter>& Parameters, FString& Error)
{
    std::set<FString> Names;
    for(auto& P:Parameters)
    {
        auto Name=P.Name.ToString();
        if(Name.empty() || !Names.insert(Name).second || !std::isfinite(P.FloatValue)) { Error="Invalid parameter declaration: "+Name; return false; }
        auto It=std::find_if(Store.Values.begin(),Store.Values.end(),[&](auto& V){return V.Name==P.Name;});
        if(It!=Store.Values.end()) { if(It->Type!=P.Type) { Error="Parameter type conflict: "+Name; return false; } continue; }
        if(P.Type!=EAnimParameterType::Float && P.Type!=EAnimParameterType::Bool && P.Type!=EAnimParameterType::Trigger) { Error="Unknown parameter type"; return false; }
        Store.Values.Add(P);
        if(P.Type==EAnimParameterType::Trigger) Store.ConsumeTrigger(P.Name);
    }
    return true;
}
}
bool ValidateAnimPoseGraphAssets(const FAnimPoseGraph& Graph, const FString& MeshKey,
                                const std::vector<FAnimParameter>& Parameters, FString& Error,
                                const FString& OwnerPath)
{
    auto* Mesh=UAssetManager::GetAssetByPath<USkeletalMesh>(MeshKey);
    if(!Mesh) { Error="Missing graph mesh: "+MeshKey; return false; }
    auto Key=[](const FString& P) { return FFileHelper::ToUtf8Path(fs::weakly_canonical(FFileHelper::ToNativePath(P))); };
    std::set<FString> Stack;
    if(!OwnerPath.empty()) Stack.insert(Key(OwnerPath));
    FAnimParameterSet Store;
    if(!MergeParameters(Store,Parameters,Error)) return false;
    std::vector<FString> Bindings;
    std::function<bool(const FAnimPoseGraph&)> Visit=[&](const FAnimPoseGraph& G) {
        if(!G.Validate(Error)) return false;
        std::set<int> Active;
        std::function<void(int)> Mark=[&](int Id) { if(!Active.insert(Id).second) return; for(auto& L:G.Links) if(L.To==Id) Mark(L.From); };
        for(auto& N:G.Nodes) if(N.Type==EAnimGraphNodeType::Output) Mark(N.Id);
        for(auto& N:G.Nodes)
        {
            if(!Active.count(N.Id)) continue;
            if(N.Type==EAnimGraphNodeType::TwoWayBlend || N.Type==EAnimGraphNodeType::BlendSpace)
                if(!N.Parameter.empty()) Bindings.push_back(N.Parameter);
            if(N.Type==EAnimGraphNodeType::StateMachine)
            {
                auto K=Key(N.AssetKey);
                if(Stack.size()>=64 || !Stack.insert(K).second) { Error="Circular StateMachine reference: "+K; return false; }
                auto* Asset=UAssetManager::Get().LoadAnimStateMachine(N.AssetKey,Error);
                if(!Asset) return false;
                const auto& D=Asset->Data;
                if(D.MeshKey!=MeshKey) { Error="StateMachine mesh mismatch at node "+std::to_string(N.Id); return false; }
                if(!MergeParameters(Store,D.Parameters,Error)) return false;
                for(auto& S:D.States)
                {
                    auto Child=S.PoseGraph.Nodes.empty()?FAnimPoseGraph::SinglePlayer(S.AssetKey,S.BlendSpace,S.BlendParameter,S.Options.bLoop,S.Options.bPlaying,S.Options.PlayRate):S.PoseGraph;
                    if(!Visit(Child)) return false;
                }
                Stack.erase(K);
            }
            else if(N.Type==EAnimGraphNodeType::Sequence || N.Type==EAnimGraphNodeType::BlendSpace)
            {
                const UAnimationAsset* Asset=N.Type==EAnimGraphNodeType::Sequence?static_cast<UAnimationAsset*>(UAssetManager::GetAssetByPath<UAnimSequence>(N.AssetKey)):static_cast<UAnimationAsset*>(UAssetManager::GetAssetByPath<UBlendSpace1D>(N.AssetKey));
                if(!Asset || Asset->GetSkeletalMesh()!=Mesh || !std::isfinite(Asset->GetPlayLength()) || Asset->GetPlayLength()<=0) { Error="Missing or incompatible animation at node "+std::to_string(N.Id)+": "+N.AssetKey; return false; }
                if(N.Type==EAnimGraphNodeType::BlendSpace)
                {
                    auto* BS=static_cast<const UBlendSpace1D*>(Asset);
                    if(!BS->GetNumSamples()) { Error="Empty BlendSpace"; return false; }
                    for(auto& S:BS->GetSamples()) if(!S.Animation || S.Animation->GetSkeletalMesh()!=Mesh || !std::isfinite(S.Animation->GetPlayLength()) || S.Animation->GetPlayLength()<=0) { Error="Invalid BlendSpace sample"; return false; }
                }
            }
        }
        return true;
    };
    if(!Visit(Graph)) return false;
    for(auto& Name:Bindings) { float Value; if(!Store.TryGetFloat(FName(Name.c_str()),Value)) { Error="Missing Float parameter: "+Name; return false; } }
    return true;
}
struct FAnimPoseGraphRuntime::FImpl
{
    UAnimInstance& Host;
    FAnimNode_Base* Root = nullptr;
    std::vector<std::unique_ptr<FAnimNode_Base>> Nodes;
    struct FBinding { FString Parameter; float Constant; float* Value; bool Alpha; };
    std::vector<FBinding> Bindings;
    struct FMachine
    {
        FString Asset;
        FAnimStateMachineDefinition Definition;
        FAnimNode_StateMachine* Node = nullptr;
        std::vector<int> StateIds;
    };
    std::vector<std::unique_ptr<FMachine>> Machines;
    std::set<FString> Dependencies;
    explicit FImpl(UAnimInstance& H):Host(H) {}
    void Bind()
    {
        for(auto& B:Bindings) { float Value=B.Constant; if(!B.Parameter.empty()) Host.TryGetFloat(B.Parameter,Value); *B.Value=B.Alpha?std::clamp(Value,0.0f,1.0f):Value; }
    }
    FAnimNode_Base* BuildGraph(const FAnimPoseGraph& Graph, FString& Error, std::set<FString>& Stack)
    {
        if(!Graph.Validate(Error)) return nullptr;
        std::map<int,FAnimNode_Base*> Built;
        std::function<FAnimNode_Base*(int)> BuildNode = [&](int Id)->FAnimNode_Base* {
            if(Built.count(Id)) return Built[Id];
            const auto& N=*Graph.Find(Id);
            if(N.Type==EAnimGraphNodeType::Output)
            {
                for(auto& L:Graph.Links) if(L.To==Id) return BuildNode(L.From);
            }
            std::unique_ptr<FAnimNode_Base> Node;
            if(N.Type==EAnimGraphNodeType::TwoWayBlend)
            {
                auto Blend=std::make_unique<FAnimNode_TwoWayBlend>();
                for(auto& L:Graph.Links) if(L.To==Id) { auto* Child=BuildNode(L.From); if(!Child) return nullptr; (L.Input==0?Blend->A:Blend->B).LinkedNode=Child; }
                Bindings.push_back({N.Parameter,N.Value,&Blend->Alpha,true}); Node=std::move(Blend);
            }
            else if(N.Type==EAnimGraphNodeType::StateMachine)
            {
                FString Key=FFileHelper::ToUtf8Path(fs::weakly_canonical(FFileHelper::ToNativePath(N.AssetKey)));
                if(Stack.size()>=64 || !Stack.insert(Key).second) { Error="Circular or excessive StateMachine nesting: "+Key; return nullptr; }
                auto* Asset=UAssetManager::Get().LoadAnimStateMachine(N.AssetKey,Error);
                if(!Asset) return nullptr;
                auto Data=Asset->Data;
                if(Data.MeshKey!=Host.GetSkeletalMesh()->GetPath()) { Error="StateMachine mesh mismatch: "+Key; return nullptr; }
                if(!MergeParameters(Host.GetParameters(),Data.Parameters,Error) || !Data.CompileRules(Error)) return nullptr;
                auto Machine=std::make_unique<FMachine>(); Machine->Asset=Key;
                TArray<FAnimNode_Base*> Roots;
                for(auto& S:Data.States)
                {
                    auto G=S.PoseGraph.Nodes.empty()?FAnimPoseGraph::SinglePlayer(S.AssetKey,S.BlendSpace,S.BlendParameter,S.Options.bLoop,S.Options.bPlaying,S.Options.PlayRate):S.PoseGraph;
                    auto* Root=BuildGraph(G,Error,Stack); if(!Root) return nullptr;
                    FAnimStateDefinition State; State.Name=FName(S.Name.c_str()); State.RootNodeIndex=Roots.Num(); Machine->Definition.States.Add(State); Roots.Add(Root); Machine->StateIds.push_back(S.Id);
                }
                auto Index=[&](int State){ for(int I=0;I<(int)Data.States.size();++I) if(Data.States[I].Id==State) return I; return -1; };
                Machine->Definition.EntryState=Index(Data.Entry);
                for(auto& C:Data.Conditions) Machine->Definition.Conditions.Add(C);
                for(auto& T:Data.Transitions)
                {
                    FAnimTransitionDefinition V; V.From=Index(T.From); V.To=Index(T.To); V.bFromAnyState=T.From==-1; V.Condition=T.Options.Condition;
                    V.Priority=T.Options.Priority; V.Blend=T.Options.Blend; V.InterruptPolicy=T.Options.InterruptPolicy; V.bRestartIfSameState=T.Options.bRestartIfSameState; V.ConsumeTriggers=T.Options.ConsumeTriggers;
                    Machine->Definition.Transitions.Add(V);
                }
                auto SM=std::make_unique<FAnimNode_StateMachine>(); SM->SharedParameters=&Host.GetParameters();
                if(!SM->Initialize(Machine->Definition,Roots)) { Error="Invalid embedded StateMachine: "+Key; return nullptr; }
                Machine->Node=SM.get(); Machines.push_back(std::move(Machine)); Node=std::move(SM);
                Stack.erase(Key); Dependencies.insert(Key);
            }
            else
            {
                std::unique_ptr<FAnimNode_AssetPlayerBase> Player;
                if(N.Type==EAnimGraphNodeType::Sequence) { auto P=std::make_unique<FAnimNode_SequencePlayer>(); P->Sequence=UAssetManager::GetAssetByPath<UAnimSequence>(N.AssetKey); Player=std::move(P); }
                else { auto P=std::make_unique<FAnimNode_BlendSpacePlayer>(); P->BlendSpace=UAssetManager::GetAssetByPath<UBlendSpace1D>(N.AssetKey); Bindings.push_back({N.Parameter,N.Value,&P->X,false}); Player=std::move(P); }
                auto* Asset=Player->GetAnimationAsset();
                if(!Asset || Asset->GetSkeletalMesh()!=Host.GetSkeletalMesh() || !std::isfinite(Asset->GetPlayLength()) || Asset->GetPlayLength()<=0) { Error="Missing or incompatible animation at node "+std::to_string(Id)+": "+N.AssetKey; return nullptr; }
                if(N.Type==EAnimGraphNodeType::BlendSpace)
                {
                    auto* BS=static_cast<const UBlendSpace1D*>(Asset);
                    if(BS->GetNumSamples()==0) { Error="Empty BlendSpace"; return nullptr; }
                    for(auto& Sample:BS->GetSamples()) if(!Sample.Animation || Sample.Animation->GetSkeletalMesh()!=Host.GetSkeletalMesh() || Sample.Animation->GetPlayLength()<=0) { Error="Invalid BlendSpace sample"; return nullptr; }
                }
                Player->bLoop=N.Loop; Player->bPlaying=N.Playing; Player->PlayRate=N.PlayRate; Node=std::move(Player);
            }
            auto* Result=Node.get(); if(!Result) return nullptr;
            Nodes.push_back(std::move(Node)); Built[Id]=Result; return Result;
        };
        for(auto& N:Graph.Nodes) if(N.Type==EAnimGraphNodeType::Output)
        {
            auto* Result=BuildNode(N.Id);
            if(Result && Built.size()+1<Graph.Nodes.size()) HTR_LOG(Warning,"[AnimGraph] Disconnected nodes are excluded from execution.");
            return Result;
        }
        return nullptr;
    }
};
FAnimPoseGraphRuntime::FAnimPoseGraphRuntime(UAnimInstance& Host):Impl(std::make_unique<FImpl>(Host)) {}
FAnimPoseGraphRuntime::~FAnimPoseGraphRuntime() = default;
bool FAnimPoseGraphRuntime::Build(const FAnimPoseGraph& Graph, FString& Error, std::set<FString>* ExistingStack)
{
    Impl=std::make_unique<FImpl>(Impl->Host);
    std::set<FString> Stack;
    Impl->Root=Impl->BuildGraph(Graph,Error,ExistingStack?*ExistingStack:Stack);
    if(!Impl->Root) return false;
    for(auto& B:Impl->Bindings) { float V; if(!B.Parameter.empty() && !Impl->Host.TryGetFloat(B.Parameter,V)) { Error="Missing Float parameter: "+B.Parameter; Impl->Root=nullptr; return false; } }
    Impl->Bind(); return true;
}
void FAnimPoseGraphRuntime::OnEnter()
{
    Impl->Bind();
    if(Impl->Root) Impl->Root->OnEnter();
    // Prepare sampling data without evaluating transitions in inactive machines.
    FAnimationUpdateContext Context; Context.AnimInstance=nullptr; Context.DeltaSeconds=0; Context.BlendWeight=0;
    for(auto& Node:Impl->Nodes) if(auto* Player=dynamic_cast<FAnimNode_AssetPlayerBase*>(Node.get())) Player->Update(Context);
}
void FAnimPoseGraphRuntime::OnExit() { if(Impl->Root) Impl->Root->OnExit(); }
void FAnimPoseGraphRuntime::Update(const FAnimationUpdateContext& C) { Impl->Bind(); if(Impl->Root) Impl->Root->Update(C); }
void FAnimPoseGraphRuntime::Evaluate(FPoseContext& O) { if(Impl->Root) Impl->Root->Evaluate(O); else O.ResetToRefPose(); }
bool FAnimPoseGraphRuntime::IsFinished() const { return Impl->Root && Impl->Root->IsFinished(); }
const std::set<FString>& FAnimPoseGraphRuntime::GetDependencies() const { return Impl->Dependencies; }
std::vector<FAnimPoseGraphRuntime::FMachineDebug> FAnimPoseGraphRuntime::GetMachines() const
{
    std::vector<FMachineDebug> Result;
    for(auto& M:Impl->Machines) { int I=M->Node->GetCurrentState(); Result.push_back({M->Asset,I>=0 && I<(int)M->StateIds.size()?M->StateIds[I]:-1,M->Node->GetTransitionProgress(),M->Node->IsTransitioning()}); }
    return Result;
}
bool FAnimGraphAssetData::Validate(FString& Error) const
{
    if(!UAssetManager::GetAssetByPath<USkeletalMesh>(MeshKey)) { Error="Choose a skeletal mesh"; return false; }
    FAnimParameterSet Store;
    return MergeParameters(Store,Parameters,Error) && ValidateAnimPoseGraphAssets(Graph,MeshKey,Parameters,Error);
}
UAnimGraphInstance::~UAnimGraphInstance() { NativeUnInitializeAnimation(); }
void UAnimGraphInstance::NativeInitializeAnimation()
{
    FString Error;
    auto Path=GetSkeletalMeshComponent()->GetAnimGraphAssetPath();
    if(Path.empty()) return; // Private editor snapshot is configured after initialization.
    auto* Asset=UAssetManager::Get().LoadAnimGraph(Path,Error);
    if(!Asset || !Configure(Asset->Data)) { if(!Error.empty()) LastError=Error; HTR_LOG(Error,"[AnimGraph] {}",LastError); }
}
void UAnimGraphInstance::NativeUnInitializeAnimation() { RootNode=nullptr; Runtime.reset(); Script.Reset(); GetParameters().Values.Reset(); }
bool UAnimGraphInstance::Configure(const FAnimGraphAssetData& Data, bool EnableScript)
{
    auto Copy=Data;
    EndAllNotifyStates(); ConsumeRootMotion(); NativeUnInitializeAnimation(); LastError.clear(); Paused=false;
    if(!Copy.Validate(LastError) || !GetSkeletalMesh() || GetSkeletalMesh()->GetPath()!=Copy.MeshKey) { if(LastError.empty()) LastError="Graph mesh mismatch"; return false; }
    if(!MergeParameters(GetParameters(),Copy.Parameters,LastError)) return false;
    auto Built=std::make_unique<FAnimPoseGraphRuntime>(*this);
    if(!Built->Build(Copy.Graph,LastError)) return false;
    if(EnableScript && !Copy.UpdateScript.empty() && !Script.Load(Copy.UpdateScript,false,true,LastError)) return false;
    Snapshot=std::move(Copy); Runtime=std::move(Built); RootNode=Runtime.get(); Runtime->OnEnter();
    return true;
}
void UAnimGraphInstance::UpdateAnimation(float DeltaSeconds) { if(!Paused) UAnimInstance::UpdateAnimation(DeltaSeconds); }
void UAnimGraphInstance::NativeUpdateAnimation(float DeltaSeconds)
{
    if(!Script.Update.valid()) return;
    bool Failed=false;
    { auto Result=Script.Update(this,DeltaSeconds); if(!Result.valid()) { sol::error E=Result; LastError=E.what(); HTR_LOG(Error,"[AnimGraph] Lua update failed: {}",LastError); Failed=true; } }
    if(Failed) Script.Update={};
}
