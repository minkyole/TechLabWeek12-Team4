#pragma once

#include <filesystem>
#include <vector>
#include <string_view>

// 헤더 도구의 실행과 입력 헤더 수집 책임
class FHeaderTool
{
public:
	// 명령행 입력을 사용하는 단일 실행 흐름
	int Run(int ArgumentCount, wchar_t* Arguments[]);

private:
	// 프로젝트 루트 인자의 해석
	void ParseArguments(int ArgumentCount, wchar_t* Arguments[]);

	// 런타임과 에디터의 원본 헤더 수집
	void DiscoverHeaders();

	// 생성 결과의 UTF-8 바이트 저장
	void SaveGeneratedFile(const std::filesystem::path& Path, std::string_view Text) const;
		
	// 현재 출력 목록에서 제외된 도구 소유 파일의 정리
	void RemoveStaleGeneratedFiles(const std::vector<std::filesystem::path>& ExpectedFiles) const;

	// 절대 경로로 정규화한 프로젝트 루트
	std::filesystem::path ProjectRoot;

	// 프로젝트 루트 기준의 정렬된 상대 경로 목록
	std::vector<std::filesystem::path> HeaderFiles;

	// 생성 파일을 모으는 빌드 출력 디렉터리
	std::filesystem::path OutputDirectory;
};