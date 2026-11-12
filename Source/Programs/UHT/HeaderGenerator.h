#pragma once

#include "HeaderParser.h"

#include <filesystem>
#include <string_view>

// 생성 파일의 원본 경로와 선언 목록의 연결
struct FHeaderFile
{
	std::filesystem::path RelativePath;
	std::vector<FHeaderClass> Classes;
};

// 분석 결과를 등록 코드 문자열로 변환하는 생성기
class FHeaderGenerator
{
public:
	// 생성 헤더의 소유권 식별 표식
	static constexpr std::string_view GeneratedHeaderMarker = "// UHT 생성 파일의 클래스 본문 연결";

	// 생성 소스의 소유권 식별 표식
	static constexpr std::string_view GeneratedSourceMarker = "// UHT 생성 파일의 모듈별 프로퍼티 등록";

	// 출력 이름과 반영 이름 충돌의 일괄 검증
	void Validate(const std::vector<FHeaderFile>& Headers) const;

	// 원본 헤더가 속한 빌드 모듈의 판별
	std::string ModuleName(const std::filesystem::path& RelativePath) const;

	// 파일별 생성 본문 매크로의 출력
	std::string GenerateHeader(const FHeaderFile& Header) const;

	// 모듈별 등록 함수와 자동 등록 앵커의 출력
	std::string GenerateModule(const std::vector<FHeaderFile>& Headers, std::string_view Module) const;

private:
	// 경로의 슬래시 구분 UTF-8 표현
	std::string PathText(const std::filesystem::path& Path) const;

	// 프로젝트 상대 경로에 대한 결정적 파일 식별자
	std::string FileId(const std::filesystem::path& RelativePath) const;

	// 컴파일 대상의 include 경로에 맞춘 헤더 이름
	std::string IncludePath(const std::filesystem::path& RelativePath) const;

	// 클래스 지정자의 기존 열거형 표현
	std::string ClassSpecifiers(const FHeaderClass& Class) const;
};