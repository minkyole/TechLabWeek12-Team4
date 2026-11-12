#pragma once
#include "Editor/SkeletalMeshViewer/SkeletalMeshViewerPanel.h"
#include "Animation/AnimGraph.h"
#include "Animation/AssetAnimInstance.h"

// Uses the same renderer/camera implementation as Mesh Viewer, with private
// targets, components and playback. Compile swaps a fully configured candidate.
class FAnimationPreviewSession
{
public:
    bool Compile(FSkeletalMeshViewerPanel& Services, const FAnimGraphAssetData& Data, FString& Error);
    bool Compile(FSkeletalMeshViewerPanel& Services, const FAnimStateMachineAssetData& Data, FString& Error);
    void Tick(float DeltaSeconds) { if(Backend) Backend->Tick(DeltaSeconds); }
    void Render() { if(Backend) Backend->RenderPreview(); }
    void Draw() { if(Backend) Backend->DrawEmbeddedPreview(); else ImGui::TextDisabled("Compile to preview this animation."); }
    void Hide() { if(Backend) Backend->HideEmbeddedPreview(); }
    FSkeletalMeshViewerPanel* GetBackend() const { return Backend.get(); }
    UAnimInstance* GetInstance() const;
    void SetPaused(bool Paused);
    bool IsPaused() const;
private:
    std::unique_ptr<FSkeletalMeshViewerPanel> Backend;
};
