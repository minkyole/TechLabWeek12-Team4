#include "HeaderTool.h"

#include <exception>
#include <iostream>

// Windows 유니코드 경로를 받는 독립 실행 진입점
int wmain(int ArgumentCount, wchar_t* Arguments[])
{
	try
	{
		FHeaderTool HeaderTool;
		return HeaderTool.Run(ArgumentCount, Arguments);
	}
	catch (const std::exception& Exception)
	{
		// 명령행 오류와 파일 시스템 오류의 공통 종료 지점
		std::cerr << "UHT error: " << Exception.what() << '\n';
		return 1;
	}
}