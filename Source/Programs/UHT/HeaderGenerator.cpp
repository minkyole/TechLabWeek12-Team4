#include "HeaderGenerator.h"

#include <Windows.h>
#include <cctype>
#include <cstdint>
#include <set>
#include <sstream>
#include <stdexcept>

// 인코딩과 경로 구분자를 고정한 생성용 텍스트
std::string FHeaderGenerator::PathText(const std::filesystem::path& Path) const
{
	const std::u8string Text = Path.generic_u8string();
	return std::string(Text.begin(), Text.end());
}

// 프로젝트 상대 경로를 치환한 생성 본문 식별자
std::string FHeaderGenerator::FileId(const std::filesystem::path& RelativePath) const
{
	const std::wstring Path = RelativePath.generic_wstring();
	std::string Id = "FID_";
	Id.reserve(Path.size() + 4);

	for (const wchar_t Character : Path)
	{
		// 로케일과 무관한 ASCII 영문·숫자 판별
		const bool bIsAlnum =
		    (Character >= L'A' && Character <= L'Z') ||
		    (Character >= L'a' && Character <= L'z') ||
		    (Character >= L'0' && Character <= L'9');

		Id += bIsAlnum ? static_cast<char>(Character) : '_';
	}
	return Id;
}

// 현재 프로젝트의 두 빌드 경계 확인
std::string FHeaderGenerator::ModuleName(const std::filesystem::path& RelativePath) const
{
	const std::string Path = PathText(RelativePath);
	if (Path.starts_with("Source/Runtime/"))
	{
		return "Runtime";
	}
	if (Path.starts_with("Source/Editor/"))
	{
		return "Editor";
	}
	throw std::runtime_error("Unsupported reflected header path: " + Path);
}

// 기존 Runtime 및 Editor include 경로의 재사용
std::string FHeaderGenerator::IncludePath(const std::filesystem::path& RelativePath) const
{
	const std::string Path = PathText(RelativePath);
	return ModuleName(RelativePath) == "Runtime"
	           ? Path.substr(std::string_view("Source/Runtime/").size())
	           : Path.substr(std::string_view("Source/").size());
}

// 동일 출력과 동일 런타임 이름의 사전 차단
void FHeaderGenerator::Validate(const std::vector<FHeaderFile>& Headers) const
{
	std::set<std::string> FileIds;
	std::set<std::string> ClassNames;
	for (std::size_t Index = 0; Index < Headers.size(); ++Index)
	{
		const FHeaderFile& Header = Headers[Index];
		ModuleName(Header.RelativePath);
		const std::string Path = PathText(Header.RelativePath);
		if (Path.find_first_of("\"\r\n") != std::string::npos || !FileIds.insert(FileId(Header.RelativePath)).second)
		{
			throw std::runtime_error("Invalid include path or duplicate file ID: " + Path);
		}
		const std::wstring Name = Header.RelativePath.filename().native();
		for (std::size_t Previous = 0; Previous < Index; ++Previous)
		{
			const std::wstring Other = Headers[Previous].RelativePath.filename().native();
			if (CompareStringOrdinal(Name.c_str(), -1, Other.c_str(), -1, TRUE) == CSTR_EQUAL)
			{
				throw std::runtime_error("Generated header basename collision: " + Path);
			}
		}
		for (const FHeaderClass& Class : Header.Classes)
		{
			std::string NameKey = Class.Name;
			for (char& Character : NameKey)
			{
				Character = static_cast<char>(std::tolower(static_cast<unsigned char>(Character)));
			}
			if (!ClassNames.insert(NameKey).second)
			{
				throw std::runtime_error("Duplicate reflected class name: " + Class.Name);
			}
		}
	}
}

// Parser가 허용한 지정자를 기존 클래스 입력값으로 연결
std::string FHeaderGenerator::ClassSpecifiers(const FHeaderClass& Class) const
{
	std::string Result;
	for (const std::string& Specifier : Class.Specifiers)
	{
		if (!Result.empty())
		{
			Result += " | ";
		}
		Result += "EClassSpecifiers::" + Specifier;
	}
	return Result.empty() ? "EClassSpecifiers::None" : Result;
}

// 클래스별 행 번호에 대응하는 생성 본문 정의
std::string FHeaderGenerator::GenerateHeader(const FHeaderFile& Header) const
{
	const std::string Id = FileId(Header.RelativePath);
	std::ostringstream Output;
	Output << GeneratedHeaderMarker << "\n#pragma once\n\n";
	Output << "#include \"UObject/Class.h\"\n#include \"UObject/ReflectionMacros.h\"\n\n";
	for (const FHeaderClass& Class : Header.Classes)
	{
		Output << "#define " << Id << '_' << Class.GeneratedBodyLine << "_GENERATED_BODY \\\n";
		Output << "    DECLARE_CLASS_BODY(" << Class.Name << ", " << Class.SuperName << ", " << ClassSpecifiers(Class) << ") \\\n";
		Output << "public: \\\n    static void RegisterProperties(UClass* InClass); \\\nprivate:\n\n";
	}
	Output << "#undef CURRENT_FILE_ID\n#define CURRENT_FILE_ID " << Id << '\n';
	return Output.str();
}

// 모듈 의존 방향을 유지하는 등록 코드와 앵커 정의
std::string FHeaderGenerator::GenerateModule(const std::vector<FHeaderFile>& Headers, std::string_view Module) const
{
	std::ostringstream Output;
	Output << GeneratedSourceMarker << "\n#include \"EnginePCH.h\"\n#include \"UObject/Class.h\"\n";
	for (const FHeaderFile& Header : Headers)
	{
		if (Module == "Editor" || ModuleName(Header.RelativePath) == Module)
		{
			Output << "#include \"" << IncludePath(Header.RelativePath) << "\"\n";
		}
	}
	Output << '\n';
	for (const FHeaderFile& Header : Headers)
	{
		if (ModuleName(Header.RelativePath) != Module)
		{
			continue;
		}
		for (const FHeaderClass& Class : Header.Classes)
		{
			Output << "// " << Class.Name << "의 직접 프로퍼티 등록\n";
			Output << "void " << Class.Name << "::RegisterProperties(UClass* InClass)\n{\n";
			if (Class.Properties.empty())
			{
				Output << "    (void)InClass;\n";
			}
			for (const FHeaderProperty& Property : Class.Properties)
			{
				// 전체 프로퍼티 지정자를 결합하는 플래그 표현식
				std::string Flags;
				for (const std::string& Specifier : Property.Specifiers)
				{
					if (!Flags.empty())
					{
						Flags += " | ";
					}
					Flags += "CPF_" + Specifier;
				}

				if (Flags.empty())
				{
					Flags = "CPF_None";
				}

				Output << "    InClass->AddProperty<decltype(ThisClass::" << Property.Name << ")>(\"" << Property.Name
				       << "\", offsetof(ThisClass, " << Property.Name << "), " << Flags << ");\n";
			}
			Output << "}\n\n";
		}
	}
	Output << "// 헤더 전용 클래스까지 포함하는 명시적 자동 등록\nnamespace UHTGenerated\n{\n";
	for (const FHeaderFile& Header : Headers)
	{
		if (ModuleName(Header.RelativePath) == Module)
		{
			for (const FHeaderClass& Class : Header.Classes)
			{
				Output << "    " << Class.Name << "::FAutoRegister Z_" << FileId(Header.RelativePath) << '_' << Class.Name << ";\n";
			}
		}
	}
	Output << "}\n";
	return Output.str();
}