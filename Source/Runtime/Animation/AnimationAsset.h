#pragma once
#include "Asset/RenderAsset.h"

class USkeletalMesh;

class UAnimationAsset : public URenderAsset
{
	DECLARE_CLASS(UAnimationAsset, URenderAsset)

public:
	UAnimationAsset() = default;
	~UAnimationAsset() override = default;

	virtual float GetPlayLength() const = 0;

	void SetSkeletalMesh(const USkeletalMesh* InMesh) { SkeletalMesh = InMesh; }
	const USkeletalMesh* GetSkeletalMesh() const { return SkeletalMesh; }

private:
	// USkeleton 대용. 이 메시의 MeshData.Bones를 스켈레톤으로 쓴다.
	// 본 매핑은 인덱스가 아닌 이름으로 한다. (비소유 참조)
	const USkeletalMesh* SkeletalMesh = nullptr;
};