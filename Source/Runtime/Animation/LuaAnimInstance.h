#pragma once
#include "Animation/AnimationScriptContext.h"
#include "Animation/StateMachineAnimInstance.h"

class UAnimationAsset;

class ULuaAnimInstance : public UStateMachineAnimInstance
{
	DECLARE_CLASS(ULuaAnimInstance, UStateMachineAnimInstance)

public:
	~ULuaAnimInstance() override;

protected:
	void NativeInitializeAnimation() override;
	void NativeUnInitializeAnimation() override;
	void NativeUpdateAnimation(float DeltaSeconds) override;

private:
	bool LoadAnimationScript(const FString& Path);
	void ReleaseAnimationScript();
	UAnimSequence* CreateRootTestClip(int32 RootBoneIndex, float DurationSeconds, const FVector& Offset);

	// 기존 Lua 별칭과 인스턴스 전용 임시 에셋 키의 연결
	bool RegisterTemporaryAsset(const FString& Alias, UAnimationAsset* Asset);

	// 이 인스턴스가 직접 생성한 임시 에셋의 소유 목록
	TArray<UAnimationAsset*> TemporaryAssets;
	FAnimationScriptContext Script;
	FString AnimationScriptPath = "Assets/Scripts/Animation/CharacterAnim.lua";
};
