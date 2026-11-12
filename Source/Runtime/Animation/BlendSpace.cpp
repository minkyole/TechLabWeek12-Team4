#include "EnginePCH.h"
#include "BlendSpace.h"
#include "AnimSequence.h"

#include <cmath>

namespace
{
	bool IsValidParameter(const FBlendParameter& Parameter)
	{
		return std::isfinite(Parameter.Min) && std::isfinite(Parameter.Max) && Parameter.Min < Parameter.Max;
	}

	// 현재 UAnimSequence::GetPlayLength()는 DataModel을
	// 직접 참조하므로 호출 전에 nullptr 검사가 필요하다.
	float GetValidSampleLength(const UAnimSequence* Animation)
	{
		if (Animation == nullptr || Animation->GetDataModel() == nullptr)
		{
			return 0.0f;
		}

		const float Length = Animation->GetPlayLength();

		if (!std::isfinite(Length) || Length <= 0.0f)
		{
			return 0.0f;
		}

		return Length;
	}
}

// ---------------------------------------------------------
// 축 조회
// ---------------------------------------------------------

const FBlendParameter* UBlendSpace::GetBlendParameter(
    int32 AxisIndex) const
{
	if (AxisIndex < 0 || AxisIndex >= GetDimension() || AxisIndex >= BlendParameters.Num())
	{
		return nullptr;
	}

	return &BlendParameters[AxisIndex];
}

// ---------------------------------------------------------
// 축 설정
// ---------------------------------------------------------

bool UBlendSpace::SetBlendParameter(
    int32 AxisIndex,
    const FBlendParameter& InParameter)
{
	if (GetBlendParameter(AxisIndex) == nullptr)
	{
		return false;
	}

	if (!IsValidParameter(InParameter))
	{
		return false;
	}

	// 새로운 범위에서 기존 샘플이 모두 유효한지 먼저 검사한다.
	// 실패하면 기존 설정은 유지된다.
	for (const FBlendSample& Sample : SampleData)
	{
		const float Value = Sample.SampleValue[AxisIndex];

		if (!std::isfinite(Value) || Value < InParameter.Min || Value > InParameter.Max)
		{
			return false;
		}
	}

	BlendParameters[AxisIndex] = InParameter;

	RebuildSampleData();
	return true;
}

// ---------------------------------------------------------
// 샘플 조회
// ---------------------------------------------------------

const FBlendSample* UBlendSpace::GetSample(
    int32 SampleIndex) const
{
	if (SampleIndex < 0 || SampleIndex >= SampleData.Num())
	{
		return nullptr;
	}

	return &SampleData[SampleIndex];
}

// ---------------------------------------------------------
// 샘플 검사
// ---------------------------------------------------------

bool UBlendSpace::ValidateSample(
    const FBlendSample& Sample,
    int32 IgnoreSampleIndex) const
{
	const int32 Dimension = GetDimension();

	// FVector가 표현할 수 있는 차원 범위와
	// 축 설정 개수가 올바른지 확인한다.
	if (Dimension < 1 || Dimension > 3 || BlendParameters.Num() != Dimension)
	{
		return false;
	}

	// nullptr, DataModel, 유한한 양수 재생 길이 검사.
	if (GetValidSampleLength(Sample.Animation) <= 0.0f)
	{
		return false;
	}

	// 초기 구현 정책: 동일한 메시를 기준으로 하는 샘플만 허용.
	// 나중에 본 이름/계층 기반 호환성 검사로 교체할 수 있다.
	if (GetSkeletalMesh() == nullptr || Sample.Animation->GetSkeletalMesh() != GetSkeletalMesh())
	{
		return false;
	}

	// 사용 중인 축만 검사한다.
	// 1D에서는 Y/Z를 검사하거나 중복 판단에 사용하지 않는다.
	for (int32 AxisIndex = 0; AxisIndex < Dimension; ++AxisIndex)
	{
		const FBlendParameter& Parameter =
		    BlendParameters[AxisIndex];

		if (!IsValidParameter(Parameter))
		{
			return false;
		}

		const float Value = Sample.SampleValue[AxisIndex];

		if (!std::isfinite(Value) || Value < Parameter.Min || Value > Parameter.Max)
		{
			return false;
		}
	}

	// 같은 입력 좌표에 샘플이 두 개 존재하는 것을 막는다.
	for (int32 Index = 0; Index < SampleData.Num(); ++Index)
	{
		// UpdateSample에서는 교체 대상 자신을 제외한다.
		if (Index == IgnoreSampleIndex)
		{
			continue;
		}

		if (IsSameSamplePoint(
		        Sample.SampleValue,
		        SampleData[Index].SampleValue))
		{
			return false;
		}
	}

	return true;
}

// ---------------------------------------------------------
// 샘플 추가
// ---------------------------------------------------------

bool UBlendSpace::AddSample(const FBlendSample& InSample)
{
	if (!ValidateSample(InSample))
	{
		return false;
	}

	SampleData.Add(InSample);

	// 1D에서는 여기서 X 오름차순 정렬이 수행된다.
	RebuildSampleData();
	return true;
}

// ---------------------------------------------------------
// 샘플 수정
// ---------------------------------------------------------

bool UBlendSpace::UpdateSample(
    int32 SampleIndex,
    const FBlendSample& InSample)
{
	if (GetSample(SampleIndex) == nullptr)
	{
		return false;
	}

	if (!ValidateSample(InSample, SampleIndex))
	{
		return false;
	}

	SampleData[SampleIndex] = InSample;

	// 좌표 변경으로 정렬 순서가 달라질 수 있다.
	RebuildSampleData();
	return true;
}

// ---------------------------------------------------------
// 샘플 삭제
// ---------------------------------------------------------

bool UBlendSpace::RemoveSample(int32 SampleIndex)
{
	if (GetSample(SampleIndex) == nullptr)
	{
		return false;
	}

	SampleData.RemoveAt(SampleIndex);

	RebuildSampleData();
	return true;
}

// ---------------------------------------------------------
// 샘플 전체 삭제
// ---------------------------------------------------------

void UBlendSpace::ClearSamples()
{
	SampleData.Reset();

	// 파생 클래스의 검색용 데이터도 비우도록 호출한다.
	RebuildSampleData();
}

// ---------------------------------------------------------
// 에셋 대표 길이
// ---------------------------------------------------------

float UBlendSpace::GetPlayLength() const
{
	float MaxLength = 0.0f;

	for (const FBlendSample& Sample : SampleData)
	{
		const float Length = GetValidSampleLength(Sample.Animation);

		if (Length > MaxLength)
		{
			MaxLength = Length;
		}
	}

	return MaxLength;
}

// ---------------------------------------------------------
// 현재 가중치에 따른 재생 길이
// ---------------------------------------------------------

float UBlendSpace::GetEffectivePlayLength(
    const TArray<FBlendSampleWeight>& Weights) const
{
	// 합산 오차를 줄이기 위해 누적 계산은 double로 수행한다.
	double WeightedLength = 0.0;
	double TotalWeight = 0.0;

	for (const FBlendSampleWeight& Entry : Weights)
	{
		if (!std::isfinite(Entry.Weight) || Entry.Weight < 0.0f)
		{
			return 0.0f;
		}

		const FBlendSample* Sample = GetSample(Entry.SampleIndex);

		if (Sample == nullptr)
		{
			return 0.0f;
		}

		const float Length = GetValidSampleLength(Sample->Animation);

		if (Length <= 0.0f)
		{
			return 0.0f;
		}

		WeightedLength +=
		    static_cast<double>(Length) * Entry.Weight;

		TotalWeight += Entry.Weight;
	}

	if (TotalWeight <= 0.0)
	{
		return 0.0f;
	}

	// 가중치 합이 정확히 1이 아니어도 정규화해서 계산한다.
	return static_cast<float>(WeightedLength / TotalWeight);
}

UBlendSpace1D::UBlendSpace1D()
{
	BlendParameters.SetNum(1);

	BlendParameters[0] = FBlendParameter();
}

bool UBlendSpace1D::GetSampleWeights(
    const FVector& Input,
    TArray<FBlendSampleWeight>& OutWeights) const
{
	// 이전 프레임의 결과를 제거한다.
	OutWeights.Reset();

	if (SampleData.IsEmpty() || !std::isfinite(Input.X))
	{
		return false;
	}

	const FBlendParameter* Parameter = GetBlendParameter(0);

	if (!Parameter ||
	    !std::isfinite(Parameter->Min) ||
	    !std::isfinite(Parameter->Max) ||
	    Parameter->Min >= Parameter->Max)
	{
		return false;
	}

	const float X = std::clamp(Input.X, Parameter->Min, Parameter->Max);

	const int32 NumSamples = SampleData.Num();

	const auto AddWeight =
	    [&OutWeights](int32 Index, float Weight)
	{
		// 가중치가 0인 샘플은 평가할 필요가 없다.
		if (Weight > 0.0f)
		{
			FBlendSampleWeight Result;
			Result.SampleIndex = Index;
			Result.Weight = Weight;
			OutWeights.Add(Result);
		}
	};

	// 샘플이 하나이거나, 입력이 첫 번째 심플 이하인 경우.
	if (NumSamples == 1 || X <= SampleData[0].SampleValue.X)
	{
		AddWeight(0, 1.0f);
		return true;
	}

	// 입력이 마지막 샘플 이상인 경우.
	if (X >= SampleData[NumSamples - 1].SampleValue.X)
	{
		AddWeight(NumSamples - 1, 1.0f);
		return true;
	}

	// X 이상인 첫 번째 샘플을 이진 탐색한다.
	const auto RightIt = std::lower_bound(
	    SampleData.begin(), SampleData.end(), X,
	    [](const FBlendSample& Sample, float Value)
	    {
		    return Sample.SampleValue.X < Value;
	    });

	// 위에서 양 끝 경계를 처리했으므로
	// RightIndex와 LeftIndex는 모두 유효하다.
	const int32 RightIndex =
	    static_cast<int32>(RightIt - SampleData.begin());

	const int32 LeftIndex = RightIndex - 1;

	// 입력이 샘플 좌표와 일치하면 해당 샘플만 사용한다.
	if (X == SampleData[RightIndex].SampleValue.X)
	{
		AddWeight(RightIndex, 1.0f);
		return true;
	}

	// 큰 좌표끼리의 뺄셈에서 float 오버플로를 피하도록
	// 중간 계산은 double로 수행한다.
	const double LeftX = SampleData[LeftIndex].SampleValue.X;
	const double RightX = SampleData[RightIndex].SampleValue.X;
	const double Interval = RightX - LeftX;

	if (Interval <= 0.0)
	{
		return false;
	}

	const float Alpha = static_cast<float>(
	    (static_cast<double>(X) - LeftX) / Interval);

	AddWeight(LeftIndex, 1.0f - Alpha);
	AddWeight(RightIndex, Alpha);

	return true;
}

bool UBlendSpace1D::IsSameSamplePoint(
    const FVector& A,
    const FVector& B) const
{
	constexpr double Tolerance = 0.0001;

	return std::abs(
	           static_cast<double>(A.X) - B.X) <= Tolerance;
}

void UBlendSpace1D::RebuildSampleData()
{
	// 1D에서는 X 오름차순 정렬만 수행한다.
	std::sort(
	    SampleData.begin(), SampleData.end(),
	    [](const FBlendSample& A, const FBlendSample& B)
	    {
		    return A.SampleValue.X < B.SampleValue.X;
	    });
}

void UBlendSpace1D::CopyDefinitionFrom(const UBlendSpace1D& Other)
{
	SetSkeletalMesh(Other.GetSkeletalMesh());
	BlendParameters = Other.BlendParameters;
	SampleData = Other.SampleData;
	NotifyTriggerMode = Other.NotifyTriggerMode;
	RebuildSampleData();
}
