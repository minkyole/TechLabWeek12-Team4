#pragma once

#include "EnginePCH.h"
#include "Asset/FBXImporter/FbxImportData.h"

#include <fbxsdk.h>

struct FBoneInfluence;

struct FFbxVertexKey	// OBJ Import와 동일하게 중복 정점을 파악하기 위해서 사용하는 Key
{
	int32 ControlPointIndex = -1;

	float NormalX = 0;
	float NormalY = 0;
	float NormalZ = 0;

	float U = 0;
	float V = 0;

	bool operator==(const FFbxVertexKey&) const = default;
};

struct FFbxVertexKeyHash	// OBJ Import와 동일하게 중복 정점을 파악하기 위해서 사용하는 KeyHash
{
	size_t operator()(const FFbxVertexKey& Index) const
	{
		size_t Hash = std::hash<int32>{}(Index.ControlPointIndex);
		Hash = Hash * 31 + std::hash<float>{}(Index.NormalX);
		Hash = Hash * 31 + std::hash<float>{}(Index.NormalY);
		Hash = Hash * 31 + std::hash<float>{}(Index.NormalZ);
		Hash = Hash * 31 + std::hash<float>{}(Index.U);
		Hash = Hash * 31 + std::hash<float>{}(Index.V);
		return Hash;
	}
};

struct FFbxImportResult	// Fbx의 Import 결과로 나오는 값 구조체
{
	bool bSuccess = false;

	// 본 순회와 메시 순회에서 수집한 결과. Cook에서 최종 데이터로 묶는다.
	TArray<FSkeletalMesh> Meshes;
	TArray<FSkeletalBone> Bones;
	TArray<FFbxStaticMeshPart> StaticMeshes;
	TArray<FFbxAnimClip> Anims;
};

class FFbxImporter
{
public:

	FFbxImporter();
	~FFbxImporter();
	FFbxImportResult ImportFBX(const FString& Path, const FString& SceneName);
	void ProcessSkeletonNode(FbxNode* Node, TMap<FbxNode*, int32>& BoneIndexByNode, FFbxImportResult& OutResult);
	void ProcessMeshNode(FbxNode* Node, const TMap<FbxNode*, int32>& BoneIndexByNode, FFbxImportResult& OutResult, const FString& FbxPath);
	FSkeletalMesh ProcessMesh(FbxMesh* Mesh, const TMap<FbxNode*, int32>& BoneIndexByNode, const FString& FbxPath);
	FSkeletalBone ProcessSkeleton(FbxNode* Node, const TMap<FbxNode*, int32>& BoneIndexByNode);
	void BuildMeshVerticesAndIndices(FbxMesh* Mesh, const TMap<int32, FBoneInfluence>& InfluenceByControlPoint, FSkeletalMesh& SkeletalMesh);
	void BuildInfluenceMap(FbxMesh* Mesh, const TMap<FbxNode*, int32>& BoneIndexByNode, TMap<int32, FBoneInfluence>& InfluenceByControlPoint);
	void BuildMaterials(FbxMesh* Mesh, FSkeletalMesh& SkeletalMesh, const FString& FbxPath);
	void ProcessAnimations(FbxScene* Scene, const TMap<FbxNode*, int32>& BoneIndexByNode, FFbxImportResult& OutResult);
	void SampleBoneTrack(FbxNode* BoneNode, FbxNode* ParentBoneNode, FbxAnimLayer* Layer, const FbxTime& StartTime,
		const FFbxAnimClip& Clip, const FSkeletalBone& Bone, FRawAnimSequenceTrack& OutTrack);
	void Cook(FFbxImportResult&& ImportResult, FFbxCookedData& Cooked);


private:
	FbxManager* pFbxManager = nullptr;
};
