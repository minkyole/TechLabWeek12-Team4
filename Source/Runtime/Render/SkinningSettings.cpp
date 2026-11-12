#include "EnginePCH.h"
#include "SkinningSettings.h"
#include <cctype>

namespace SkinningConsole
{
	// 콘솔 입력은 대소문자를 구분하지 않는다.
	bool EqualsIgnoreCase(const FString& A, const char* B)
	{
		uint64 Index = 0;
		for (; Index < A.size() && B[Index] != '\0'; ++Index)
		{
			if (std::tolower(static_cast<unsigned char>(A[Index])) != std::tolower(static_cast<unsigned char>(B[Index])))
				return false;
		}
		return Index == A.size() && B[Index] == '\0';
	}

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
}

void FSkinningSettings::SetGPU(bool bInGPU)
{
	if (bGPUSkinning == bInGPU)
		return;
	bGPUSkinning = bInGPU;
	++ModeVersion;
}

// skinning [cpu|gpu] : 스키닝 위치를 바꾼다. 인자가 없으면 토글한다.
// skinning force [on|off] : 측정용으로 매 프레임 다시 스키닝한다. 인자가 없으면 토글한다.
bool FSkinningSettings::ExecCommand(const FString& CommandLine, FString& OutMessage)
{
	TArray<FString> Tokens;
	SkinningConsole::Tokenize(CommandLine, Tokens);
	if (Tokens.Num() == 0 || !SkinningConsole::EqualsIgnoreCase(Tokens[0], "skinning"))
		return false;

	if (Tokens.Num() >= 2 && SkinningConsole::EqualsIgnoreCase(Tokens[1], "force"))
	{
		bool bNewForce = !bForceEveryFrame;
		if (Tokens.Num() == 3 && SkinningConsole::EqualsIgnoreCase(Tokens[2], "on"))       bNewForce = true;
		else if (Tokens.Num() == 3 && SkinningConsole::EqualsIgnoreCase(Tokens[2], "off")) bNewForce = false;
		else if (Tokens.Num() != 2)
		{
			OutMessage = "usage: skinning force [on|off] (no argument: toggle)";
			return true;
		}
		bForceEveryFrame = bNewForce;
		OutMessage = bForceEveryFrame ? "skinning force: ON (re-skin every frame)" : "skinning force: OFF";
		return true;
	}

	bool bNewGPU = !bGPUSkinning;
	if (Tokens.Num() == 2)
	{
		if (SkinningConsole::EqualsIgnoreCase(Tokens[1], "gpu"))      bNewGPU = true;
		else if (SkinningConsole::EqualsIgnoreCase(Tokens[1], "cpu")) bNewGPU = false;
		else
		{
			OutMessage = "usage: skinning [cpu|gpu] (no argument: toggle)";
			return true;
		}
	}
	else if (Tokens.Num() > 2)
	{
		OutMessage = "usage: skinning [cpu|gpu] (no argument: toggle)";
		return true;
	}

	SetGPU(bNewGPU);
	OutMessage = bGPUSkinning ? "skinning: GPU" : "skinning: CPU";
	return true;
}
