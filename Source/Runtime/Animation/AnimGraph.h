#pragma once
#include "Animation/AnimInstance.h"
#include "Animation/AnimationScriptContext.h"
#include "Asset/RenderAsset.h"
#include <vector>
#include <memory>
#include <set>
#include "json.hpp"
using json = nlohmann::json;

enum class EAnimGraphNodeType : uint8 { Sequence, BlendSpace, TwoWayBlend, StateMachine, Output };
enum class EAnimDiagnosticSeverity : uint8 { Warning, Error };
// Location is optional for legacy string-only diagnostics.
struct FAnimGraphDiagnostic
{
    FString AssetPath, GraphPath, Message;
    int NodeId=0, LinkId=0, StateId=-1, TransitionId=0;
    EAnimDiagnosticSeverity Severity=EAnimDiagnosticSeverity::Error;
};
struct FAnimGraphNode
{
    int Id = 1;
    EAnimGraphNodeType Type = EAnimGraphNodeType::Sequence;
    FString AssetKey, Parameter;
    float Value = 0, PlayRate = 1, X = 0, Y = 0;
    bool Loop = true, Playing = true;
};
struct FAnimGraphLink { int Id = 1, From = 0, To = 0, Input = 0; };
struct FAnimPoseGraph
{
    std::vector<FAnimGraphNode> Nodes;
    std::vector<FAnimGraphLink> Links;
    FAnimGraphNode* Find(int Id);
    const FAnimGraphNode* Find(int Id) const;
    int NextNodeId() const;
    int NextLinkId() const;
    bool Validate(FString& Error, int* ErrorNode = nullptr) const;
    bool CanConnect(int From, int To, int Input) const;
    static FAnimPoseGraph SinglePlayer(const FString& Asset, bool BlendSpace, const FString& Parameter,
                                     bool Loop, bool Playing, float Rate);
};
// JSON helpers are shared by .animgraph and version 2 .animsm files.
json AnimPoseGraphJson(const FAnimPoseGraph& Graph);
FAnimPoseGraph ReadAnimPoseGraph(const json& Data);
bool ValidateAnimPoseGraphAssets(const FAnimPoseGraph& Graph, const FString& MeshKey,
                                const std::vector<FAnimParameter>& Parameters, FString& Error,
                                const FString& OwnerPath = {});
struct FAnimGraphAssetData
{
    FString MeshKey, UpdateScript;
    std::vector<FAnimParameter> Parameters;
    FAnimPoseGraph Graph;
    bool Validate(FString& Error) const;
};
class UAnimGraphAsset : public URenderAsset
{
    DECLARE_CLASS(UAnimGraphAsset, URenderAsset)
public:
    FAnimGraphAssetData Data;
};

// Owns all nodes and definitions. Embedded state machines share the host instance's
// parameter store and notify/root-motion context, never a nested UAnimInstance.
class FAnimPoseGraphRuntime : public FAnimNode_Base
{
public:
    explicit FAnimPoseGraphRuntime(UAnimInstance& Host);
    ~FAnimPoseGraphRuntime() override;
    bool Build(const FAnimPoseGraph& Graph, FString& Error, std::set<FString>* Stack = nullptr);
    void OnEnter() override;
    void OnExit() override;
    void Update(const FAnimationUpdateContext& Context) override;
    void Evaluate(FPoseContext& Output) override;
    bool IsFinished() const override;
    struct FMachineDebug { FString Asset; int State = -1; float Progress = 0; bool Transitioning = false; };
    std::vector<FMachineDebug> GetMachines() const;
    const std::set<FString>& GetDependencies() const;
private:
    struct FImpl;
    std::unique_ptr<FImpl> Impl;
};

class UAnimGraphInstance : public UAnimInstance
{
    DECLARE_CLASS(UAnimGraphInstance, UAnimInstance)
public:
    ~UAnimGraphInstance() override;
    bool Configure(const FAnimGraphAssetData& Data, bool EnableScript = true);
    const FString& GetLastError() const { return LastError; }
    bool IsGraphReady() const { return Runtime != nullptr; }
    void SetPaused(bool Value) { Paused = Value; }
    bool IsPaused() const { return Paused; }
    void UpdateAnimation(float DeltaSeconds) override;
    const FAnimGraphAssetData& GetAssetData() const { return Snapshot; }
    const FAnimPoseGraphRuntime* GetRuntime() const { return Runtime.get(); }
protected:
    void NativeInitializeAnimation() override;
    void NativeUnInitializeAnimation() override;
    void NativeUpdateAnimation(float DeltaSeconds) override;
private:
    FAnimGraphAssetData Snapshot;
    std::unique_ptr<FAnimPoseGraphRuntime> Runtime;
    FAnimationScriptContext Script;
    FString LastError;
    bool Paused = false;
};
