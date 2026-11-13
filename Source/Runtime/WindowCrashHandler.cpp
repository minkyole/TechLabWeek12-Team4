#include "EnginePCH.h"
#include "WindowCrashHandler.h"
#include <DbgHelp.h>

#pragma comment(lib, "Dbghelp.lib")

static wchar_t GCrashDir[MAX_PATH] = {};

namespace
{
	// 콘솔 입력은 대소문자를 구분하지 않는다.
	bool EqualsIgnoreCase(const FString& A, const char* B)
	{
		uint64 Index = 0;
		for (; Index < A.size() && B[Index] != '\0'; ++Index)
		{
			const int Left = std::tolower(static_cast<unsigned char>(A[Index]));
			const int Right = std::tolower(static_cast<unsigned char>(B[Index]));
			if (Left != Right)
				return false;
		}
		return Index == A.size() && B[Index] == '\0';
	}

	// 공백 구분 토큰으로 나눈다. 연속 공백과 앞뒤 공백은 무시한다.
	void Tokenize(const FString& Line, TArray<FString>& OutTokens)
	{
		OutTokens.Reset();
		uint64 Cursor = 0;
		while (Cursor < Line.size())
		{
			while (Cursor < Line.size() && std::isspace(static_cast<unsigned char>(Line[Cursor])))
				++Cursor;
			const uint64 Start = Cursor;
			while (Cursor < Line.size() && !std::isspace(static_cast<unsigned char>(Line[Cursor])))
				++Cursor;
			if (Cursor > Start)
				OutTokens.Add(Line.substr(Start, Cursor - Start));
		}
	}

	// ---- 크래시 유발 함수들 ----
	// noinline: 콜스택에 함수 이름이 따로 보이게 해서 덤프에서 확인하기 쉽게 한다.
	__declspec(noinline) void CrashNullWrite()
	{
		volatile int* P = nullptr;
		*P = 0; // 0xC0000005
	}

	__declspec(noinline) void CrashDivideByZero()
	{
		volatile int Zero = 0;
		volatile int Result = 1 / Zero; // 0xC0000094
		(void)Result;
	}

	#pragma warning(push)
	#pragma warning(disable : 4717) // "모든 경로가 재귀" 경고 끄기
	__declspec(noinline) void CrashStackOverflow(volatile int Depth)
	{
		volatile char Buffer[1024]; // 스택을 빨리 소모
		Buffer[0] = static_cast<char>(Depth);
		CrashStackOverflow(Depth + 1); // 0xC00000FD
	}
	#pragma warning(pop)
}

void FWindowCrashHandler::Install()
{
	namespace fs = std::filesystem;

	//덤프 저장 폴더 준비
	std::error_code EC;
	fs::create_directories("Saved/Crashes", EC); 

	const fs::path Abs = fs::absolute("Saved/Crashes", EC); // 2. 절대 경로 고정
	wcsncpy_s(GCrashDir, Abs.c_str(), _TRUNCATE);

	// OS에 크래시 콜백을 등록한다.
	SetUnhandledExceptionFilter(&OnUnhandledException);

}

LONG __stdcall FWindowCrashHandler::OnUnhandledException(EXCEPTION_POINTERS* ExceptionInfo)
{
	//1.중복 진입 방지,핸들러 안에서 또 크래시가 나면 핸들러가 다시 호출되어 무한 반복 될 수 있다.
	//static 플래그로 두번째 호출이면 바로 빠져나간다.
	static volatile LONG bEntered = 0;
	if (InterlockedExchange(&bEntered, 1) != 0)
		return EXCEPTION_EXECUTE_HANDLER;

	//2.파일 이름 만들기
	//시간 기록용 변수
	SYSTEMTIME T;
	GetLocalTime(&T);
	wchar_t Path[MAX_PATH];
	swprintf_s(Path, L"%s\\Crash_%04d%02d%02d_%02d%02d%02d.dmp",
	           GCrashDir, T.wYear, T.wMonth, T.wDay, T.wHour, T.wMinute, T.wSecond);

	//3.파일 열기
	BOOL bDumpWritten = FALSE; 
	HANDLE File = CreateFileW(Path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
	if (File != INVALID_HANDLE_VALUE) 
	{
		//4. 덤프 쓰기
		MINIDUMP_EXCEPTION_INFORMATION Info{};
		Info.ThreadId = GetCurrentThreadId();
		Info.ExceptionPointers = ExceptionInfo;
		Info.ClientPointers = FALSE;

		MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), File,
		                  MINIDUMP_TYPE(MiniDumpWithIndirectlyReferencedMemory |
		                                MiniDumpWithThreadInfo),
		                  &Info, nullptr, nullptr);
		CloseHandle(File);

	}

	//5.사용자에게 알림, 이게없으면 프로그램이 조용히 실행이안되서 왜 실행이안되는지를 모른다
	wchar_t Msg[MAX_PATH + 256];
	swprintf_s(Msg,
	           L"크래시가 발생했습니다.\n\n예외 코드: 0x%08X\n주소: %p\n\n덤프 파일:\n%s",
	           ExceptionInfo->ExceptionRecord->ExceptionCode,
	           ExceptionInfo->ExceptionRecord->ExceptionAddress,
	           bDumpWritten ? Path : L"(덤프 저장 실패)");
	MessageBoxW(nullptr, Msg, L"Hitori Crash",
	            MB_OK | MB_ICONERROR | MB_TOPMOST | MB_SETFOREGROUND);

	
	return EXCEPTION_EXECUTE_HANDLER;
}

bool FWindowCrashHandler::ExecCommand(const FString& CommandLine, FString& OutMessage)
{
	//토큰 나누기
	TArray<FString> Tokens;
	Tokenize(CommandLine, Tokens);

	//내 명령인가?
	if (Tokens.Num() == 0 || !EqualsIgnoreCase(Tokens[0], "causecrash"))
		return false;

	//종류 고르기 (인자 없으면 null)
	if (Tokens.Num() == 1 || EqualsIgnoreCase(Tokens[1], "null"))
		CrashNullWrite();
	else if (EqualsIgnoreCase(Tokens[1], "div"))
		CrashDivideByZero();
	else if (EqualsIgnoreCase(Tokens[1], "stack"))
		CrashStackOverflow(0);

	//여기까지 왔다면 모르는 인자다
	OutMessage = "usage: CauseCrash [null|div|stack]";
	return true;
}