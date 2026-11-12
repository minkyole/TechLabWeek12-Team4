#pragma once
#include "Animation/AnimStateMachineAsset.h"
#include "Animation/AnimationScriptContext.h"
#include "Animation/StateMachineAnimInstance.h"

class UAssetAnimInstance : public UStateMachineAnimInstance
{
	DECLARE_CLASS(UAssetAnimInstance, UStateMachineAnimInstance)
  public:
	~UAssetAnimInstance() override;
	void UpdateAnimation(float DeltaSeconds) override;
	bool UsesDependency(const FString& Path) const;
	bool Configure(const FAnimStateMachineAssetData &Data, bool EnableScript = true);
	void SetPaused(bool Value)
	{
		Paused = Value;
	}
	bool IsPaused() const
	{
		return Paused;
	}
	int GetCurrentAssetState() const;
	const FAnimStateMachineAssetData &GetAssetData() const
	{
		return Snapshot;
	}

  protected:
	void NativeInitializeAnimation() override;
	void NativeUnInitializeAnimation() override;
	void NativeUpdateAnimation(float DeltaSeconds) override;

  private:
	FAnimStateMachineAssetData Snapshot;
	bool Paused = false;
	FAnimationScriptContext Script;
};
