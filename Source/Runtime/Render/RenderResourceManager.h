#pragma once

#include "Shader.h"
#include "ViewMode.h"

class FRenderResourceManager
{
public:
	static FRenderResourceManager& Get()
	{
		static FRenderResourceManager* Instance = new FRenderResourceManager();
		return *Instance;
	}

	static void Init()
	{
		Get().ScanShaders("Resources/Shader");
	}

	void ScanShaders(const fs::path& ShaderRoot);
	void LoadUberLit(const fs::path& ShaderRoot);

	static void Shutdown();

	static FComputeShader* GetComputeShader(const FString& InPath);
	static FShaderProgram* GetShaderProgram(const FString& InPath, const D3D_SHADER_MACRO* Macro = nullptr);

	// 뷰모드에 맞는 UberLit 변형. bSkinned면 GPU_SKINNING 변형(정점 셰이더에서 스키닝). 컴파일에 실패했으면 nullptr.
	static FShaderProgram* GetUberLitProgram(EViewMode Mode, bool bSkinned = false);
	// 머티리얼 셰이더가 UberLit(기본 키 또는 변형)인지. 아니면 뷰모드로 바꾸지 않는다.
	static bool IsUberLitProgram(const FShaderProgram* Program);
	// GPU_SKINNING=1 한 개짜리 매크로. UberLit 외 셰이더(아웃라인)의 스킨 변형 키로 쓴다.
	static const D3D_SHADER_MACRO* GetSkinningMacros();
private:
	void LoadOrCompileShader(const FString& Path, const D3D_SHADER_MACRO* Macro = nullptr);

	TMap<FString, TUniquePtr<FVertexShader>> VertexShaderMap;
	TMap<FString, TUniquePtr<FPixelShader>> PixelShaderMap;
	TMap<FString, TUniquePtr<FComputeShader>> ComputeShaderMap;
	TMap<FString, TUniquePtr<FShaderProgram>> ShaderProgramMap;

	// LoadUberLit이 채운다. 맵 안의 프로그램을 가리키기만 하고 소유하지 않는다.
	FShaderProgram* UberLitDefault = nullptr;
	// [0] 일반, [1] GPU_SKINNING 변형
	FShaderProgram* UberLitPrograms[2][static_cast<int32>(EViewMode::Count)]{};
}; 
