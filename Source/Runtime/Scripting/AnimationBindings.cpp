#include "EnginePCH.h"
#include "Scripting/AnimationBindings.h"

#include "Animation/LuaAnimInstance.h"
#include "Animation/AnimGraph.h"
#include "Components/SkeletalMeshComponent.h"

#include <stdexcept>
#include <utility>

namespace
{
	FAnimPlayerOptions ReadPlayerOptions(
		const sol::optional<sol::table>& Table)
	{
		FAnimPlayerOptions Result;

		if (!Table)
			return Result;

		Result.bLoop =
		    Table->get_or("loop", true);

		Result.bPlaying =
		    Table->get_or("playing", true);

		Result.PlayRate =
		    Table->get_or("playRate", 1.0f);

		return Result;
	}

	FLuaAnimTransitionOptions ReadTransitionOptions(
	    const sol::table& Table)
	{
		FLuaAnimTransitionOptions Result;

		Result.Condition = Table.get<FAnimConditionId>("condition");
		Result.Priority = Table.get_or("priority", 0);

		Result.Blend.Duration =
		    Table.get_or("blendDuration", 0.2f);

		Result.bRestartIfSameState =
		    Table.get_or("restartIfSameState", false);

		const FString Curve =
		    Table.get_or("blendCurve", FString("Linear"));

		if (Curve == "Linear")
			Result.Blend.Curve = EAnimBlendCurve::Linear;
		else if (Curve == "SmoothStep")
			Result.Blend.Curve = EAnimBlendCurve::SmoothStep;
		else
			throw std::runtime_error("Unknown blendCurve.");

		const FString Policy =
		    Table.get_or("interruptPolicy", FString("Always"));

		if (Policy == "Always")
		{
			Result.InterruptPolicy =
			    EAnimTransitionInterruptPolicy::Always;
		}
		else if (Policy == "Never")
		{
			Result.InterruptPolicy =
			    EAnimTransitionInterruptPolicy::Never;
		}
		else if (Policy == "HigherPriorityOnly")
		{
			Result.InterruptPolicy =
			    EAnimTransitionInterruptPolicy::HigherPriorityOnly;
		}
		else
		{
			throw std::runtime_error("Unknown interruptPolicy.");
		}

		const sol::optional<sol::table> Triggers =
		    Table.get<sol::optional<sol::table>>("consumeTriggers");

		if (Triggers)
		{
			for (size_t Index = 1; Index <= Triggers->size(); ++Index)
			{
				const FString Name =
				    Triggers->get<FString>(Index);

				if (Name.empty())
					throw std::runtime_error("Empty trigger name.");

				Result.ConsumeTriggers.Add(FName(Name.c_str()));
			}
		}

		return Result;
	}

	struct FLuaAnimConditionContext
	{
		FAnimParameterSet Parameters;

		bool bSourceFinished = false;
		float StateElapsed = 0.0f;

		bool GetBool(const FString& Name) const
		{
			bool Value = false;

			if (!Parameters.TryGetBool(FName(Name.c_str()), Value))
				throw std::runtime_error(
				    "Bool parameter is missing or has the wrong type.");

			return Value;
		}

		float GetFloat(const FString& Name) const
		{
			float Value = 0.0f;

			if (!Parameters.TryGetFloat(FName(Name.c_str()), Value))
				throw std::runtime_error(
				    "Float parameter is missing or has the wrong type.");

			return Value;
		}

		bool IsTriggerActive(const FString& Name) const
		{
			bool Value = false;

			if (!Parameters.TryGetTrigger(FName(Name.c_str()), Value))
				throw std::runtime_error(
				    "Trigger is missing or has the wrong type.");

			return Value;
		}

		bool IsSourceFinished() const
		{
			return bSourceFinished;
		}

		float GetStateElapsed() const
		{
			return StateElapsed;
		}
	};

	FAnimConditionId AddLuaCondition(
	    ULuaAnimInstance& Anim,
	    sol::protected_function Function)
	{
		return Anim.AddCustomCondition(
		    [Function = std::move(Function),
		     bDisabled = false](const FAnimConditionContext& Context) mutable -> bool
		    {
			    if (bDisabled)
				    return false;

			    FLuaAnimConditionContext Snapshot;
			    Snapshot.Parameters = Context.Parameters;
			    Snapshot.bSourceFinished = Context.bSourceFinished;
			    Snapshot.StateElapsed = Context.StateElpased;

			    // 값으로 전달하여 Lua userdata가 복사본을 소유하게 한다.
			    sol::protected_function_result Result =
			        Function(std::move(Snapshot));

			    if (!Result.valid())
			    {
				    const sol::error Error = Result;

				    HTR_LOG(
				        Error,
				        "[LuaAnim] Condition failed: {}",
				        Error.what());

				    bDisabled = true;
				    return false;
			    }

			    if (Result.return_count() != 1 ||
			        Result.get_type() != sol::type::boolean)
			    {
				    HTR_LOG(
				        Error,
				        "[LuaAnim] Condition must return one boolean.");

				    bDisabled = true;
				    return false;
			    }

			    return Result.get<bool>();
		    });
	}
}

void BindAnimationAPI(sol::state& LuaState)
{
	auto Base = LuaState.new_usertype<UAnimInstance>("AnimInstance", sol::no_constructor);
    Base["SetBool"] = &UAnimInstance::SetBool;
    Base["SetFloat"] = &UAnimInstance::SetFloat;
    Base["SetTrigger"] = &UAnimInstance::SetTrigger;
    Base["ResetTrigger"] = &UAnimInstance::ResetTrigger;
    Base["GetSkeletalMeshComponent"] = &UAnimInstance::GetSkeletalMeshComponent;
    Base["GetBool"] = [](const UAnimInstance& I, const FString& N) { bool V; if(!I.TryGetBool(N,V)) throw std::runtime_error("Invalid Bool parameter"); return V; };
    Base["GetFloat"] = [](const UAnimInstance& I, const FString& N) { float V; if(!I.TryGetFloat(N,V)) throw std::runtime_error("Invalid Float parameter"); return V; };
    Base["IsTriggerActive"] = [](const UAnimInstance& I, const FString& N) { bool V; if(!I.TryGetTrigger(N,V)) throw std::runtime_error("Invalid Trigger parameter"); return V; };
    auto Graph = LuaState.new_usertype<UAnimGraphInstance>("AnimGraphInstance", sol::no_constructor, sol::base_classes, sol::bases<UAnimInstance>());
    Graph["IsGraphReady"] = &UAnimGraphInstance::IsGraphReady;
    Graph["GetLastError"] = &UAnimGraphInstance::GetLastError;
    auto Common = LuaState.new_usertype<UStateMachineAnimInstance>("StateMachineAnimInstance", sol::no_constructor, sol::base_classes, sol::bases<UAnimInstance>());
	Common["SetBool"] = &UStateMachineAnimInstance::SetBool;
	Common["SetFloat"] = &UStateMachineAnimInstance::SetFloat;
	Common["SetTrigger"] = &UStateMachineAnimInstance::SetTrigger;
	Common["ResetTrigger"] = &UStateMachineAnimInstance::ResetTrigger;
	Common["IsGraphReady"] = &UStateMachineAnimInstance::IsGraphReady;
	Common["GetLastError"] = &UStateMachineAnimInstance::GetLastError;
	Common["IsSourceFinished"] = &UStateMachineAnimInstance::IsSourceFinished;
	Common["GetCurrentState"] = &UStateMachineAnimInstance::GetCurrentState;
	Common["GetBool"] = [](const UStateMachineAnimInstance &I, const FString &N) {
		bool V = false;
		if (!I.TryGetBool(N, V))
			throw std::runtime_error("Invalid bool parameter.");
		return V;
	};
	Common["IsTriggerActive"] = [](const UStateMachineAnimInstance &I, const FString &N) {
		bool V = false;
		if (!I.TryGetTrigger(N, V))
			throw std::runtime_error("Invalid trigger parameter.");
		return V;
	};
	Common["GetFloat"] = [](const UStateMachineAnimInstance &I, const FString &N) {
		float V = 0;
		if (!I.TryGetFloat(N, V))
			throw std::runtime_error("Invalid float parameter.");
		return V;
	};

	LuaState.new_usertype<FLuaAnimConditionContext>(
	    "AnimConditionContext",
	    sol::no_constructor,

	    "GetBool",
	    &FLuaAnimConditionContext::GetBool,

	    "GetFloat",
	    &FLuaAnimConditionContext::GetFloat,

	    "IsTriggerActive",
	    &FLuaAnimConditionContext::IsTriggerActive,

	    "IsSourceFinished",
	    &FLuaAnimConditionContext::IsSourceFinished,

	    "GetStateElapsed",
	    &FLuaAnimConditionContext::GetStateElapsed);

	auto AnimType = LuaState.new_usertype<ULuaAnimInstance>(
	    "LuaAnimInstance",
	    sol::no_constructor, sol::base_classes, sol::bases<UStateMachineAnimInstance>());

	AnimType["CreateSequencePlayer"] =
	    [](ULuaAnimInstance& Anim,
	       const FString& Name,
	       sol::optional<sol::table> Options)
	{
		return Anim.CreateSequencePlayer(
		    Name, ReadPlayerOptions(Options));
	};

	AnimType["CreateBlendSpacePlayer"] =
	    [](ULuaAnimInstance& Anim,
	       const FString& Name,
	       sol::optional<sol::table> Options)
	{
		return Anim.CreateBlendSpacePlayer(
		    Name, ReadPlayerOptions(Options));
	};

	AnimType["AddState"] =
	    &ULuaAnimInstance::AddState;

    AnimType["CreateTwoWayBlend"] =
        [](ULuaAnimInstance& Anim, FAnimNodeId A, FAnimNodeId B, sol::optional<float> Alpha)
        { return Anim.CreateTwoWayBlend(A, B, Alpha.value_or(0.0f)); };
    AnimType["SetBlendAlpha"] = &ULuaAnimInstance::SetBlendAlpha;

	AnimType["SetEntryState"] =
	    &ULuaAnimInstance::SetEntryState;

	// Lua 함수 → 기존 Custom 조건
	AnimType["AddCondition"] = &AddLuaCondition;

	AnimType["AddTransition"] =
	    [](ULuaAnimInstance& Anim,
	       FAnimStateId From,
	       FAnimStateId To,
	       const sol::table& Options)
	{
		return Anim.AddTransition(
		    From, To, ReadTransitionOptions(Options));
	};

	AnimType["AddAnyStateTransition"] =
	    [](ULuaAnimInstance& Anim,
	       FAnimStateId To,
	       const sol::table& Options)
	{
		return Anim.AddAnyStateTransition(
		    To, ReadTransitionOptions(Options));
	};

	AnimType["SetBool"] = &ULuaAnimInstance::SetBool;
	AnimType["SetFloat"] = &ULuaAnimInstance::SetFloat;

	AnimType["DefineTrigger"] = &ULuaAnimInstance::DefineTrigger;
	AnimType["SetTrigger"] = &ULuaAnimInstance::SetTrigger;
	AnimType["ResetTrigger"] = &ULuaAnimInstance::ResetTrigger;

	AnimType["SetBlendSpaceInput"] =
	    &ULuaAnimInstance::SetBlendSpaceInput;

	AnimType["SetPlayRate"] =
	    &ULuaAnimInstance::SetPlayRate;

	AnimType["SetPlaying"] =
	    &ULuaAnimInstance::SetPlaying;

	AnimType["GetCurrentState"] =
	    &ULuaAnimInstance::GetCurrentState;

	AnimType["IsSourceFinished"] =
	    &ULuaAnimInstance::IsSourceFinished;

	AnimType["IsGraphReady"] =
	    &ULuaAnimInstance::IsGraphReady;

	AnimType["GetLastError"] =
	    &ULuaAnimInstance::GetLastError;

	AnimType["GetFloat"] =
	    [](const ULuaAnimInstance& Anim, const FString& Name)
	{
		float Value = 0.0f;

		if (!Anim.TryGetFloat(Name, Value))
			throw std::runtime_error("Invalid float parameter.");

		return Value;
	};

	AnimType["GetBool"] =
	    [](const ULuaAnimInstance& Anim, const FString& Name)
	{
		bool Value = false;

		if (!Anim.TryGetBool(Name, Value))
			throw std::runtime_error("Invalid bool parameter.");

		return Value;
	};

	AnimType["IsTriggerActive"] =
	    [](const ULuaAnimInstance& Anim, const FString& Name)
	{
		bool Value = false;

		if (!Anim.TryGetTrigger(Name, Value))
			throw std::runtime_error("Invalid trigger parameter.");

		return Value;
	};
}
