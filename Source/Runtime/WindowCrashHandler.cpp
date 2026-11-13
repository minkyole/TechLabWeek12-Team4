#include "EnginePCH.h"
#include "WindowCrashHandler.h"
#include <DbgHelp.h>

#pragma comment(lib, "Dbghelp.lib")

static wchar_t GCrashDir[MAX_PATH] = {};

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
