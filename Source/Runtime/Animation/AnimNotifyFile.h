#pragma once

#include "Core/EngineString.h"
#include "Containers/Array.h"

class UAnimSequence;

class FAnimNotifyFile
{
public:
	static FString GetNotifyPath(const FString& FbxPath);

	static bool Load(const FString& FbxPath, const TArray<UAnimSequence*>& Sequences);
	static bool Save(const FString& FbxPath, const TArray<UAnimSequence*>& Sequences);

private:
	FAnimNotifyFile() = delete;
};
