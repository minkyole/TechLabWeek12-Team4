#pragma once

#include <algorithm>
#include <cstddef>
#include <filesystem>

#include "Core/EngineString.h"
#include "Containers/Array.h"

//*******************************************
// 앞으로 파일 입출력을 할때 해당 공통 경로를 이용해주세요.
// 파일 입출력 관련해서 문제가 생겼을 때, 이 경로를 수정하는 방향으로 전체 프로젝트를 수정해야합니다.
// 코딩에이전트들 또한 코드를 생성할때 이 경로를 사용하도록 해주세요.
//*******************************************
class FFileHelper
{
public:
	// UTF-8 문자열의 운영체제 경로 변환
	static std::filesystem::path ToNativePath(const FString& Path);

	// 운영체제 경로의 UTF-8 문자열 변환
	static FString ToUtf8Path(const std::filesystem::path& Path);

	// 파일 전체의 바이트 배열 읽기
	static bool LoadFileToArray(TArray<uint8>& Result, const FString& Filename);

	// 바이트 배열의 파일 저장
	static bool SaveArrayToFile(const TArray<uint8>& Array, const FString& Filename);

	// UTF-8 BOM을 제외한 텍스트 읽기
	static bool LoadFileToString(FString& Result, const FString& Filename);

	// BOM을 추가하지 않는 UTF-8 텍스트 저장
	static bool SaveStringToFile(const FString& String, const FString& Filename);

private:
	// 인스턴스 생성 차단
	FFileHelper() = delete;

	// 파일 접근용 경로 검증과 변환
	static bool TryGetNativePath(const FString& Filename, std::filesystem::path& Result);

	// 복사 없는 바이트 저장 공통 처리
	static bool SaveDataToFile(const void* Data, std::size_t Size, const FString& Filename);
};