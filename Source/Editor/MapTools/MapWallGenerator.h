#pragma once

#include "Core/Types.h"
#include "Core/EngineString.h"

class AActor;
class UScriptComponent;

struct FMapWallGenerateResult
{
	bool bSuccess = false;
	int32 RemovedActors = 0;
	int32 WallActors = 0;
	int32 FloorActors = 0;
	int32 CeilingActors = 0;
	int32 OpenEdgeCells = 0;
	FString Message;
};

class FMapWallGenerator
{
public:
	static bool IsMapActorScript(const UScriptComponent& ScriptComponent);
	static FMapWallGenerateResult Generate(AActor& MapActor);
};
