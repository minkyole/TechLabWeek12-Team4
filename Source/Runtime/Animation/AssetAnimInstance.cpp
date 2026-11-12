#include "EnginePCH.h"
#include "AssetAnimInstance.h"
#include "Asset/AssetManager.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/Engine.h"
#include "Scripting/LuaVM.h"
#include "Core/Misc/FileHelper.h"

UAssetAnimInstance::~UAssetAnimInstance()
{
	NativeUnInitializeAnimation();
}
void UAssetAnimInstance::NativeInitializeAnimation()
{
	ResetGraph();
	auto path = GetSkeletalMeshComponent()->GetStateMachineAssetPath();
	if (path.empty())
		return; // editor configures its private working snapshot after initialization
	FString error;
	auto *asset = UAssetManager::Get().LoadAnimStateMachine(path, error);
	if (!asset || !Configure(asset->Data))
	{
		if (!error.empty())
			Fail(error);
		HTR_LOG(Error, "[AnimStateMachine] {}", GetLastError());
	}
}
bool UAssetAnimInstance::Configure(const FAnimStateMachineAssetData &Data, bool EnableScript)
{
	FString error;
	if (!Data.Validate(error))
		return Fail(error);
	if (!GetSkeletalMesh() || GetSkeletalMesh()->GetPath() != Data.MeshKey)
		return Fail("State machine mesh does not match component mesh.");
	EndAllNotifyStates();
	NativeUnInitializeAnimation();
	Snapshot = Data;
	Paused = false;
	if (!Snapshot.Build(*this, error))
	{
		ResetGraph();
		return Fail(error);
	}
	for (int i = 0; i < (int)Snapshot.States.size(); ++i)
	{
		const auto &s = Snapshot.States[i];
		if (s.PoseGraph.Nodes.empty() && s.BlendSpace && !s.BlendParameter.empty())
		{
			float v = 0;
			TryGetFloat(s.BlendParameter, v);
			SetBlendSpaceInput(i, v);
		}
	}
	FAnimationUpdateContext Context;
	Context.AnimInstance = nullptr;
	Context.DeltaSeconds = 0.0f;
	for (const FPlayerEntry& Entry : Players) if (Entry.Kind != EPlayerKind::Graph) Entry.Node->Update(Context);
	if (EnableScript && !Snapshot.UpdateScript.empty())
	{
		if (!Script.Load(Snapshot.UpdateScript, false, true, error))
		{
			ResetGraph();
			return Fail(error);
		}
	}
	return true;
}
void UAssetAnimInstance::NativeUnInitializeAnimation()
{
	ResetGraph();
	Script.Reset();
}
void UAssetAnimInstance::UpdateAnimation(float DeltaSeconds)
{
	if (!Paused)
		UStateMachineAnimInstance::UpdateAnimation(DeltaSeconds);
}
void UAssetAnimInstance::NativeUpdateAnimation(float DeltaSeconds)
{
	if (!IsGraphReady())
		return;
	if (Script.Update.valid())
	{
		bool failed = false;
		{
			auto result = Script.Update(static_cast<UStateMachineAnimInstance *>(this), DeltaSeconds);
			if (!result.valid())
			{
				sol::error e = result;
				Fail(e.what());
				HTR_LOG(Error, "[AnimStateMachine] Lua update failed: {}", e.what());
				failed = true;
			}
		}
		if (failed)
			Script.Update = {};
	}
	for (int i = 0; i < (int)Snapshot.States.size(); ++i)
	{
		const auto &s = Snapshot.States[i];
		if (s.PoseGraph.Nodes.empty() && s.BlendSpace && !s.BlendParameter.empty())
		{
			float v;
			if (TryGetFloat(s.BlendParameter, v))
				SetBlendSpaceInput(i, v);
		}
	}
}
int UAssetAnimInstance::GetCurrentAssetState() const
{
	int i = GetCurrentState();
	return i >= 0 && i < (int)Snapshot.States.size() ? Snapshot.States[i].Id : -1;
}

bool UAssetAnimInstance::UsesDependency(const FString& Path) const
{
    auto Key = FFileHelper::ToUtf8Path(fs::weakly_canonical(FFileHelper::ToNativePath(Path)));
    for (auto& Entry : Players)
        if (Entry.Kind == EPlayerKind::Graph)
            for (auto& Dependency : static_cast<FAnimPoseGraphRuntime*>(Entry.Node.get())->GetDependencies())
                if (FFileHelper::ToUtf8Path(fs::weakly_canonical(FFileHelper::ToNativePath(Dependency))) == Key) return true;
    return false;
}
