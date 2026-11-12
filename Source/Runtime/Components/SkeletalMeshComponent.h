#pragma once

#include "EnginePCH.h"
#include "SkinnedMeshComponent.h"
#include "Engine/SkeletonSceneProxy.h"
#include "Render/Mesh.h"
#include "Animation/AnimationContext.h"
#include "Animation/SingleAnimationPlayData.h"
#include "Animation/AnimNotifyEvent.h"
#include "Core/TDelegate.h"

class UAnimInstance;
class UAnimSequenceBase;
class UClass;

enum class EAnimationMode : uint8
{
	AnimationBlueprint = 0,  // AnimClass로 지정한 그래프 인스턴스 (StateMachine 등)
	AnimationSingleNode = 1, // UAnimSingleNodeInstance로 클립 하나만 재생
    Lua = 2,
    StateMachine = 3,
    AnimGraph = 4,
};

class USkeletalMeshComponent : public USkinnedMeshComponent
{
	DECLARE_CLASS(USkeletalMeshComponent, USkinnedMeshComponent)

	REFLECT_START(USkeletalMeshComponent)
		PROPERTY(AnimationMode)
        PROPERTY(AnimationScriptPath)
        PROPERTY(AnimGraphAssetPath)
		PROPERTY(StateMachineAssetPath)
	REFLECT_END()

public:
	USkeletalMeshComponent();
	~USkeletalMeshComponent() override;

	void BeginPlay() override;
	void Serialize(FStructuredArchive::FRecord Record) override;
	void NormalizeProperties(const FProperty* ChangedProperty = nullptr) override;
	void PostApplyProperties(const FProperty* ChangedProperty = nullptr) override;
	void TickComponent(float DeltaTime) override;

	void InitializeComponent() override;
	// void TickComponent(float DeltaTime) override;

	void SetSkeletalMesh(USkeletalMesh* InMesh) override;

	// 메시가 준비되면 생성, 초기화. 이미 초기화됐다면 유지.
	void InitializeAnimInstance();

	// 종료 처리 후 인스턴스 삭제.
	void ClearAnimInstance();

	// 시간 진행 없이 포즈만 평가, 적용. 뷰어 시크에 사용.
	bool RefreshAnimationPose();

	void HandleAnimNotify(const struct FAnimNotifyEvent& Notify);
	void HandleAnimNotifyStateBegin(const FAnimNotifyEvent& Notify);
	void HandleAnimNotifyStateEnd(const FAnimNotifyEvent& Notify);

	// 발동한 모든 Notify(클래스, 이름)를 알린다. 에디터 프리뷰처럼 오너가 없는 쪽이 구독한다.
	DECLARE_DELEGATE(OnAnimNotify, const FAnimNotifyEvent&, UAnimSequenceBase*)
	DECLARE_DELEGATE(OnAnimNotifyStateBegin, const FAnimNotifyEvent&, UAnimSequenceBase*)
	DECLARE_DELEGATE(OnAnimNotifyStateEnd, const FAnimNotifyEvent&, UAnimSequenceBase*)

	// 비소유 포인터. 호출자는 delete하지 않는다.
	UAnimInstance* GetAnimInstance() const { return AnimInstance.get(); }

	// SingleNode 모드가 아니면 nullptr.
	UAnimSingleNodeInstance* GetSingleNodeInstance() const;

	// 이번 프레임 루트 이동을 월드 기준으로 꺼낸다. 꺼내면 비워지니 이동 쪽 한 곳에서만 부른다
	bool ConsumeRootMotionWorld(FVector& OutWorldDelta, FQuat& OutWorldRotation);

	// ---- 재생 입구 ----
	// 모드나 클래스가 바뀌면 인스턴스를 버리고 설계도로 다시 만든다.
	void SetAnimationMode(EAnimationMode InMode);
	EAnimationMode GetAnimationMode() const { return static_cast<EAnimationMode>(AnimationMode); }
	void SetAnimInstanceClass(UClass* InClass);
	void SetStateMachineAssetPath(const FString& Path);
	const FString& GetStateMachineAssetPath() const { return StateMachineAssetPath; }

    void SetAnimGraphAssetPath(const FString& Path);
    const FString& GetAnimGraphAssetPath() const { return AnimGraphAssetPath; }
    void SetAnimationScriptPath(const FString& Path);
    const FString& GetAnimationScriptPath() const { return AnimationScriptPath; }
    bool UsesAnimationAsset(const FString& Path) const;

	// ---- SingleNode 모드 전용 ----
	void SetAnimation(UAnimSequence* NewAnimToPlay);
	void Play(bool bLooping);
	void Stop();
	bool IsPlaying() const;

	// SingleNode 모드로 전환 → 클립 지정 → 처음부터 재생.
	void PlayAnimation(UAnimSequence* NewAnimToPlay, bool bLooping);

	FPrimitiveSceneProxy* CreateSceneProxy() override;
	void SubmitToRenderQueue(FRenderQueue& RenderQueue) override;

protected:
	// 시간 진행 + 포즈 받기 평가, 적용.
	void TickPose(float DeltaTime) override final;

private:
	TUniquePtr<UAnimInstance> AnimInstance;

	int AnimationMode = static_cast<int>(EAnimationMode::Lua);
    FString AnimGraphAssetPath;
    FString AnimationScriptPath = "Assets/Scripts/Animation/CharacterAnim.lua";

	// AnimationBlueprint 모드에서 생성할 클래스.
	UClass* AnimClass = nullptr;
	FString StateMachineAssetPath;

	// AnimationSingleNode 모드의 재생 요청.
	FSingleAnimationPlayData AnimationData;

	// 매 프레임 포즈 배열을 새로 만들지 않고 재사용.
	FPoseContext EvaluatedPose;
};
