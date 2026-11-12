#pragma once

#include "EnginePCH.h"
#include "Render/Vertex.h"
#include "Math/Transform.h"

struct FSkeletalVertex
{
	FVertex Vertex;
	uint32 BoneIndices[4] = {};
	float BoneWeights[4] = {};
};
// GPU 스킨 셰이더의 정점 입력(BLENDINDICES / BLENDWEIGHT)과 바이트 단위로 같아야 한다.
static_assert(sizeof(FSkeletalVertex) == 96, "FSkeletalVertex는 GPU 스킨 정점 입력과 크기가 같아야 한다");

inline bool IsValidBoneInfluence(const FSkeletalVertex& Vertex, int32 Slot, uint32 NumBones)
{
	return Vertex.BoneWeights[Slot] > 0.0f && Vertex.BoneIndices[Slot] < NumBones;
}

struct FSkeletalBone
{
	FString Name;
	int32 ParentIndex = -1;
	FTransform RefPoseTransform;
};

struct FSkeletalMaterialSlot
{
	FString Name;
	FVector4 BaseColor = FVector4(1, 1, 1, 1);
	FString BaseColorTexturePath;
	FString NormalTexturePath;
};

struct FSkeletalMeshSection
{
	uint32 StartIndex = 0;
	uint32 IndexCount = 0;
	uint32 MaterialSlotIndex = 0;
};

struct FSkeletalMesh
{
	// 메시 로컬 -> FBX 전역의 바인드 변환. Import에서 읽고 Cook에서 사용한다.
	FMatrix MeshTransform = FMatrix::Identity;
	// 메시 로컬 -> 각 본의 바인드 로컬. 공통 Bones 배열의 인덱스에 대응한다.
	TArray<FMatrix> InverseBindPoseMat;
	TArray<FSkeletalVertex> Vertices;
	TArray<uint32> Indices;
	TArray<FSkeletalMeshSection> Sections;
	TArray<FSkeletalMaterialSlot> MaterialSlots;
};

struct FSkeletalMeshData
{
	TArray<FSkeletalMesh> Meshes;
	TArray<FSkeletalBone> Bones;
};
