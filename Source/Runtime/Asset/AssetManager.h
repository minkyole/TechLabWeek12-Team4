#pragma once

#include "UObject/Object.h"
#include "UObject/Property.h"
#include "Render/Renderer.h"
#include "Asset/LOD/StaticMeshLODGenerator.h"

#include <filesystem>
#include <functional>

class UStaticMesh;
class USkeletalMesh;
class FShader;
class UAnimSequence;
class UBlendSpace1D;
class UAnimStateMachineAsset;
class UAnimGraphAsset;
struct FAnimGraphAssetData;
struct FAnimStateMachineAssetData;

struct FFbxStaticAsset
{
	FString NodeName;
	FMatrix NodeTransform = FMatrix::Identity;
	UStaticMesh* Mesh = nullptr;
};

struct FFbxAssetSet
{
	USkeletalMesh* SkeletalMesh = nullptr;
	TArray<FFbxStaticAsset> StaticMeshes;
	TArray<class UAnimSequence*> Animations;
};

namespace fs = std::filesystem;

class UAssetManager : public UObject
{
	DECLARE_CLASS(UAssetManager, UObject)
private:
	UAssetManager() = default;

	UAssetManager(const UAssetManager& src) =  delete;
	UAssetManager& operator= (const UAssetManager& src) = delete;

	using FAssetLoadProgress = std::function<void(int32 Loaded, int32 Total, const FString& Path)>;
public:
	static UAssetManager& Get();
	static void Init(const FAssetLoadProgress& OnProgress = nullptr);
	static void Shutdown();

	void ScanAssets(const fs::path& AssetRoot, const FAssetLoadProgress& OnProgress);
	void LoadAsset(const FString& Key, const FString& Path);

	void CreateDefaultTextures();
	void CreateDefaultMeshes();
	void CreateDefaultMaterial();
	void CreateDefaultParticleMaterial();
	void CreateParticleMaterial();
	void CreateEditorArrowMaterial();

	template <typename T>
	static T* GetAssetByPath(const FString& Path)
	{
		URenderAsset** Found = Get().AssetMap.Find(Path);

		if (Found == nullptr || *Found == nullptr || !(*Found)->IsA<T>())
		{
			return nullptr;
		}
		return Cast<T>(*Found);
	}

	void RegisterAsset(const FString& Key, URenderAsset* Asset);

	// 현재 등록 객체가 일치하는 비소유 참조의 해제
	void UnregisterAsset(const FString& Key, const URenderAsset* ExpectedAsset);

	// Physical file paths under Assets; stored references use AssetMap keys.
	UBlendSpace1D* CreateBlendSpace1D(const FString& Path, const USkeletalMesh* Mesh, FString& Error);
	UBlendSpace1D* LoadBlendSpace(const FString& Path, FString& Error);
	bool SaveBlendSpace(const FString& Path, const UBlendSpace1D& Definition, FString& Error, bool bCreateOnly = false);
	TArray<UAnimSequence*> GetAnimationAssets(const USkeletalMesh* Mesh = nullptr) const;
	TArray<USkeletalMesh*> GetSkeletalMeshAssets() const;
	TArray<UBlendSpace1D*> GetBlendSpaceAssets(const USkeletalMesh* Mesh = nullptr) const;

	UAnimGraphAsset* LoadAnimGraph(const FString& Path, FString& Error);
    bool SaveAnimGraph(const FString& Path, const FAnimGraphAssetData& Data, FString& Error, bool CreateOnly = false);
	UAnimStateMachineAsset* LoadAnimStateMachine(const FString& Path, FString& Error);
	bool SaveAnimStateMachine(const FString& Path, const FAnimStateMachineAssetData& Data, FString& Error, bool CreateOnly = false);
	//UStaticMesh* GetMesh(FString InName);
	//UStaticMesh* GetMesh(EPrimitiveType Type); // 오버로드(수정 중)

	UTexture2D* LoadTexture(const FString& InPath, bool bGenerateMips = true);
	UFont* LoadFontAtlas(const FString& JsonPath, const FString& AtlasTexturePath);
	static UStaticMesh* LoadObjStaticMesh(const FString& Path);
	static bool IsFbxBakePath(const fs::path& Path);
	const FFbxAssetSet* LoadFbxAssets(const FString& Path);
	const FFbxAssetSet* FindFbxAssets(const FString& Key) const { return FbxAssets.Find(Key); }

	FLODGenerateResult GenerateStaticMeshLODs(UStaticMesh& Mesh,const FLODGenerateRequest& Request);

	// USkeletalMesh의 현재 MeshData(수정된 RefPose 및 InverseBindPoseMat)를 디스크의 .bin 캐시 파일에 저장하고 등록된 에셋 및 씬 컴포넌트를 갱신한다.
	bool SaveSkeletalMesh(USkeletalMesh* Mesh);

	// 변형된 .bin을 원본 FBX 초기 포즈로 즉시 복원하고 .bin 캐시도 덮어쓴다.
	bool RestoreSkeletalMeshToOriginalFBX(USkeletalMesh* Mesh);

	// 새로운 이름으로 .fbx.bin 파일을 저장하고 새 에셋으로 등록한다.
	USkeletalMesh* SaveSkeletalMeshAsNewBin(USkeletalMesh* Mesh, const FString& NewAssetName, const FSkeletalMeshData& NewData);

	// 원본 좌표축이 엔진과 다른 메시를 로드 후 돌린다 (정점·법선·AABB·삼각형 BVH·GPU 정점 버퍼를 다시 만든다).
	// LOD는 LOD0에서 만들어지므로 LOD를 만들기 전에만 부를 수 있다. Rotate는 회전(길이·손잡이 보존)이어야 한다.
	static bool ReorientStaticMesh(UStaticMesh& Mesh, const std::function<FVector(const FVector&)>& Rotate);

	// 로드된 OBJ 스태틱 메시(키가 .obj로 끝나는 것)마다 부른다. 기본 도형·기즈모 메시는 제외된다.
	static void ForEachObjStaticMesh(const std::function<void(const FString& Key, UStaticMesh& Mesh)>& Func);

private:
	TMap<FString, FString> AssetPathMap;
	TMap<FString, URenderAsset*> AssetMap;
	TMap<FString, FFbxAssetSet> FbxAssets;

	//TMap<FString, UStaticMesh*> MeshMap;	// KEY: FILE NAME OR 쉐입 첫글자 대문자
	TMap<FString, TUniquePtr<FShader>> ShaderMap;	// KEY: FILE NAME

	//TMap<FString, UTexture2D*> TextureMap;
};
