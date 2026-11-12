#pragma once
#include "Core/Types.h"
#include "Core/EngineString.h"

// 스켈레탈 메시를 CPU와 GPU 중 어디서 스키닝할지 정한다. 콘솔 명령 "skinning [cpu|gpu]"로 바꾼다.
class FSkinningSettings
{
public:
	static bool ExecCommand(const FString& CommandLine, FString& OutMessage);

	static bool IsGPU() { return bGPUSkinning; }
	static void SetGPU(bool bInGPU);

	// 모드가 바뀔 때마다 증가한다. 컴포넌트는 이 값이 바뀌면 포즈가 같아도 스키닝을 다시 한다.
	static uint32 GetModeVersion() { return ModeVersion; }

	// 성능 비교용. 켜면 포즈가 그대로여도 매 프레임 다시 스키닝한다. "skinning force [on|off]".
	static bool IsForceEveryFrame() { return bForceEveryFrame; }
	static void SetForceEveryFrame(bool bInForce) { bForceEveryFrame = bInForce; }

private:
	inline static bool bGPUSkinning = true;
	inline static bool bForceEveryFrame = false;
	inline static uint32 ModeVersion = 1;
};
