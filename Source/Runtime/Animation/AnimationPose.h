#pragma once

#include "Math/Vector.h"
#include "Math/Quat.h"
#include "Math/Transform.h"

class USkeletalMesh;

struct FAnimBoneTransform
{
	FVector Translation = FVector::ZeroVector;
	FQuat Rotation = FQuat::Identity();
	FVector Scale = FVector::OneVector;

	static FAnimBoneTransform FromTransform(const FTransform& In)
	{
		FAnimBoneTransform Out;
		Out.Translation = In.Location;
		Out.Rotation = In.Rotation.Quaternion().Normalized();
		Out.Scale = In.Scale;
		return Out;
	}

	FTransform ToTransform() const
	{
		return FTransform(Rotation.ToFRotator(), Translation, Scale);
	}

	FMatrix ToMatrix() const
	{
		const FMatrix R = Rotation.Normalized().ToFMatrix();
		return FMatrix(
			R[0][0] * Scale.X, R[0][1] * Scale.X, R[0][2] * Scale.X, 0.0f,
			R[1][0] * Scale.Y, R[1][1] * Scale.Y, R[1][2] * Scale.Y, 0.0f,
			R[2][0] * Scale.Z, R[2][1] * Scale.Z, R[2][2] * Scale.Z, 0.0f,
			Translation.X, Translation.Y, Translation.Z, 1.0f);
	}
};

struct FAnimationPose
{
	// 비소유 참조. 평가하는 동안 유효해야 한다.
	const USkeletalMesh* TargetMesh = nullptr;

	// 부모 본 기준 로컬 변환.
	// 인덱스는 TargetMesh->MeshData.Bones와 동일하다.
	TArray<FAnimBoneTransform> LocalTransforms;

	void Initialize(const USkeletalMesh& InMesh);
	void ResetToRefPose();

	bool IsCompatibleWith(const FAnimationPose& Other) const;
};