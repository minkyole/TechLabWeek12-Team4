#include "HeaderParser.h"

#include <algorithm>
#include <cctype>
#include <stdexcept>
#include <utility>

// EOF 계약과 헤더별 생성 파일명의 준비
FHeaderParser::FHeaderParser(const std::vector<FHeaderToken>& InTokens, std::string_view InFileName)
    : Tokens(InTokens), FileName(InFileName)
{
	if (Tokens.empty() || Tokens.back().Type != EHeaderTokenType::EndOfFile)
	{
		throw std::invalid_argument("Parser requires an EOF-terminated token list");
	}
	const std::size_t Slash = FileName.find_last_of("/\\");
	const std::string BaseName = FileName.substr(Slash == std::string::npos ? 0 : Slash + 1);
	GeneratedHeader = BaseName.substr(0, BaseName.find_last_of('.')) + ".generated.h";
}

// 반영 마커가 있는 헤더의 클래스 수집과 파일 단위 검증
std::vector<FHeaderClass> FHeaderParser::Parse()
{
	Position = 0;
	ConditionalDepth = 0;
	LastIncludeLine = 0;
	GeneratedIncludeLine = 0;
	GeneratedIncludeCount = 0;
	LineDirectiveLine = 0;
	std::vector<FHeaderClass> Classes;
	if (std::none_of(Tokens.begin(), Tokens.end(), [this](const FHeaderToken& Token)
	                 { return IsMarker(Token); }))
	{
		return Classes;
	}

	bool bDeclarationStart = true;
	while (Peek().Type != EHeaderTokenType::EndOfFile)
	{
		if (Peek().Type == EHeaderTokenType::Directive)
		{
			ReadDirective();
		}
		else if (Is("UCLASS"))
		{
			if (!bDeclarationStart || ConditionalDepth != 0)
			{
				Error(Peek().Line, "UCLASS must begin an unconditional global declaration");
			}
			FHeaderClass Class = ReadClass();
			for (const FHeaderClass& Existing : Classes)
			{
				if (SameName(Existing.Name, Class.Name) || Existing.GeneratedBodyLine == Class.GeneratedBodyLine)
				{
					Error(Class.Line, "Duplicate class name or GENERATED_BODY line");
				}
			}
			Classes.push_back(std::move(Class));
			bDeclarationStart = true;
		}
		else if (IsMarker(Peek()))
		{
			Error(Peek().Line, "Reflection marker outside a supported UCLASS declaration");
		}
		else if (Is("{") || Is("(") || Is("["))
		{
			const bool bBody = Is("{");
			SkipGroup();
			bDeclarationStart = bBody;
		}
		else
		{
			if (Is("}") || Is(")") || Is("]"))
			{
				Error(Peek().Line, "Unexpected closing delimiter");
			}
			bDeclarationStart = Is(";");
			++Position;
		}
	}
	if (ConditionalDepth != 0)
	{
		Error(Peek().Line, "Unclosed conditional directive");
	}
	if (!Classes.empty())
	{
		if (GeneratedIncludeCount != 1 || GeneratedIncludeLine != LastIncludeLine || GeneratedIncludeLine > Classes.front().Line)
		{
			Error(Classes.front().Line, "Require exactly one \"" + GeneratedHeader + "\" as the last include before UCLASS");
		}
		if (LineDirectiveLine != 0)
		{
			Error(LineDirectiveLine, "#line is unsupported in reflected headers");
		}
	}
	return Classes;
}

// 잘린 입력에서도 EOF를 반환하는 조회 경계
const FHeaderToken& FHeaderParser::Peek(std::size_t Lookahead) const
{
	return Tokens[std::min(Position + Lookahead, Tokens.size() - 1)];
}

// 리터럴과 지시문을 제외한 토큰 비교
bool FHeaderParser::Is(std::string_view Text) const
{
	return (Peek().Type == EHeaderTokenType::Identifier || Peek().Type == EHeaderTokenType::Symbol) && Peek().Text == Text;
}

// 예상 문법 요소의 선택적 소비
bool FHeaderParser::Accept(std::string_view Text)
{
	if (!Is(Text))
	{
		return false;
	}
	++Position;
	return true;
}

// 필수 문법 요소 누락의 공통 진단
void FHeaderParser::Expect(std::string_view Text)
{
	if (!Accept(Text))
	{
		Error(Peek().Line, "Expected '" + std::string(Text) + "'");
	}
}

// 클래스명과 타입명 및 변수명의 공통 판독
std::string FHeaderParser::ReadIdentifier()
{
	if (Peek().Type != EHeaderTokenType::Identifier)
	{
		Error(Peek().Line, "Expected an identifier");
	}
	return Tokens[Position++].Text;
}

// 지정자 문법과 허용 목록 검증의 단일 경계
std::vector<std::string> FHeaderParser::ReadSpecifiers(std::initializer_list<std::string_view> Allowed)
{
	Expect("(");
	std::vector<std::string> Result;
	if (!Accept(")"))
	{
		do
		{
			const std::size_t Line = Peek().Line;
			std::string Value = ReadIdentifier();
			if (std::find(Allowed.begin(), Allowed.end(), Value) == Allowed.end() || std::find(Result.begin(), Result.end(), Value) != Result.end())
			{
				Error(Line, "Unsupported or duplicate specifier '" + Value + "'");
			}
			Result.push_back(std::move(Value));
		} while (Accept(","));
		Expect(")");
	}
	return Result;
}

// 기존 Lexer를 재사용한 전처리 지시문 판독
void FHeaderParser::ReadDirective()
{
	const FHeaderToken& Directive = Tokens[Position++];
	FHeaderLexer Lexer(std::string_view(Directive.Text).substr(1), FileName);
	const std::vector<FHeaderToken> Parts = Lexer.Tokenize();
	const std::string& Command = Parts.front().Text;
	if (Command == "if" || Command == "ifdef" || Command == "ifndef")
	{
		++ConditionalDepth;
	}
	else if (Command == "endif")
	{
		if (ConditionalDepth == 0)
		{
			Error(Directive.Line, "Unexpected #endif");
		}
		--ConditionalDepth;
	}
	else if ((Command == "else" || Command == "elif") && ConditionalDepth == 0)
	{
		Error(Directive.Line, "Conditional branch without #if");
	}
	else if (Command == "line")
	{
		LineDirectiveLine = Directive.Line;
	}
	else if (Command == "include")
	{
		LastIncludeLine = Directive.Line;
		if (Parts.size() > 1 && Parts[1].Type == EHeaderTokenType::Literal && Parts[1].Text.starts_with('"') && Parts[1].Text.ends_with('"'))
		{
			std::string Name = Parts[1].Text.substr(1, Parts[1].Text.size() - 2);
			for (const std::string_view Splice : { "\\\r\n", "\\\n" })
			{
				for (std::size_t Index = Name.find(Splice); Index != std::string::npos; Index = Name.find(Splice, Index))
				{
					Name.erase(Index, Splice.size());
				}
			}
			if (Name.ends_with(".generated.h"))
			{
				if (Name != GeneratedHeader || ConditionalDepth != 0)
				{
					Error(Directive.Line, "Expected an unconditional include of \"" + GeneratedHeader + "\"");
				}
				++GeneratedIncludeCount;
				GeneratedIncludeLine = Directive.Line;
			}
		}
	}
}

// 비반영 코드와 초기화 식의 괄호 영역 건너뛰기
void FHeaderParser::SkipGroup()
{
	const std::size_t Line = Peek().Line;
	const std::string Closing = Is("{") ? "}" : (Is("(") ? ")" : "]");
	++Position;
	while (!Accept(Closing))
	{
		if (Peek().Type == EHeaderTokenType::EndOfFile)
		{
			Error(Line, "Unclosed delimiter group");
		}
		if (Peek().Type == EHeaderTokenType::Directive)
		{
			ReadDirective();
		}
		else if (IsMarker(Peek()))
		{
			Error(Peek().Line, "Reflection markers require a direct UCLASS member position");
		}
		else if (Is("{") || Is("(") || Is("["))
		{
			SkipGroup();
		}
		else if (Is("}") || Is(")") || Is("]"))
		{
			Error(Peek().Line, "Mismatched closing delimiter");
		}
		else
		{
			++Position;
		}
	}
}

// 지원하지 않는 반영 선언도 조용히 누락하지 않는 마커 검사
bool FHeaderParser::IsMarker(const FHeaderToken& Token) const
{
	return Token.Type == EHeaderTokenType::Identifier &&
	       (Token.Text == "UCLASS" || Token.Text == "UPROPERTY" || Token.Text == "GENERATED_BODY" || Token.Text == "USTRUCT" || Token.Text == "UENUM" || Token.Text == "UINTERFACE" || Token.Text == "UFUNCTION");
}

// 반영 키에서 충돌하는 이름의 공통 비교
bool FHeaderParser::SameName(std::string_view Left, std::string_view Right) const
{
	return std::equal(Left.begin(), Left.end(), Right.begin(), Right.end(), [](unsigned char A, unsigned char B)
	                  { return std::tolower(A) == std::tolower(B); });
}

// 단일 상속 클래스와 직접 멤버의 선언 분석
FHeaderClass FHeaderParser::ReadClass()
{
	FHeaderClass Result;
	Result.Line = Peek().Line;
	Expect("UCLASS");
	Result.Specifiers = ReadSpecifiers({ "Abstract", "Placeable", "NotPlaceable" });
	if (std::find(Result.Specifiers.begin(), Result.Specifiers.end(), "Placeable") != Result.Specifiers.end() &&
	    std::find(Result.Specifiers.begin(), Result.Specifiers.end(), "NotPlaceable") != Result.Specifiers.end())
	{
		Error(Result.Line, "Placeable and NotPlaceable cannot be combined");
	}
	Expect("class");
	if (Peek().Text.ends_with("_API") && Peek(1).Type == EHeaderTokenType::Identifier && Peek(1).Text != "final")
	{
		++Position;
	}
	Result.Name = ReadIdentifier();
	Accept("final");
	Expect(":");
	Expect("public");
	Result.SuperName = ReadIdentifier();
	Expect("{");
	bool bMemberStart = true;
	while (!Accept("}"))
	{
		if (Peek().Type == EHeaderTokenType::EndOfFile)
		{
			Error(Result.Line, "Unclosed UCLASS body");
		}
		if (Peek().Type == EHeaderTokenType::Directive)
		{
			ReadDirective();
		}
		else if ((Is("public") || Is("protected") || Is("private")) && Peek(1).Text == ":")
		{
			Position += 2;
			bMemberStart = true;
		}
		else if (Is("GENERATED_BODY"))
		{
			if (!bMemberStart || ConditionalDepth != 0 || Result.GeneratedBodyLine != 0)
			{
				Error(Peek().Line, "Require one unconditional GENERATED_BODY");
			}
			Result.GeneratedBodyLine = Peek().Line;
			++Position;
			Expect("(");
			if (Peek().Line != Result.GeneratedBodyLine)
			{
				Error(Peek().Line, "GENERATED_BODY must fit on one physical line");
			}
			Expect(")");
			Accept(";");
			bMemberStart = true;
		}
		else if (Is("UPROPERTY"))
		{
			if (!bMemberStart)
			{
				Error(Peek().Line, "UPROPERTY must begin a member declaration");
			}
			FHeaderProperty Property = ReadProperty();
			if (std::any_of(Result.Properties.begin(), Result.Properties.end(), [this, &Property](const FHeaderProperty& Existing)
			                { return SameName(Existing.Name, Property.Name); }))
			{
				Error(Property.Line, "Duplicate reflected property name");
			}
			Result.Properties.push_back(std::move(Property));
			bMemberStart = true;
		}
		else if (IsMarker(Peek()))
		{
			Error(Peek().Line, "Unsupported reflection declaration inside UCLASS");
		}
		else if (Is("{") || Is("(") || Is("["))
		{
			const bool bBody = Is("{");
			SkipGroup();
			bMemberStart = bBody;
		}
		else
		{
			if (Is(")") || Is("]"))
			{
				Error(Peek().Line, "Unexpected closing delimiter in UCLASS");
			}
			bMemberStart = Is(";");
			++Position;
		}
	}
	Expect(";");
	if (Result.GeneratedBodyLine == 0)
	{
		Error(Result.Line, "Missing GENERATED_BODY");
	}
	return Result;
}

// 단일 값 또는 단일 객체 포인터 멤버의 수집
FHeaderProperty FHeaderParser::ReadProperty()
{
	FHeaderProperty Result;
	Result.Line = Peek().Line;
	if (ConditionalDepth != 0)
	{
		Error(Result.Line, "Conditional UPROPERTY is unsupported");
	}
	Expect("UPROPERTY");
	Result.Specifiers = ReadSpecifiers({ "EditAnywhere", "VisibleAnywhere", "Transient" });

	// 편집 가능 지정자와 읽기 전용 지정자의 동시 사용 차단
	const bool bEditAnywhere = std::find(Result.Specifiers.begin(), Result.Specifiers.end(), "EditAnywhere") != Result.Specifiers.end();
	const bool bVisibleAnywhere = std::find(Result.Specifiers.begin(), Result.Specifiers.end(), "VisibleAnywhere") != Result.Specifiers.end();
	if (bEditAnywhere && bVisibleAnywhere)
	{
		Error(Result.Line, "Choose EditAnywhere or VisibleAnywhere");
	}
	const bool bElaboratedClass = Accept("class");
	Result.TypeName = ReadIdentifier();
	if (Result.TypeName == "const" || Result.TypeName == "static" || Result.TypeName == "volatile" || Result.TypeName == "mutable" || Result.TypeName == "constexpr" || Result.TypeName == "auto")
	{
		Error(Result.Line, "Reflected properties require mutable instance fields with explicit types");
	}
	const bool bPointer = Accept("*");
	if (bElaboratedClass && !bPointer)
	{
		Error(Result.Line, "An elaborated class property must be a pointer");
	}
	if (bPointer)
	{
		Result.TypeName += "*";
	}
	Result.Name = ReadIdentifier();
	if (Accept("="))
	{
		bool bHasValue = false;
		while (!Accept(";"))
		{
			if (Peek().Type == EHeaderTokenType::EndOfFile || Is("}"))
			{
				Error(Result.Line, "Unterminated property initializer");
			}
			if (Is(")") || Is("]"))
			{
				Error(Peek().Line, "Unexpected closing delimiter in initializer");
			}
			if (Is(",") || IsMarker(Peek()))
			{
				Error(Peek().Line, "Top-level commas and extra reflection markers are unsupported in a property initializer");
			}
			if (Peek().Type == EHeaderTokenType::Directive)
			{
				ReadDirective();
			}
			else if (Is("{") || Is("(") || Is("["))
			{
				SkipGroup();
				bHasValue = true;
			}
			else
			{
				++Position;
				bHasValue = true;
			}
		}
		if (!bHasValue)
		{
			Error(Result.Line, "Missing property initializer");
		}
	}
	else
	{
		if (Is("{"))
		{
			SkipGroup();
		}
		Expect(";");
	}
	return Result;
}

// 기존 실행 진입점으로 전달하는 파일별 구문 진단
void FHeaderParser::Error(std::size_t Line, std::string_view Message) const
{
	throw std::runtime_error(FileName + ":" + std::to_string(Line) + ": " + std::string(Message));
}