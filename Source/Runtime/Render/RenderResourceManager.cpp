#include "EnginePCH.h"
#include "RenderResourceManager.h"

#include "RenderCommand.h"

#include "RenderUtil.h"

namespace
{
	FString MakeShaderKey(const FString& Path, const D3D_SHADER_MACRO* Macros)
	{
		FString Key = Path;
		while (Macros && Macros->Name)
		{
			Key += "_" + FString(Macros->Name);
			++Macros;
		}
		return Key;
	}
}


void FRenderResourceManager::ScanShaders(const fs::path& ShaderRoot)
{
	std::error_code ErrorCode;

	if (!fs::exists(ShaderRoot, ErrorCode))
	{
		HTR_LOG(Error, "[Shader] Scan Root Not Found : {}", ShaderRoot.generic_string());
		return;
	}

	for (const fs::directory_entry& Entry : fs::recursive_directory_iterator(ShaderRoot))
	{
		if (!Entry.is_regular_file()) continue;
		if (Entry.path().extension() != ".hlsl") continue;
		if (Entry.path().stem() == "UberLit") continue;

		FString Path = Entry.path().generic_string();
		LoadOrCompileShader(Path);
	}
	LoadUberLit(ShaderRoot);
	// 선택 아웃라인의 GPU 스키닝 변형. GPU 모드 스켈레탈 메시를 현재 자세로 감싼다.
	LoadOrCompileShader((ShaderRoot / "OutlineShader.hlsl").generic_string(), GetSkinningMacros());
}

const D3D_SHADER_MACRO* FRenderResourceManager::GetSkinningMacros()
{
	static const D3D_SHADER_MACRO Macros[] = { { "GPU_SKINNING", "1" },{ nullptr, nullptr } };
	return Macros;
}

void FRenderResourceManager::LoadUberLit(const fs::path& ShaderRoot)
{
	const FString Path = (ShaderRoot / "UberLit.hlsl").generic_string();

	// 같은 UberLit.hlsl을 매크로 조합별로 따로 컴파일한다.
	// 캐시 키 / CSO 파일명은 "경로_매크로이름" 형태로 구분된다.
	// 매크로 없는 기본 키("Resources/Shader/UberLit.hlsl")는 기존 머티리얼이 쓰므로 Lambert로 남겨 둔다. (셰이더 쪽 기본값)

	static const D3D_SHADER_MACRO Unlit[] = { { "LIGHTING_MODEL_UNLIT", "1" },{ nullptr, nullptr } };
	static const D3D_SHADER_MACRO Gouraud[] = { { "LIGHTING_MODEL_GOURAUD", "1" },{ nullptr, nullptr } };
	static const D3D_SHADER_MACRO Lambert[] = { { "LIGHTING_MODEL_LAMBERT", "1" },{ nullptr, nullptr } };
	static const D3D_SHADER_MACRO BlinnPhong[] = { { "LIGHTING_MODEL_BLINN_PHONG", "1" },{ nullptr, nullptr } };
	static const D3D_SHADER_MACRO WorldNormal[] = { { "WORLD_NORMAL", "1" },{ nullptr, nullptr } };

	// GPU 스키닝 변형. 정점 셰이더가 BLENDINDICES / BLENDWEIGHT와 본 행렬(t30, t31)로 스키닝한다.
	// 기본 키의 스킨 변형은 만들지 않는다. BindMaterial이 UberLit을 항상 뷰모드 변형으로 바꿔 끼우기 때문이다.
	static const D3D_SHADER_MACRO UnlitSkin[] = { { "LIGHTING_MODEL_UNLIT", "1" },{ "GPU_SKINNING", "1" },{ nullptr, nullptr } };
	static const D3D_SHADER_MACRO GouraudSkin[] = { { "LIGHTING_MODEL_GOURAUD", "1" },{ "GPU_SKINNING", "1" },{ nullptr, nullptr } };
	static const D3D_SHADER_MACRO LambertSkin[] = { { "LIGHTING_MODEL_LAMBERT", "1" },{ "GPU_SKINNING", "1" },{ nullptr, nullptr } };
	static const D3D_SHADER_MACRO BlinnPhongSkin[] = { { "LIGHTING_MODEL_BLINN_PHONG", "1" },{ "GPU_SKINNING", "1" },{ nullptr, nullptr } };
	static const D3D_SHADER_MACRO WorldNormalSkin[] = { { "WORLD_NORMAL", "1" },{ "GPU_SKINNING", "1" },{ nullptr, nullptr } };

	LoadOrCompileShader(Path);	// 기본 (Lambert)
	LoadOrCompileShader(Path, Unlit);
	LoadOrCompileShader(Path, Gouraud);
	LoadOrCompileShader(Path, Lambert);
	LoadOrCompileShader(Path, BlinnPhong);
	LoadOrCompileShader(Path, WorldNormal);
	LoadOrCompileShader(Path, UnlitSkin);
	LoadOrCompileShader(Path, GouraudSkin);
	LoadOrCompileShader(Path, LambertSkin);
	LoadOrCompileShader(Path, BlinnPhongSkin);
	LoadOrCompileShader(Path, WorldNormalSkin);

	// 뷰모드 -> 변형 테이블. 컴파일에 실패한 키는 nullptr로 남는다(GetShaderProgram의 폴백을 쓰지 않으려고 맵을 직접 찾는다).
	auto FindProgram = [this](const FString& Key) -> FShaderProgram*
		{
			TUniquePtr<FShaderProgram>* Found = ShaderProgramMap.Find(Key);
			return Found ? Found->get() : nullptr;
		};

	UberLitDefault = FindProgram(MakeShaderKey(Path, nullptr));
	FShaderProgram** Base = UberLitPrograms[0];
	Base[static_cast<int32>(EViewMode::Lit_Gouraud)] = FindProgram(MakeShaderKey(Path, Gouraud));
	Base[static_cast<int32>(EViewMode::Lit_Lambert)] = FindProgram(MakeShaderKey(Path, Lambert));
	Base[static_cast<int32>(EViewMode::Lit_BlinnPhong)] = FindProgram(MakeShaderKey(Path, BlinnPhong));
	Base[static_cast<int32>(EViewMode::Unlit)] = FindProgram(MakeShaderKey(Path, Unlit));
	Base[static_cast<int32>(EViewMode::WorldNormal)] = FindProgram(MakeShaderKey(Path, WorldNormal));

	FShaderProgram** Skinned = UberLitPrograms[1];
	Skinned[static_cast<int32>(EViewMode::Lit_Gouraud)] = FindProgram(MakeShaderKey(Path, GouraudSkin));
	Skinned[static_cast<int32>(EViewMode::Lit_Lambert)] = FindProgram(MakeShaderKey(Path, LambertSkin));
	Skinned[static_cast<int32>(EViewMode::Lit_BlinnPhong)] = FindProgram(MakeShaderKey(Path, BlinnPhongSkin));
	Skinned[static_cast<int32>(EViewMode::Unlit)] = FindProgram(MakeShaderKey(Path, UnlitSkin));
	Skinned[static_cast<int32>(EViewMode::WorldNormal)] = FindProgram(MakeShaderKey(Path, WorldNormalSkin));
}

FShaderProgram* FRenderResourceManager::GetUberLitProgram(EViewMode Mode, bool bSkinned)
{
	const int32 Index = static_cast<int32>(Mode);
	if (Index < 0 || Index >= static_cast<int32>(EViewMode::Count))
		return nullptr;
	return Get().UberLitPrograms[bSkinned ? 1 : 0][Index];
}

bool FRenderResourceManager::IsUberLitProgram(const FShaderProgram* Program)
{
	if (!Program)
		return false;

	const FRenderResourceManager& Self = Get();
	if (Program == Self.UberLitDefault)
		return true;
	for (const auto& Variants : Self.UberLitPrograms)
	{
		for (const FShaderProgram* Variant : Variants)
		{
			if (Program == Variant)
				return true;
		}
	}
	return false;
}

void FRenderResourceManager::Shutdown()
{
	Get().VertexShaderMap.Empty();
	Get().PixelShaderMap.Empty();
	Get().ComputeShaderMap.Empty();
	Get().ShaderProgramMap.Empty();
	Get().UberLitDefault = nullptr;
	for (auto& Variants : Get().UberLitPrograms)
		for (FShaderProgram*& Program : Variants)
			Program = nullptr;
}

FShaderProgram* FRenderResourceManager::GetShaderProgram(const FString& InPath, const D3D_SHADER_MACRO* Macro)
{
	const FString Key = MakeShaderKey(InPath, Macro);

	if (TUniquePtr<FShaderProgram>* Found = Get().ShaderProgramMap.Find(Key))
		return Found->get();

	HTR_LOG(Error, "[Shader] not found: {}", Key);

	if (TUniquePtr<FShaderProgram>* Fallback =
		Get().ShaderProgramMap.Find("Resources/Shader/DefaultShader.hlsl"))
		return Fallback->get();

	return nullptr;
}

FComputeShader* FRenderResourceManager::GetComputeShader(const FString& InPath)
{
	if (TUniquePtr<FComputeShader>* Found = Get().ComputeShaderMap.Find(InPath))
		return Found->get();
	return nullptr;
}
void FRenderResourceManager::LoadOrCompileShader(const FString& Path, const D3D_SHADER_MACRO* Macro)
{
	std::ifstream In(Path);
	const std::string Source((std::istreambuf_iterator<char>(In)), std::istreambuf_iterator<char>());

	const bool bHasVS = Source.find("mainVS") != std::string::npos;
	const bool bHasPS = Source.find("mainPS") != std::string::npos;
	const bool bHasCS = Source.find("mainCS") != std::string::npos;
	const bool HasGraphicsInclude{ !bHasCS && Source.find("#include") != std::string::npos };

	if (bHasVS || bHasPS || HasGraphicsInclude) {
		FString VSCSOPath;
		FString PSCSOPath;
		FShaderByteCode VSCode = RenderUtil::GetOrCompile(Path, "mainVS", EShaderType::Vertex, VSCSOPath, Macro);
		FShaderByteCode PSCode = RenderUtil::GetOrCompile(Path, "mainPS", EShaderType::Pixel, PSCSOPath, Macro);
		const FString Key = MakeShaderKey(Path, Macro);




		if (!VSCode.IsValid() || !PSCode.IsValid())
		{
			HTR_LOG(Error, "[Shader] compile failed: {}", Key);
			return;
		}

		TUniquePtr<FVertexShader> Vs = RenderCommand::CreateVertexShader(VSCode);
		TUniquePtr<FPixelShader>  Ps = RenderCommand::CreatePixelShader(PSCode);

		if (!Vs || !Vs->IsValid() || !Ps || !Ps->IsValid())
		{
			HTR_LOG(Error, "[Shader] device create failed: {}", Key);
			return;
		}

		FVertexShader* VsRaw = Vs.get();
		FPixelShader* PsRaw = Ps.get();

		VertexShaderMap[VSCSOPath] = std::move(Vs);
		PixelShaderMap[PSCSOPath] = std::move(Ps);

		ShaderProgramMap[Key] = MakeUnique<FShaderProgram>(VsRaw, PsRaw);

		HTR_LOG(Info, "[Shader] Loaded Shader: {}", Path);
	}

	if (bHasCS)
	{
		FString CSOPath;
		FShaderByteCode CSCode = RenderUtil::GetOrCompile(Path, "mainCS", EShaderType::Compute, CSOPath);
		if (!CSCode.IsValid()) { HTR_LOG(Error, "[Shader] compute compile failed: {}", Path); return; }   // 진짜 에러

		ComputeShaderMap[Path] = RenderCommand::CreateComputeShader(CSCode);

		HTR_LOG(Info, "[Shader] Loaded ComputeShader: {}", Path);

	}

}
