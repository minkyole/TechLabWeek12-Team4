#pragma once

#include "Animation/AnimInstance.h"

class UAnimSequence;

// SingleNode 모드의 "재생 요청"을 컴포넌트에 보관한다. FAnimNode_SequencePlayer에 대응.
struct FSingleAnimationPlayData
{
	// 비소유 참조. 클립은 AssetManager가 소유하는 공유 에셋이다.
	UAnimSequence* AnimToPlay = nullptr;

	bool bSavedLooping = true;
	bool bSavedPlaying = true;
	float SavedPosition = 0.0f;
	float SavedPlayRate = 1.0f;

	// 새로 만들어진 인스턴스에 보관해 둔 요청을 밀어 넣는다.
	void Initialize(UAnimSingleNodeInstance* Instance) const
	{
		if (!Instance)
			return;

		// SetAnimationAsset이 CurrentTime을 0으로 되돌리고,
		// SetPosition은 클립 길이로 클램프하므로 반드시 클립을 먼저 넣는다.
		Instance->SetAnimationAsset(AnimToPlay);
		Instance->SetPosition(SavedPosition);
		Instance->SetPlayRate(SavedPlayRate);
		Instance->SetLooping(bSavedLooping);
		Instance->SetPlaying(bSavedPlaying);
	}
};
