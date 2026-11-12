#include "HeaderTool.h"
#include "HeaderLexer.h"
#include "HeaderParser.h"
#include "HeaderGenerator.h"

#include <algorithm>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <fstream>
#include <utility>

// 입력 해석과 헤더 탐색의 순차 실행
int FHeaderTool::Run(int ArgumentCount, wchar_t* Arguments[])
{
	if (ArgumentCount == 2 && std::wstring_view(Arguments[1]) == L"--help")
	{
		std::cout << "Usage: UHT.exe --root <ProjectRoot> [--output <Directory>]\n";
		return 0;
	}

	ParseArguments(ArgumentCount, Arguments);
	DiscoverHeaders();

	// 한글 경로를 포함한 입력 목록의 UTF-8 출력
	const std::u8string RootText = ProjectRoot.generic_u8string();
	std::cout << "Project: " << std::string(RootText.begin(), RootText.end()) << '\n';
	std::size_t TotalTokens = 0;
	std::size_t TotalClasses = 0;
	std::size_t TotalProperties = 0;
	std::vector<FHeaderFile> ReflectedHeaders;

	for (const std::filesystem::path& HeaderFile : HeaderFiles)
	{
		const std::u8string HeaderText = HeaderFile.generic_u8string();
		const std::string HeaderName(HeaderText.begin(), HeaderText.end());
		std::ifstream Input(ProjectRoot / HeaderFile, std::ios::binary | std::ios::ate);
		const std::streamoff SourceSize = Input.tellg();
		if (SourceSize < 0)
		{
			throw std::runtime_error(HeaderName + ": Cannot open or size header");
		}
		std::string SourceText(static_cast<std::size_t>(SourceSize), '\0');
		Input.seekg(0);
		if (!Input.read(SourceText.data(), static_cast<std::streamsize>(SourceText.size())))
		{
			throw std::runtime_error(HeaderName + ": Cannot read header");
		}

		// 원문 수명 안에서 수행하는 어휘 분석
		FHeaderLexer Lexer(SourceText, HeaderName);
		const std::vector<FHeaderToken> Tokens = Lexer.Tokenize();
		const std::size_t TokenCount = Tokens.size() - 1;
		TotalTokens += TokenCount;

		FHeaderParser Parser(Tokens, HeaderName);
		std::vector<FHeaderClass> Classes = Parser.Parse();
		TotalClasses += Classes.size();

		std::cout << HeaderName << ": " << TokenCount << " token(s), " << Classes.size() << " class(es).\n";

		for (const FHeaderClass& Class : Classes)
		{
			TotalProperties += Class.Properties.size();
			std::cout << "  class " << Class.Name << " : " << Class.SuperName << ", body line " << Class.GeneratedBodyLine << '\n';
			for (const FHeaderProperty& Property : Class.Properties)
			{
				std::cout << "    " << Property.TypeName << ' ' << Property.Name << '\n';
			}
		} 
		// 원본 경로와 분석 결과의 생성 단계 이관
		if (!Classes.empty())
		{
			ReflectedHeaders.push_back({ HeaderFile, std::move(Classes) });
		}
	}
	// 전체 입력 분석 성공 후의 출력 이름 검증
	FHeaderGenerator Generator;
	Generator.Validate(ReflectedHeaders);

	// 이번 실행에서 유지할 생성 파일 목록
	std::vector<std::filesystem::path> ExpectedFiles;

	// 반영 클래스가 있는 헤더의 생성 본문 저장
	for (const FHeaderFile& Header : ReflectedHeaders)
	{
		std::filesystem::path Name = Header.RelativePath.filename();
		Name.replace_extension(".generated.h");

		const std::filesystem::path OutputPath = OutputDirectory / Generator.ModuleName(Header.RelativePath) / Name;

		SaveGeneratedFile(OutputPath, Generator.GenerateHeader(Header));
		ExpectedFiles.push_back(OutputPath);
	}

	// 반영 클래스 개수와 무관한 모듈별 등록 파일 저장
	for (const std::string_view Module : { "Runtime", "Editor" })
	{
		const std::filesystem::path OutputPath = OutputDirectory / Module / (std::string(Module) + ".gen.cpp");

		SaveGeneratedFile(OutputPath, Generator.GenerateModule(ReflectedHeaders, Module));
		ExpectedFiles.push_back(OutputPath);
	}

	// 전체 저장 성공 후의 오래된 생성 파일 정리
	RemoveStaleGeneratedFiles(ExpectedFiles);

	std::cout << "Tokenized " << HeaderFiles.size() << " header(s), " << TotalTokens << " token(s).\n";
	std::cout << "Parsed " << TotalClasses << " class(es), " << TotalProperties << " property(ies).\n";

	const std::u8string OutputText = OutputDirectory.generic_u8string();
	std::cout << "Generated files: " << std::string(OutputText.begin(), OutputText.end()) << '\n';

	return 0;
}

// 고정된 명령 형식과 프로젝트 루트의 확정
void FHeaderTool::ParseArguments(int ArgumentCount, wchar_t* Arguments[])
{
	const bool bHasOutput = ArgumentCount == 5;
	if ((ArgumentCount != 3 && !bHasOutput) ||std::wstring_view(Arguments[1]) != L"--root" || std::wstring_view(Arguments[2]).empty() ||
		(bHasOutput && (std::wstring_view(Arguments[3]) != L"--output" || std::wstring_view(Arguments[4]).empty())))
	{
		throw std::invalid_argument("Usage: UHT.exe --root <ProjectRoot> [--output <Directory>]");
	}

	// 존재하는 프로젝트 루트의 절대 경로 확정
	ProjectRoot = std::filesystem::canonical(Arguments[2]);

	// 프로젝트 루트를 기준으로 해석하는 선택적 출력 경로
	const std::filesystem::path RequestedOutput = bHasOutput ? std::filesystem::path(Arguments[4])
		: std::filesystem::path("Build/Generated/UHT");

	OutputDirectory =(RequestedOutput.is_absolute() ? RequestedOutput : ProjectRoot / RequestedOutput).lexically_normal();
}

// 두 소스 영역의 원본 헤더 수집과 순서 고정
void FHeaderTool::DiscoverHeaders()
{
	HeaderFiles.clear();
	for (const wchar_t* SourceDirectory : { L"Source/Runtime", L"Source/Editor" })
	{
		const std::filesystem::path SearchRoot = ProjectRoot / SourceDirectory;
		for (const std::filesystem::directory_entry& Entry : std::filesystem::recursive_directory_iterator(SearchRoot))
		{
			if (!Entry.is_regular_file())
			{
				continue;
			}

			const std::filesystem::path& HeaderPath = Entry.path();
			if (HeaderPath.extension() != L".h" || HeaderPath.filename().wstring().ends_with(L".generated.h"))
			{
				continue;
			}

			// 후속 분석과 출력 식별에 사용할 루트 기준 경로
			HeaderFiles.push_back(HeaderPath.lexically_relative(ProjectRoot));
		}
	}

	// 파일 시스템의 열거 순서와 독립적인 처리 순서
	std::sort(HeaderFiles.begin(), HeaderFiles.end());
}

// 바이트가 달라진 생성 결과만 기록하는 저장
void FHeaderTool::SaveGeneratedFile(const std::filesystem::path& Path, std::string_view Text) const
{
	if (std::filesystem::exists(Path) && std::filesystem::file_size(Path) == Text.size())
	{
		std::ifstream Reader(Path, std::ios::binary);
		std::string Existing(Text.size(), '\0');

		if (!Reader.read(Existing.data(), static_cast<std::streamsize>(Existing.size())))
		{
			const std::u8string Name = Path.generic_u8string();
			throw std::runtime_error("Cannot read generated file: " + std::string(Name.begin(), Name.end()));
		}

		if (Existing == Text)
		{
			return;
		}
	}

	std::filesystem::create_directories(Path.parent_path());

	std::ofstream Writer(Path, std::ios::binary | std::ios::trunc);
	Writer.write(Text.data(), static_cast<std::streamsize>(Text.size()));
	Writer.close();

	if (!Writer)
	{
		const std::u8string Name = Path.generic_u8string();
		throw std::runtime_error("Cannot write generated file: " + std::string(Name.begin(), Name.end()));
	}
}

// 생성 표식과 실제 파일 동일성에 따른 오래된 출력 정리
void FHeaderTool::RemoveStaleGeneratedFiles(const std::vector<std::filesystem::path>& ExpectedFiles) const
{
	std::vector<std::filesystem::path> StaleFiles;

	for (const std::string_view Module : { "Runtime", "Editor" })
	{
		const std::filesystem::path Directory = OutputDirectory / Module;
		if (!std::filesystem::is_directory(std::filesystem::symlink_status(Directory)))
		{
			continue;
		}

		for (const std::filesystem::directory_entry& Entry : std::filesystem::directory_iterator(Directory))
		{
			if (!std::filesystem::is_regular_file(Entry.symlink_status()))
			{
				continue;
			}

			const std::filesystem::path& Path = Entry.path();
			const std::u8string Name = Path.filename().generic_u8string();
			const bool bIsHeader = Name.ends_with(u8".generated.h");
			const bool bIsSource = Name.ends_with(u8".gen.cpp");
			if (!bIsHeader && !bIsSource)
			{
				continue;
			}

			// 대소문자 표기 변경에도 유지되는 실제 파일 비교
			const bool bExpected = std::any_of(
			    ExpectedFiles.begin(), ExpectedFiles.end(),
			    [&Path](const std::filesystem::path& ExpectedPath)
			    {
				    return std::filesystem::equivalent(Path, ExpectedPath);
			    });

			if (bExpected)
			{
				continue;
			}

			std::ifstream Reader(Path, std::ios::binary);
			std::string FirstLine;
			std::getline(Reader, FirstLine);
			if (!Reader.is_open() || Reader.bad())
			{
				const std::u8string FullName = Path.generic_u8string();
				throw std::runtime_error("Cannot inspect generated file: " +
					std::string(FullName.begin(), FullName.end()));
			}

			if (!FirstLine.empty() && FirstLine.back() == '\r')
			{
				FirstLine.pop_back();
			}

			const std::string_view Marker = bIsHeader
			                                    ? FHeaderGenerator::GeneratedHeaderMarker
			                                    : FHeaderGenerator::GeneratedSourceMarker;

			if (FirstLine == Marker)
			{
				StaleFiles.push_back(Path);
			}
		}
	}

	// 디렉터리를 보존하는 생성 파일별 삭제
	for (const std::filesystem::path& Path : StaleFiles)
	{
		std::filesystem::remove(Path);
	}
}