#include "EnginePCH.h"
#include "AnimationPreviewSession.h"
bool FAnimationPreviewSession::Compile(FSkeletalMeshViewerPanel& Services, const FAnimGraphAssetData& Data, FString& Error)
{
    auto Candidate=Services.CreateAnimationPreviewBackend();
    if(!Candidate->PreviewAnimGraph(Data,Error)) return false;
    Backend=std::move(Candidate); return true;
}
bool FAnimationPreviewSession::Compile(FSkeletalMeshViewerPanel& Services, const FAnimStateMachineAssetData& Data, FString& Error)
{
    auto Candidate=Services.CreateAnimationPreviewBackend();
    if(!Candidate->PreviewStateMachine(Data,Error)) return false;
    Backend=std::move(Candidate); return true;
}
UAnimInstance* FAnimationPreviewSession::GetInstance() const
{
    if(!Backend) return nullptr;
    if(auto* Graph=Backend->GetAnimGraphPreview()) return Graph;
    return Backend->GetStateMachinePreview();
}
bool FAnimationPreviewSession::IsPaused() const
{
    if(auto* G=Cast<UAnimGraphInstance>(GetInstance())) return G->IsPaused();
    if(auto* S=Cast<UAssetAnimInstance>(GetInstance())) return S->IsPaused();
    return true;
}
void FAnimationPreviewSession::SetPaused(bool Paused)
{
    if(auto* G=Cast<UAnimGraphInstance>(GetInstance())) G->SetPaused(Paused);
    if(auto* S=Cast<UAssetAnimInstance>(GetInstance())) S->SetPaused(Paused);
}
