#pragma once

#include "Animation/AnimInstance.h"
#include "Animation/Nodes/AnimNodes.h"

class UAnimSequence;
class UBlendSpace1D;

class UTestAnimInstance : public UAnimInstance
{
	DECLARE_CLASS(UTestAnimInstance, UAnimInstance)

public:
	~UTestAnimInstance() override;

	void SetSpeed(float InSpeed);

protected:
	void NativeInitializeAnimation() override;
	void NativeUnInitializeAnimation() override;

private:
	UAnimSequence* CreateRotationClip(
	    int32 BoneIndex,
	    const FVector& LocalAxis,
	    float DurationSeconds,
	    bool bStationary);

	void ReleaseTestAssets();

private:
	FAnimNode_BlendSpacePlayer BlendSpacePlayer;

	// 이 인스턴스가 생성하고 해제한다.
	UBlendSpace1D* TestBlendSpace = nullptr;
	UAnimSequence* IdleClip = nullptr;
	UAnimSequence* SlowClip = nullptr;
	UAnimSequence* FastClip = nullptr;
};