#include "HeaderLexer.h"

#include <cctype>
#include <stdexcept>
#include <utility>

// 원문 참조와 독립적인 진단용 파일명 보관
FHeaderLexer::FHeaderLexer(std::string_view InSource, std::string_view InFileName)
    : Source(InSource), FileName(InFileName)
{
}

// 원문 시작부터의 반복 가능한 토큰 생성
std::vector<FHeaderToken> FHeaderLexer::Tokenize()
{
	// UTF-8 BOM 제거
	Offset = Source.starts_with("\xEF\xBB\xBF") ? 3 : 0;
	Line = 1;
	bLineStart = true;
	std::vector<FHeaderToken> Tokens;
	while (true)
	{
		SkipIgnored();
		if (Offset == Source.size())
		{
			Tokens.push_back({ EHeaderTokenType::EndOfFile, {}, Line });
			return Tokens;
		}
		if (bLineStart && Source[Offset] == '#')
		{
			// 매크로 본문과 include 정보를 보존하는 단일 지시문 토큰
			const std::size_t Begin = Offset;
			const std::size_t TokenLine = Line;
			while (Offset < Source.size() && Source[Offset] != '\n')
			{
				if (SkipLineSplice() || SkipComment())
				{
					continue;
				}
				ReadToken();
			}
			Tokens.push_back({ EHeaderTokenType::Directive, std::string(Source.substr(Begin, Offset - Begin)), TokenLine });
			bLineStart = false;
		}
		else
		{
			Tokens.push_back(ReadToken());
		}
	}
}

// 리터럴과 식별자 및 숫자 경계의 공통 판독
FHeaderToken FHeaderLexer::ReadToken()
{
	const std::size_t Begin = Offset;
	const std::size_t TokenLine = Line;
	const unsigned char Character = static_cast<unsigned char>(Source[Offset]);
	EHeaderTokenType Type = EHeaderTokenType::Symbol;
	std::string Text;
	if (TryReadLiteral())
	{
		Type = EHeaderTokenType::Literal;
	}
	else if (std::isalpha(Character) || Character == '_')
	{
		Type = EHeaderTokenType::Identifier;
		while (Offset < Source.size())
		{
			if (SkipLineSplice())
			{
				continue;
			}
			const unsigned char Current = static_cast<unsigned char>(Source[Offset]);
			if (!std::isalnum(Current) && Current != '_')
			{
				break;
			}
			Text.push_back(Source[Offset]);
			Advance();
		}
	}
	else if (std::isdigit(Character) || (Character == '.' && Offset + 1 < Source.size() && std::isdigit(static_cast<unsigned char>(Source[Offset + 1]))))
	{
		// 수치 해석 없이 보존하는 숫자와 접미사
		Type = EHeaderTokenType::Literal;
		Text.push_back(Source[Offset]);
		Advance();
		while (Offset < Source.size())
		{
			if (SkipLineSplice())
			{
				continue;
			}
			const char Current = Source[Offset];
			const char Previous = Text.back();
			const bool bExponentSign = (Current == '+' || Current == '-') && (Previous == 'e' || Previous == 'E' || Previous == 'p' || Previous == 'P');
			if (!std::isalnum(static_cast<unsigned char>(Current)) && Current != '_' && Current != '.' && Current != '\'' && !bExponentSign)
			{
				break;
			}
			Text.push_back(Current);
			Advance();
		}
	}
	else
	{
		Advance();
	}

	if (Text.empty())
	{
		Text = Source.substr(Begin, Offset - Begin);
	}
	bLineStart = false;
	return { Type, std::move(Text), TokenLine };
}

// LF 기준의 물리 행 추적과 다음 위치 이동
void FHeaderLexer::Advance(std::size_t Count)
{
	for (std::size_t Index = 0; Index < Count; ++Index)
	{
		if (Source[Offset++] == '\n')
		{
			++Line;
			bLineStart = true;
		}
	}
}

// 토큰 경계에 걸친 줄 연결을 포함한 물리 길이 계산
std::size_t FHeaderLexer::Match(std::string_view Text) const
{
	std::size_t Position = Offset;
	for (const char Expected : Text)
	{
		while (true)
		{
			const std::string_view Tail = Source.substr(Position);
			const std::size_t SpliceLength = Tail.starts_with("\\\r\n") ? 3 : (Tail.starts_with("\\\n") ? 2 : 0);
			if (SpliceLength == 0)
			{
				break;
			}
			Position += SpliceLength;
		}
		if (Position == Source.size() || Source[Position] != Expected)
		{
			return 0;
		}
		++Position;
	}
	return Position - Offset;
}

// LF와 CRLF 형식의 줄 연결 처리
bool FHeaderLexer::SkipLineSplice()
{
	const std::string_view Tail = Source.substr(Offset);
	const std::size_t Length = Tail.starts_with("\\\r\n") ? 3 : (Tail.starts_with("\\\n") ? 2 : 0);
	const bool bWasLineStart = bLineStart;
	Advance(Length);
	bLineStart = bWasLineStart;
	return Length != 0;
}

// 문자열 본문과 분리된 두 종류의 주석 처리
bool FHeaderLexer::SkipComment()
{
	if (const std::size_t Length = Match("//"))
	{
		Advance(Length);
		while (Offset < Source.size() && Source[Offset] != '\n')
		{
			if (!SkipLineSplice())
			{
				Advance();
			}
		}
		return true;
	}
	if (const std::size_t Length = Match("/*"))
	{
		const std::size_t StartLine = Line;
		Advance(Length);
		while (Offset < Source.size())
		{
			if (const std::size_t ClosingLength = Match("*/"))
			{
				Advance(ClosingLength);
				return true;
			}
			Advance();
		}
		Error(StartLine, "Unterminated block comment");
	}
	return false;
}

// 공백과 주석 뒤의 첫 실제 토큰 위치 탐색
void FHeaderLexer::SkipIgnored()
{
	while (Offset < Source.size())
	{
		if (std::isspace(static_cast<unsigned char>(Source[Offset])))
		{
			Advance();
			continue;
		}
		if (SkipLineSplice() || SkipComment())
		{
			continue;
		}
		break;
	}
}

// 일반 리터럴과 사용자 구분자 raw 리터럴의 구분
bool FHeaderLexer::TryReadLiteral()
{
	std::size_t OpeningLength = 0;
	bool bRaw = false;
	char Quote = '\0';
	for (const std::string_view Prefix : { "R\"", "u8R\"", "uR\"", "UR\"", "LR\"" })
	{
		OpeningLength = Match(Prefix);
		if (OpeningLength != 0)
		{
			bRaw = true;
			break;
		}
	}
	if (!bRaw)
	{
		for (const std::string_view Prefix : { "\"", "'", "u8\"", "u8'", "u\"", "u'", "U\"", "U'", "L\"", "L'" })
		{
			OpeningLength = Match(Prefix);
			if (OpeningLength != 0)
			{
				Quote = Prefix.back();
				break;
			}
		}
	}
	if (OpeningLength == 0)
	{
		return false;
	}

	const std::size_t StartLine = Line;
	Advance(OpeningLength);
	if (bRaw)
	{
		const std::size_t DelimiterBegin = Offset;
		while (Offset < Source.size() && Source[Offset] != '(')
		{
			if (Offset - DelimiterBegin == 16 || std::string_view(" \t\r\n\v\f)\\").find(Source[Offset]) != std::string_view::npos)
			{
				Error(StartLine, "Invalid raw string delimiter");
			}
			Advance();
		}
		if (Offset == Source.size())
		{
			Error(StartLine, "Unterminated raw string literal");
		}
		const std::string Closing = ")" + std::string(Source.substr(DelimiterBegin, Offset - DelimiterBegin)) + "\"";
		const std::size_t End = Source.find(Closing, Offset + 1);
		if (End == std::string_view::npos)
		{
			Error(StartLine, "Unterminated raw string literal");
		}
		Advance(End + Closing.size() - Offset);
		return true;
	}

	// 줄 연결을 먼저 제거하는 이스케이프 상태 추적
	bool bEscaped = false;
	while (Offset < Source.size())
	{
		if (SkipLineSplice())
		{
			continue;
		}
		const char Current = Source[Offset];
		if (!bEscaped && Current == Quote)
		{
			Advance();
			return true;
		}
		if (Current == '\n' || Current == '\r')
		{
			Error(StartLine, "Unterminated quoted literal");
		}
		bEscaped = !bEscaped && Current == '\\';
		Advance();
	}
	Error(StartLine, "Unterminated quoted literal");
}

// 공통 실행 진입점으로 전달하는 파일별 진단
void FHeaderLexer::Error(std::size_t ErrorLine, std::string_view Message) const
{
	throw std::runtime_error(FileName + ":" + std::to_string(ErrorLine) + ": " + std::string(Message));
}