#pragma once

#include <filesystem>
#include <optional>

struct FFbxCookedData;

class FSkeletalMeshBake
{
public:
	static bool ReadBaked(const FString& SourcePath, std::optional<std::filesystem::file_time_type> ExpectedSourceTime, FFbxCookedData& OutData);
	static void WriteBaked(const FString& SourcePath, std::filesystem::file_time_type SourceTime, const FFbxCookedData& Data);
	static bool WriteBakedDirect(const std::filesystem::path& TargetBinPath, const FFbxCookedData& Data, int64 Stamp = 0);
};
