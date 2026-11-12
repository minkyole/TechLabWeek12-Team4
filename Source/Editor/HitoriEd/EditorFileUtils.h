#pragma once

#include <filesystem>
#include "Engine/World.h"

class FEditorFileUtils
{
public:
	static bool NewScene(UWorld* World);
	static bool SaveScene(UWorld* World);
	static bool SaveSceneAs(UWorld* World);
	static bool LoadScene(UWorld* World);

private:
	// 씬 대화상자의 기본 절대 경로 구성
	static std::filesystem::path GetSceneFolderAbsolute();

	static FString OpenSaveSceneDialog();
	static FString OpenLoadSceneDialog();

	static FString CurrentScenePath;
};

