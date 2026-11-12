#include "EnginePCH.h"
#include "Core/Misc/FileHelper.h"

#include <fstream>
#include <limits>
#include <system_error>
#include <utility>

// char8_t 문자열을 통한 UTF-8 입력 명시
std::filesystem::path FFileHelper::ToNativePath(const FString& Path)
{
	return std::filesystem::path(std::u8string(Path.begin(), Path.end()));
}

// 경로 구분자를 슬래시로 통일한 UTF-8 출력
FString FFileHelper::ToUtf8Path(const std::filesystem::path& Path)
{
	const std::u8string Utf8 = Path.generic_u8string();
	return FString(reinterpret_cast<const char*>(Utf8.data()), Utf8.size());
}

// 파일 접근 경계의 입력 검사와 변환 오류 처리
bool FFileHelper::TryGetNativePath(const FString& Filename, std::filesystem::path& Result)
{
	if (Filename.empty() || Filename.find('\0') != FString::npos)
	{
		return false;
	}

	try
	{
		Result = ToNativePath(Filename);
		return true;
	}
	catch (const std::system_error&)
	{
		return false;
	}
}

// 전체 읽기 성공 후 출력 배열 교체
bool FFileHelper::LoadFileToArray(TArray<uint8>& Result, const FString& Filename)
{
	std::filesystem::path NativePath;
	if (!TryGetNativePath(Filename, NativePath))
	{
		return false;
	}

	std::ifstream Reader(NativePath, std::ios::binary | std::ios::ate);
	if (!Reader)
	{
		return false;
	}

	const std::streamoff Size = Reader.tellg();
	if (Size < 0 || Size > std::numeric_limits<int32>::max())
	{
		return false;
	}

	TArray<uint8> Loaded{};
	Loaded.SetNum(static_cast<int32>(Size), false);
	Reader.seekg(0, std::ios::beg);
	if (!Reader)
	{
		return false;
	}

	if (Size > 0)
	{
		Reader.read(reinterpret_cast<char*>(Loaded.GetData()), static_cast<std::streamsize>(Size));
		if (!Reader)
		{
			return false;
		}
	}

	Result = std::move(Loaded);
	return true;
}

// 배열의 실제 크기를 사용하는 저장 요청
bool FFileHelper::SaveArrayToFile(const TArray<uint8>& Array, const FString& Filename)
{
	return SaveDataToFile(Array.GetData(), Array.size(), Filename);
}

// 바이트 읽기 재사용과 선택적 UTF-8 BOM 제거
bool FFileHelper::LoadFileToString(FString& Result, const FString& Filename)
{
	TArray<uint8> Bytes{};
	if (!LoadFileToArray(Bytes, Filename))
	{
		return false;
	}

	int32 Offset = 0;
	if (Bytes.Num() >= 3 && Bytes[0] == 0xEF && Bytes[1] == 0xBB && Bytes[2] == 0xBF)
	{
		Offset = 3;
	}

	FString Loaded;
	if (Bytes.Num() > Offset)
	{
		Loaded.assign(
		    reinterpret_cast<const char*>(Bytes.GetData() + Offset),
		    static_cast<std::size_t>(Bytes.Num() - Offset));
	}

	Result = std::move(Loaded);
	return true;
}

// 문자열의 원본 바이트 직접 저장
bool FFileHelper::SaveStringToFile(const FString& String, const FString& Filename)
{
	return SaveDataToFile(String.data(), String.size(), Filename);
}

// 파일 닫기까지 포함한 저장 결과 확인
bool FFileHelper::SaveDataToFile(const void* Data, std::size_t Size, const FString& Filename)
{
	if (Size > static_cast<std::size_t>(std::numeric_limits<int32>::max()))
	{
		return false;
	}

	std::filesystem::path NativePath;
	if (!TryGetNativePath(Filename, NativePath))
	{
		return false;
	}

	std::ofstream Writer(NativePath, std::ios::binary | std::ios::trunc);
	if (!Writer)
	{
		return false;
	}

	if (Size > 0)
	{
		Writer.write(static_cast<const char*>(Data), static_cast<std::streamsize>(Size));
	}

	Writer.close();
	return !Writer.fail();
}
