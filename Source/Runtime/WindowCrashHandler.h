#pragma once



class FWindowCrashHandler
{
public:
	FWindowCrashHandler() = delete;
	~FWindowCrashHandler() = delete;

	FWindowCrashHandler(const FWindowCrashHandler&) = delete;
	FWindowCrashHandler& operator=(const FWindowCrashHandler&) = delete;

	// 초기화시에도 크래시가 날 수 있으니 WinMain 맨 앞에서 실행해줘야함
	static void Install();

	//크래시가 났을 때 OS가 호출해 주는 함수(콜백)다.
	static LONG WINAPI OnUnhandledException(EXCEPTION_POINTERS* ExceptionInfo);

	//의도적으로 크래시를 내는 코드
	static bool ExecCommand(const FString& CommandLine, FString& OutMessage);

private:
};



