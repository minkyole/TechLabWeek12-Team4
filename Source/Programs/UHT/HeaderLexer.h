#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

// 후속 선언 분석에 필요한 토큰의 구분
enum class EHeaderTokenType
{
	Identifier,
	Literal,
	Symbol,
	Directive,
	EndOfFile
};

// 종류와 텍스트 및 시작 행을 보관하는 분석 단위
struct FHeaderToken
{
	EHeaderTokenType Type;
	std::string Text;
	std::size_t Line;
};

// 주석과 리터럴을 구분하는 헤더의 어휘 분석기
class FHeaderLexer
{
public:
	// 호출자가 유지하는 원문과 진단용 파일명의 연결
	FHeaderLexer(std::string_view InSource, std::string_view InFileName);

	// EOF를 포함한 토큰 목록의 생성
	std::vector<FHeaderToken> Tokenize();

private:
	// 지시문과 일반 코드가 공유하는 단일 토큰 판독
	FHeaderToken ReadToken();

	// 물리 행 번호를 반영하는 지정 바이트 전진
	void Advance(std::size_t Count = 1);

	// 줄 연결을 제외한 접두어 일치와 물리 길이 계산
	std::size_t Match(std::string_view Text) const;

	// 역슬래시와 개행으로 구성된 줄 연결의 소비
	bool SkipLineSplice();

	// 줄 주석과 블록 주석의 소비
	bool SkipComment();

	// 토큰 사이의 공백과 주석 제거
	void SkipIgnored();

	// 접두어를 포함한 문자열과 문자 리터럴의 소비
	bool TryReadLiteral();

	// 파일명과 시작 행을 포함한 분석 오류
	[[noreturn]] void Error(std::size_t ErrorLine, std::string_view Message) const;

	std::string_view Source;
	std::string FileName;
	std::size_t Offset = 0;
	std::size_t Line = 1;
	bool bLineStart = true;
};