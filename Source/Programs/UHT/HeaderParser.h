#pragma once

#include "HeaderLexer.h"

#include <initializer_list>

// 프로퍼티 등록 코드 생성에 필요한 선언 정보
struct FHeaderProperty
{
	std::string Name;
	std::string TypeName;
	std::vector<std::string> Specifiers;
	std::size_t Line = 0;
};

// 클래스 등록과 생성 본문에 필요한 선언 정보
struct FHeaderClass
{
	std::string Name;
	std::string SuperName;
	std::vector<std::string> Specifiers;
	std::vector<FHeaderProperty> Properties;
	std::size_t Line = 0;
	std::size_t GeneratedBodyLine = 0;
};

// 토큰 목록에서 반영 선언을 수집하는 구문 분석기
class FHeaderParser
{
public:
	// 분석 동안 유지되는 토큰 목록과 진단 파일명의 연결
	FHeaderParser(const std::vector<FHeaderToken>& InTokens, std::string_view InFileName);

	// 파일에 선언된 반영 클래스 목록의 생성
	std::vector<FHeaderClass> Parse();

private:
	// EOF를 넘지 않는 토큰 조회
	const FHeaderToken& Peek(std::size_t Lookahead = 0) const;

	// 현재 토큰의 원문 비교
	bool Is(std::string_view Text) const;

	// 일치하는 토큰의 선택적 소비
	bool Accept(std::string_view Text);

	// 필수 토큰의 검사와 소비
	void Expect(std::string_view Text);

	// 식별자 토큰의 검사와 복사
	std::string ReadIdentifier();

	// 허용된 지정자 목록의 수집
	std::vector<std::string> ReadSpecifiers(std::initializer_list<std::string_view> Allowed);

	// include 위치와 조건부 깊이의 추적
	void ReadDirective();

	// 중첩 괄호 영역의 건너뛰기
	void SkipGroup();

	// 지원 대상 반영 마커의 판별
	bool IsMarker(const FHeaderToken& Token) const;

	// FName 계약에 맞춘 반영 이름의 대소문자 무시 비교
	bool SameName(std::string_view Left, std::string_view Right) const;

	// 단일 클래스 선언의 분석
	FHeaderClass ReadClass();

	// 단일 멤버 변수 선언의 분석
	FHeaderProperty ReadProperty();

	// 파일명과 행 번호를 포함한 구문 오류
	[[noreturn]] void Error(std::size_t Line, std::string_view Message) const;

	const std::vector<FHeaderToken>& Tokens;
	std::string FileName;
	std::string GeneratedHeader;
	std::size_t Position = 0;
	std::size_t ConditionalDepth = 0;
	std::size_t LastIncludeLine = 0;
	std::size_t GeneratedIncludeLine = 0;
	std::size_t GeneratedIncludeCount = 0;
	std::size_t LineDirectiveLine = 0;
};