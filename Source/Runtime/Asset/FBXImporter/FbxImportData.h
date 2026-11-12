#pragma once

#include "Render/SkeletalMeshData.h"
#include "Render/StaticMeshData.h"
#include "Animation/AnimTypes.h"

// AnimStack 하나를 일정 FPS로 구운 결과. UObject가 아니어야 FBX 경로와 캐시 경로가 같은 입력을 쓴다.
struct FFbxAnimClip
{
	FString Name;
	FFrameRate FrameRate;
	int32 NumFrames = 0; // 키 개수는 NumFrames + 1
	TArray<FBoneAnimationTrack> Tracks;
};

struct FFbxStaticMeshPart
{
	FString NodeName;
	// 메시 데이터는 로컬 공간에 저장하고, 이 변환은 가져온 씬에서 노드를 배치할 때 사용한다.
	FMatrix NodeTransform = FMatrix::Identity;
	FStaticMeshData MeshData;
};

struct FFbxCookedData
{
	FSkeletalMeshData SkeletalMesh;
	TArray<FFbxStaticMeshPart> StaticMeshes;
	TArray<FFbxAnimClip> Anims;
};
