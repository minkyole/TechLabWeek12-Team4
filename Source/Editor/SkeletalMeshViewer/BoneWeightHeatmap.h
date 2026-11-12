#pragma once
#include "Render/SkeletalMeshData.h"
#include "Render/Buffer.h"

class USkeletalMesh;

// SkeletalMesh Viewer의 Bone Weight 히트맵용 정점별 웨이트 버퍼.
// CPU 스키닝 VertexBuffer에는 Bone 정보가 없으므로, 정점 버퍼 대신 이 버퍼를 사용한다.
class FBoneWeightHeatmap
{
public:
	static constexpr uint32 ShaderSlot = 32;

	// FViewConstants::BoneWeightHeatmapStatus 값. UberLit.hlsl의 BONE_WEIGHT_HEATMAP_*와 같아야 한다.
	static constexpr uint32 StatusOff = 0;          // 히트맵 꺼짐
	static constexpr uint32 StatusOn = 1;           // 선택한 본의 웨이트를 t32에서 읽어 칠함
	static constexpr uint32 StatusNoSelection = 2;  // 켜져 있지만 본 선택 없음 (t32를 읽지 않음)

	static TArray<float> BuildVertexWeightsForBone(const FSkeletalMeshData& SkeletalMeshData, uint32 SelectedBoneNumber);
	
	bool Update(const USkeletalMesh* Mesh, int32 SelectedBoneIndex);

	void Invalidate();

	void Bind() const;
	void Unbind() const;

private:
	TUniquePtr<FStructuredBuffer> WeightBuffer;
	const USkeletalMesh* CachedMesh = nullptr;
	int32 CachedBoneIndex = -1;
};
