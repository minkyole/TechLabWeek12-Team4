#include "EnginePCH.h"
#include "AnimationPose.h"

#include "Render/Mesh.h"

void FAnimationPose::Initialize(const USkeletalMesh& InMesh)
{
	TargetMesh = &InMesh;
	ResetToRefPose();
}

void FAnimationPose::ResetToRefPose()
{
	if (!TargetMesh)
	{
		assert(false);
		LocalTransforms.Reset();
		return;
	}

	const TArray<FSkeletalBone>& Bones = TargetMesh->MeshData.Bones;

	// 용량을 불필요하게 줄이지 않고 본 개수에 맞춘다.
	LocalTransforms.SetNum(Bones.Num(), false);

	for (int32 BoneIndex = 0; BoneIndex < Bones.Num(); ++BoneIndex)
	{
		const FTransform& RefTransform =
		    Bones[BoneIndex].RefPoseTransform;

		FAnimBoneTransform& BoneTransform =
		    LocalTransforms[BoneIndex];

		BoneTransform.Translation = RefTransform.Location;
		BoneTransform.Rotation = FQuat(RefTransform.Rotation);
		BoneTransform.Scale = RefTransform.Scale;
	}
}

bool FAnimationPose::IsCompatibleWith(
    const FAnimationPose& Other) const
{
	// 대상이 없는 두 포즈는 유효한 블랜드 입력으로 취급하지 않는다.
	if (!TargetMesh || TargetMesh != Other.TargetMesh)
	{
		return false;
	}

	const int32 BoneCount = TargetMesh->MeshData.Bones.Num();

	return LocalTransforms.Num() == BoneCount &&
	       Other.LocalTransforms.Num() == BoneCount;
}