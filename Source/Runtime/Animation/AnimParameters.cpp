#include "EnginePCH.h"
#include "AnimParameters.h"

bool FAnimParameterSet::SetBool(FName Name, bool Value)
{
	FAnimParameter* Parameter =
	    FindOrAdd(Name, EAnimParameterType::Bool);

	if (!Parameter)
		return false;

	Parameter->BoolValue = Value;
	return true;
}

bool FAnimParameterSet::SetFloat(FName Name, float Value)
{
	if (!std::isfinite(Value))
		return false;

	FAnimParameter* Parameter =
	    FindOrAdd(Name, EAnimParameterType::Float);

	if (!Parameter)
		return false;

	Parameter->FloatValue = Value;
	return true;
}

bool FAnimParameterSet::Trigger(FName Name)
{
	FAnimParameter* Parameter =
	    FindOrAdd(Name, EAnimParameterType::Trigger);

	if (!Parameter)
		return false;

	Parameter->BoolValue = true;
	return true;
}

bool FAnimParameterSet::TryGetBool(FName Name, bool& OutValue) const
{
	const FAnimParameter* Parameter = Find(Name);

	if (!Parameter ||
	    Parameter->Type != EAnimParameterType::Bool)
	{
		return false;
	}

	OutValue = Parameter->BoolValue;
	return true;
}

bool FAnimParameterSet::TryGetFloat(FName Name, float& OutValue) const
{
	const FAnimParameter* Parameter = Find(Name);

	if (!Parameter ||
	    Parameter->Type != EAnimParameterType::Float)
	{
		return false;
	}

	OutValue = Parameter->FloatValue;
	return true;
}

bool FAnimParameterSet::TryGetTrigger(FName Name, bool& OutValue) const
{
	const FAnimParameter* Parameter = Find(Name);

	if (!Parameter ||
	    Parameter->Type != EAnimParameterType::Trigger)
	{
		return false;
	}

	OutValue = Parameter->BoolValue;
	return true;
}

void FAnimParameterSet::ConsumeTrigger(FName Name)
{
	FAnimParameter* Parameter = Find(Name);

	if (Parameter &&
	    Parameter->Type == EAnimParameterType::Trigger)
	{
		Parameter->BoolValue = false;
	}
}

void FAnimParameterSet::ClearTriggers()
{
	for (FAnimParameter& Parameter : Values)
	{
		if (Parameter.Type == EAnimParameterType::Trigger)
			Parameter.BoolValue = false;
	}
}

FAnimParameter* FAnimParameterSet::Find(FName Name)
{
	for (FAnimParameter& Parameter : Values)
	{
		if (Parameter.Name == Name)
			return &Parameter;
	}

	return nullptr;
}

const FAnimParameter* FAnimParameterSet::Find(FName Name) const
{
	for (const FAnimParameter& Parameter : Values)
	{
		if (Parameter.Name == Name)
			return &Parameter;
	}


	return nullptr;
}

FAnimParameter* FAnimParameterSet::FindOrAdd(
    FName Name,
    EAnimParameterType Type)
{
	if (FAnimParameter* Existing = Find(Name))
	{
		// 동일한 이름을 다른 타입으로 사용하는 것은 오류
		return Existing->Type == Type ? Existing : nullptr;
	}

	FAnimParameter Parameter;
	Parameter.Name = Name;
	Parameter.Type = Type;

	Values.Add(Parameter);
	return &Values[Values.Num() - 1];
}