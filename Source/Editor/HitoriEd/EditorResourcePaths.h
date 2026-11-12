#pragma once

// 에디터 UI가 쓰는 파일 경로. 모두 Resources 아래에 있어야 한다.
// 엔진 공용 경로는 Runtime/Core/ResourcePaths.h에 있다.
namespace EditorResourcePaths
{
	// ImGui 한글 UI 폰트
	constexpr const char* UIFont = "Resources/Fonts/HMKMRHD.TTF";

	// Content Drawer가 보여주는 사용자 콘텐츠 루트와 씬 저장 기본 폴더
	constexpr const char* AssetRoot = "Assets";
	constexpr const char* SceneFolder = "Assets/Scenes";

	constexpr const char* FolderIcon = "Resources/Icons/Folder.png";
	constexpr const char* FileIcon = "Resources/Icons/File.png";

	constexpr const char* ToolbarPlayIcon = "Resources/Icons/Toolbar/Play.png";
	constexpr const char* ToolbarPauseIcon = "Resources/Icons/Toolbar/Pause.png";
	constexpr const char* ToolbarStepIcon = "Resources/Icons/Toolbar/Step.png";
	constexpr const char* ToolbarStopIcon = "Resources/Icons/Toolbar/Stop.png";
}
