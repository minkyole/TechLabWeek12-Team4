#pragma once

#include "Animation/AnimationAsset.h"

class UAnimSequence;

// 블랜드 축의 정의. 예: Speed, 0-600
struct FBlendParameter
{
	FName Name;
	float Min = 0.0f;
	float Max = 600.0f;
};

// 에셋에 저장하는 샘플
struct FBlendSample
{
	UAnimSequence* Animation = nullptr;
	FVector SampleValue = FVector::ZeroVector; // 1D에서는 X만 사용
};

// 입력값으로 계산한 결과. 재생 노드가 보관한다.
struct FBlendSampleWeight
{
	int32 SampleIndex = -1;
	float Weight = 0.0f;
};

// 여러 샘플이 섞일 때 어느 샘플의 Notify를 발동할지 (언리얼 ENotifyTriggerMode 대응)
enum class ENotifyTriggerMode : uint8
{
	AllAnimations,
	HighestWeightedAnimation,
	None,
};

inline const char* NotifyTriggerModeToString(ENotifyTriggerMode Mode)
{
	switch (Mode)
	{
	case ENotifyTriggerMode::AllAnimations: return "AllAnimations";
	case ENotifyTriggerMode::None:          return "None";
	default:                                return "HighestWeightedAnimation";
	}
}

inline bool NotifyTriggerModeFromString(const FString& Text, ENotifyTriggerMode& OutMode)
{
	for (ENotifyTriggerMode Mode : { ENotifyTriggerMode::AllAnimations, ENotifyTriggerMode::HighestWeightedAnimation, ENotifyTriggerMode::None })
	{
		if (Text == NotifyTriggerModeToString(Mode))
		{
			OutMode = Mode;
			return true;
		}
	}
	return false;
}

class UBlendSpace : public UAnimationAsset
{
	DECLARE_CLASS(UBlendSpace, UAnimationAsset)

public:
	UBlendSpace() = default;
	~UBlendSpace() override = default;

	// UBlendSpace1D는 1, UBlendSpace2D는 2를 반환한다.
	virtual int32 GetDimension() const = 0;

	// 유효하지 않은 축 번호이면 nullptr.
	const FBlendParameter* GetBlendParameter(int32 AxisIndex) const;

	bool SetBlendParameter(
	    int32 AxisIndex,
	    const FBlendParameter& InParameter);

	int32 GetNumSamples() const
	{
		return SampleData.Num();
	}

	const TArray<FBlendSample>& GetSamples() const
	{
		return SampleData;
	}

	// 유효하지 않은 인덱스이면 nullptr.
	const FBlendSample* GetSample(int32 SampleIndex) const;

	bool AddSample(const FBlendSample& InSample);

	bool UpdateSample(
	    int32 SampleIndex,
	    const FBlendSample& InSample);

	// 유효하지 않은 인덱스이면 false.
	bool RemoveSample(int32 SampleIndex);

	void ClearSamples();

	virtual bool GetSampleWeights(
	    const FVector& Input,
	    TArray<FBlendSampleWeight>& OutWeights) const = 0;

	float GetPlayLength() const override;

	float GetEffectivePlayLength(
	    const TArray<FBlendSampleWeight>& Weights) const;

	ENotifyTriggerMode NotifyTriggerMode = ENotifyTriggerMode::HighestWeightedAnimation;

protected:
	bool ValidateSample(
	    const FBlendSample& Sample,
	    int32 IgnoreSampleIndex = -1) const;

	virtual bool IsSameSamplePoint(
	    const FVector& A,
	    const FVector& B) const = 0;

	virtual void RebuildSampleData() = 0;

protected:
	TArray<FBlendParameter> BlendParameters;

	TArray<FBlendSample> SampleData;
};

class UBlendSpace1D : public UBlendSpace
{
	DECLARE_CLASS(UBlendSpace1D, UBlendSpace)
public:
	UBlendSpace1D();

	// Copy authored data without changing object identity or path.
	void CopyDefinitionFrom(const UBlendSpace1D& Other);

	int32 GetDimension() const override
	{
		return 1;
	}

	bool GetSampleWeights(
	    const FVector& Input,
	    TArray<FBlendSampleWeight>& OutWeights) const override;

protected:
	bool IsSameSamplePoint(
	    const FVector& A,
	    const FVector& B) const override;

	void RebuildSampleData() override;
};