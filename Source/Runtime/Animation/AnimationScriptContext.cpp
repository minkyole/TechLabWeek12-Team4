#include "EnginePCH.h"
#include "AnimationScriptContext.h"
#include "Engine/Engine.h"
#include "Scripting/LuaVM.h"

bool FAnimationScriptContext::Load(const FString &Path, bool RequireInitialize, bool RequireUpdate, FString &Error)
{
	Reset();
	auto *vm = GEngine ? GEngine->GetLuaVM() : nullptr;
	auto *lua = vm ? vm->GetState() : nullptr;
	if (!lua)
	{
		Error = "Lua VM is not initialized.";
		return false;
	}
	Environment = sol::environment(*lua, sol::create, lua->globals());
	auto result = lua->safe_script_file(Path, Environment, sol::script_pass_on_error);
	if (!result.valid())
	{
		sol::error e = result;
		Error = e.what();
		return false;
	}
	auto function = [&](const char *Name, bool Required, sol::protected_function &Out) {
		sol::object value = Environment[Name];
		if (value.get_type() == sol::type::nil && !Required)
			return true;
		if (value.get_type() != sol::type::function)
		{
			Error = FString(Name) + " must be a function.";
			return false;
		}
		Out = value.as<sol::protected_function>();
		return true;
	};
	return function("InitializeAnimation", RequireInitialize, Initialize) &&
		   function("UpdateAnimation", RequireUpdate, Update);
}
