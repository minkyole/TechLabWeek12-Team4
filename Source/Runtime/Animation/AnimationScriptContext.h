#pragma once
#include <sol/sol.hpp>
// Per-instance Lua references. Call Reset only after graph callbacks are released.
struct FAnimationScriptContext
{
	sol::environment Environment;
	sol::protected_function Initialize, Update;
	bool Load(const FString &Path, bool RequireInitialize, bool RequireUpdate, FString &Error);
	void Reset()
	{
		Initialize = {};
		Update = {};
		Environment = {};
	}
};
