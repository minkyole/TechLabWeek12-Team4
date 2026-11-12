#pragma once
#include "Core/Types.h"
#include <cmath>

enum class EDecalMode : uint8
{
	ScreenSpace,
	Projected,
};

class FDecalSettings
{
public:
	static void SetMode(EDecalMode InMode) { Mode = InMode; }
	static EDecalMode GetMode() { return Mode; }

	// 투영 방향과 표면 법선이 이루는 각도가 이보다 크면 Decal을 찍지 않는다.
	// 90도면 옆면과 뒷면을 버리고, 180도면 아무것도 버리지 않는다.
	static void SetNormalAngle(float InDegrees)
	{
		if (NormalAngle == InDegrees) return;
		NormalAngle = InDegrees;
		++Version;
	}
	static float GetNormalAngle() { return NormalAngle; }
	static float GetNormalThreshold() { return std::cos(NormalAngle * 3.14159265f / 180.0f); }

	// 투영 메시에 영향을 주는 설정이 바뀔 때마다 늘어난다. Scene이 비교해 투영 메시를 다시 만든다.
	static uint32 GetVersion() { return Version; }

private:
	inline static EDecalMode Mode = EDecalMode::ScreenSpace;
	inline static float NormalAngle = 60.0f;
	inline static uint32 Version = 0;
};
