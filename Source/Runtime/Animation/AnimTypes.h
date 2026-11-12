#pragma once
#include "Math/Vector.h"
#include "Math/Quat.h"
#include "Core/NameTypes.h"
#include "Containers/Array.h"

struct FFrameRate
{
	int32 Numerator = 30; // 분자
	int32 Denominator = 1; // 분모
	

	FFrameRate() = default;
	FFrameRate(int32 InNumerator, int32 InDenominator)
	    : Numerator(InNumerator), Denominator(InDenominator) {}

	// Frame per Second
	float AsDecimal() const
	{
		return static_cast<float>(Numerator) / static_cast<float>(Denominator);
	}

	// Second per Frame
	float AsInterval() const
	{
		return static_cast<float>(Denominator) / static_cast<float>(Numerator);
	}
};

struct FRawAnimSequenceTrack
{
	TArray<FVector> PosKeys; // 위치
	TArray<FQuat> RotKeys; // 회전
	TArray<FVector> ScaleKeys; // 스케일

	bool IsValid() const
	{
		return !PosKeys.IsEmpty() && !RotKeys.IsEmpty() && !ScaleKeys.IsEmpty();
	}
};

struct FBoneAnimationTrack
{
	FName Name; // Bone 이름 
	FRawAnimSequenceTrack InternalTrack; // 애니메이션 데이터
};

// 시간 하나를 두 키 사이의 위치(alpha)로 바꾼 결과
struct FAnimKeyLookup
{
	int32 KeyIndex0 = 0;
	int32 KeyIndex1 = 0;
	float Alpha = 0.f;
};

// 시간을 한 번 진행한 결과. 상태 전환과 Notify 판단이 이 값을 본다.
enum class ETypeAdvanceAnim : uint8
{
	Default,  // 경계에 닿지 않았다
	Looped,   // 루프 경계를 넘어 되감겼다
	Finished, // 루프가 아니고 진행 방향의 끝(정방향 1, 역방향 0)에 닿았다
};

// 이번 프레임에 재생 헤드가 지나간 구간. 정규화 시간(0~1) 단위.
struct FDeltaTimeRecord
{
	float Previous = 0.0f; // 움직이기 전 시간
	float Delta = 0.0f;    // 되감기 전 값이라 부호로 방향, 크기로 바퀴 수를 알 수 있다

	void Set(float InPrevious, float InDelta)
	{
		Previous = InPrevious;
		Delta = InDelta;
	}

	bool IsMoved() const { return Delta != 0.0f; }
};

// UE의 ERootMotionMode. 몽타주가 없어서 FromMontagesOnly는 빠졌다
enum class ERootMotionMode : uint8
{
	NoRootMotionExtraction, // 잠그지도 뽑지도 않는다. 원본 궤적 그대로 재생
	IgnoreRootMotion,       // 잠그고 뽑지만 버린다. 제자리 재생
	RootMotionFromEverything, // 그래프 전체 클립을 가중치로 섞어 Actor로 넘긴다
};

// 루트 본이 한 구간 동안 움직인 양. 구간을 시작할 때 루트가 바라보던 방향 기준이다.
struct FRootMotionParams
{
	FVector Translation = FVector::ZeroVector;
	FQuat Rotation = FQuat::Identity();
	bool bHasRootMotion = false;

	void Clear()
	{
		Translation = FVector::ZeroVector;
		Rotation = FQuat::Identity();
		bHasRootMotion = false;
	}

	// 다음 구간은 앞 구간이 끝난 방향에서 출발하므로, 앞 회전으로 돌려서 이어 붙인다
	void Accumulate(const FRootMotionParams& Next)
	{
		if (!Next.bHasRootMotion)
		{
			return;
		}
		Translation = Translation + Rotation.RotateVector(Next.Translation);
		Rotation = (Rotation * Next.Rotation).Normalized();
		bHasRootMotion = true;
	}

	// 월드 차이를 그대로 쓰면 Actor가 돌아섰을 때 엉뚱한 방향으로 가니, 시작 방향 기준으로 바꾼다
	static FRootMotionParams MakeDelta(const FVector& StartPos, const FQuat& StartRot, const FVector& EndPos, const FQuat& EndRot)
	{
		const FQuat InvStart = StartRot.Inverse();

		FRootMotionParams Out;
		Out.Translation = InvStart.RotateVector(EndPos - StartPos);
		Out.Rotation = (InvStart * EndRot).Normalized();
		Out.bHasRootMotion = true;
		return Out;
	}
};
