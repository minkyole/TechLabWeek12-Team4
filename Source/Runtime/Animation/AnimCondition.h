#pragma once

#include <functional>

struct FAnimConditionContext;
using FAnimConditionCallback =
    std::function<bool(const FAnimConditionContext&)>;

using FAnimConditionId = int32;
constexpr FAnimConditionId InvalidAnimConditionId = -1;

enum class EAnimConditionType : uint8
{
	Always,
	BoolEquals,
	FloatCompare,
	TriggerActive,
	SourceFinished,
	StateElapsedCompare,
	And,
	Or,
	Not,

	Custom,
};

enum class EAnimCompareOp : uint8
{
	Less,
	LessEqual,
	Greater,
	GreaterEqual,
	NearlyEqual,
	NotNearlyEqual,
};

struct FAnimConditionDefinition
{
	EAnimConditionType Type = EAnimConditionType::Always;

	// BoolEquals / FloatCompare / TriggerActive
	FName ParameterName;

	// BoolEquals
	bool BoolValue = true;

	// FloatCompare / StateElapsedCompare
	EAnimCompareOp CompareOp = EAnimCompareOp::Greater;
	float FloatValue = 0.0f;
	float Tolerance = 0.001f;

	// And / Or / Not
	TArray<FAnimConditionId> Children;

	// Custom
	FAnimConditionCallback Callback;
};