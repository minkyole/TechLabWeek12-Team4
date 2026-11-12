#include "EnginePCH.h"
#include "Editor/HitoriEd/EditorFileUtils.h"

#include "Core/EngineLog.h"
#include "Core/Misc/FileHelper.h"
#include <commdlg.h>
#include "Serialization/JsonArchive.h"
#include "Editor/HitoriEd/EditorResourcePaths.h"

FString FEditorFileUtils::CurrentScenePath = "";

// 운영체제 인코딩을 유지한 기본 씬 폴더 경로 구성
std::filesystem::path FEditorFileUtils::GetSceneFolderAbsolute()
{
	std::error_code Ec;
	const std::filesystem::path SceneFolder = FFileHelper::ToNativePath(EditorResourcePaths::SceneFolder);
	std::filesystem::create_directories(SceneFolder, Ec);
	return std::filesystem::absolute(SceneFolder, Ec).lexically_normal();
}

bool FEditorFileUtils::NewScene(UWorld* World)
{
	if (!World)
		return false;

	World->ClearWorld();

	CurrentScenePath.clear();
	World->SetScenePath("");

	return true;
}

bool FEditorFileUtils::SaveScene(UWorld* World)
{
	if (!World)
		return false;

	if (CurrentScenePath.empty())
	{
		return SaveSceneAs(World);
	}

	if (!FJsonArchive::SaveWorld(World, CurrentScenePath))
	{
		HTR_LOG(Error, "Failed to save scene: {}", CurrentScenePath);

		MessageBoxW(
			GetActiveWindow(),
			L"씬을 저장하지 못했습니다.\n경로와 쓰기 권한을 확인하세요.",
			L"저장 실패",
			MB_OK | MB_ICONERROR
		);
		return false;
	}
	HTR_LOG(Info, "Save Scene : {}", CurrentScenePath);

	return true;
}

bool FEditorFileUtils::SaveSceneAs(UWorld* World)
{
	if (!World)
	{
		return false;
	}

	FString FilePath = OpenSaveSceneDialog();

	if (FilePath.empty())
	{
		return false;
	}

	const std::filesystem::path NativePath = FFileHelper::ToNativePath(FilePath);
	FilePath = FFileHelper::ToUtf8Path(std::filesystem::absolute(NativePath).lexically_normal());

	if (!FJsonArchive::SaveWorld(World, FilePath))
	{
		HTR_LOG(Error, "Failed to save scene: {}", FilePath);

		MessageBoxW(
			GetActiveWindow(),
			L"씬을 저장하지 못했습니다.\n경로와 쓰기 권한을 확인하세요.",
			L"저장 실패",
			MB_OK | MB_ICONERROR
		);

		return false;
	}

	// 저장 성공 이후의 현재 씬 경로 확정
	CurrentScenePath = FilePath;
	World->SetScenePath(FilePath);
	HTR_LOG(Info, "Save Scene As : {}", CurrentScenePath);

	return true;
}

bool FEditorFileUtils::LoadScene(UWorld* World)
{
	if (!World)
	{
		return false;
	}

	FString FilePath = OpenLoadSceneDialog();

	// 파일 선택 취소
	if (FilePath.empty())
	{
		return false;
	}

	const std::filesystem::path NativePath = FFileHelper::ToNativePath(FilePath);
	FilePath = FFileHelper::ToUtf8Path(std::filesystem::absolute(NativePath).lexically_normal());

	if (!FJsonArchive::LoadWorld(World, FilePath))
	{
		HTR_LOG(Error, "Failed to load scene: {}", FilePath);
		return false;
	}

	// 불러오기 성공 이후의 현재 씬 경로 확정
	CurrentScenePath = FilePath;
	World->SetScenePath(FilePath);
	HTR_LOG(Info, "Load Scene : {}", CurrentScenePath);

	return true;
}

FString FEditorFileUtils::OpenSaveSceneDialog()
{
	// 긴 유니코드 경로를 위한 UTF-16 반환 버퍼
	constexpr DWORD FilePathCapacity = 32768;
	wchar_t FilePath[FilePathCapacity] = {};

	OPENFILENAMEW ofn = {};
	ofn.lStructSize = sizeof(OPENFILENAMEW);
	ofn.hwndOwner = nullptr;
	ofn.lpstrFile = FilePath;
	ofn.nMaxFile = FilePathCapacity;

	ofn.lpstrFilter = L"Scene Files (*.scene)\0*.scene\0";
	ofn.lpstrDefExt = L"scene";
	const std::wstring InitialDir = GetSceneFolderAbsolute().wstring();
	ofn.lpstrInitialDir = InitialDir.c_str();

	ofn.Flags =
		OFN_PATHMUSTEXIST |
		OFN_OVERWRITEPROMPT |
		OFN_NOCHANGEDIR;

	if (GetSaveFileNameW(&ofn))
	{
		return FFileHelper::ToUtf8Path(std::filesystem::path(FilePath));
	}

	return FString();
}

FString FEditorFileUtils::OpenLoadSceneDialog()
{
	// 긴 유니코드 경로를 위한 UTF-16 반환 버퍼
	constexpr DWORD FilePathCapacity = 32768;
	wchar_t FilePath[FilePathCapacity] = {};

	OPENFILENAMEW ofn = {};
	ofn.lStructSize = sizeof(OPENFILENAMEW);
	ofn.hwndOwner = nullptr;

	ofn.lpstrFile = FilePath;
	ofn.nMaxFile = FilePathCapacity;

	ofn.lpstrFilter =
		L"Scene Files (*.scene)\0*.scene\0"
		L"All Files (*.*)\0*.*\0";

	ofn.lpstrDefExt = L"scene";
	const std::wstring InitialDir = GetSceneFolderAbsolute().wstring();
	ofn.lpstrInitialDir = InitialDir.c_str();

	ofn.Flags =
		OFN_FILEMUSTEXIST |
		OFN_PATHMUSTEXIST |
		OFN_NOCHANGEDIR;

	if (GetOpenFileNameW(&ofn))
	{
		return FFileHelper::ToUtf8Path(std::filesystem::path(FilePath));
	}

	return FString();
}


