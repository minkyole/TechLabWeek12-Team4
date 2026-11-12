#pragma once

enum class EAnimParameterType : uint8
{
	Bool,
	Float,
	Trigger,
};

struct FAnimParameter
{
	FName Name;
	EAnimParameterType Type = EAnimParameterType::Bool;

	bool BoolValue = false;
	float FloatValue = 0.0f;
};

struct FAnimParameterSet
{
	TArray<FAnimParameter> Values;

	bool SetBool(FName Name, bool Value);
	bool SetFloat(FName Name, float Value);
	bool Trigger(FName Name);

	bool TryGetBool(FName Name, bool& OutValue) const;
	bool TryGetFloat(FName Name, float& OutValue) const;
	bool TryGetTrigger(FName Name, bool& OutValue) const;
	void ConsumeTrigger(FName Name);
	void ClearTriggers();

private:
	FAnimParameter* Find(FName Name);
	const FAnimParameter* Find(FName Name) const;

	FAnimParameter* FindOrAdd(FName Name, EAnimParameterType Type);
};