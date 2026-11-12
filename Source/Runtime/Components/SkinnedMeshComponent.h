#pragma once

#include "Components/MeshComponent.h"
#include "Render/Mesh.h"
#include "Animation/AnimationPose.h"

class USkinnedMeshComponent : public UMeshComponent
{
	DECLARE_CLASS(USkinnedMeshComponent, UMeshComponent)

	REFLECT_START(USkinnedMeshComponent)
		PROPERTY(SkeletalMesh)
	REFLECT_END()

public:
	USkinnedMeshComponent();
	virtual void TickComponent(float DeltaTime) override;
	void InitializeComponent() override;

	USkeletalMesh* GetSkeletalMesh() const { return SkeletalMesh; }
	virtual void SetSkeletalMesh(USkeletalMesh* InSkeletalMesh);
	void PostApplyProperties(const FProperty* ChangedProperty = nullptr) override;
	// 부모 본 기준의 현재 자세. 원본 RefPoseTransform은 수정하지 않는다.
	bool GetBoneLocalTransform(int32 BoneIndex, FTransform& OutTransform) const;
	bool SetBoneLocalTransform(int32 BoneIndex, const FTransform& InTransform);
	int32 GetNumMaterials() const override;
	void UpdateSkinning();
	void ResetPoseToBindPose();
	bool LineTraceComponentLocal(const FRay& LocalRay, float& OutT) override;
	bool ApplyPose(const FAnimationPose& Pose);
	void FlushPendingPose();

	FOutlineGeometry GetOutlineGeometry() const override;
	FString GetMaterialSlotName(int32 SlotIndex) const override;
	UMaterial* GetDefaultMaterial(int32 SlotIndex) const override;
	FVertexBuffer* GetSkinnedVertexBuffer() const{return SkinnedVertexBuffer.get();}

	// 마지막 스키닝이 GPU 경로였는지. true면 메시의 VertexBuffer(바인드 포즈)와 본 행렬 버퍼로 그린다.
	bool UsesGPUSkinning() const { return bGPUSkinnedPose; }
	FStructuredBuffer* GetSkinMatrixBuffer() const { return SkinMatrixBuffer.get(); }
	FStructuredBuffer* GetNormalMatrixBuffer() const { return NormalMatrixBuffer.get(); }
	// 콘솔에서 CPU/GPU 모드가 바뀌었으면 Tick을 기다리지 않고 바로 다시 스키닝한다. 패킷을 만들기 전에 부른다.
	void SyncSkinningMode();
	// 투영 Decal이 현재 자세의 삼각형을 자를 때 쓴다. 컴포넌트 로컬 좌표다.
	// GPU 모드에서는 부를 때 CPU로 한 번 스키닝해 현재 자세로 맞춘다.
	const TArray<FVertex>& GetSkinnedVertices() const { EnsureCPUSkinnedVertices(); return SkinnedVertices; }
	const TArray<uint32>& GetSkinnedIndices() const { EnsureCPUSkinnedVertices(); return SkinnedIndices; }
	// GPU 모드에서 피킹, Decal처럼 CPU 정점이 필요한 곳이 부른다. 자세가 바뀐 뒤 처음 부를 때만 계산한다.
	void EnsureCPUSkinnedVertices() const;

	// 부모 Proxy가 월드 Bounds를 계산할 때 스켈레탈 메시의 로컬 Bounds를 사용한다.
	FBox CalcLocalBounds() const override
	{
		if (bHasSkinnedBounds) return SkinnedBounds;
		return SkeletalMesh ? SkeletalMesh->GetRenderBounds() : Super::CalcLocalBounds();
	}

protected:
	virtual void TickPose(float DeltaTime);

	bool CanUseGPUSkinning() const;
	// CurrentGlobalMatrices로 파트별 스킨, 법선 행렬 평면 배열을 만든다. 칸 p * Stride + b.
	bool BuildSkinMatrices(TArray<FMatrix>& OutSkin, TArray<FMatrix>& OutNormal) const;
	// 행렬 배열로 모든 파트 정점을 CPU 스키닝하고 탄젠트를 다시 만든다.
	void SkinVerticesOnCPU(const TArray<FMatrix>& SkinMatrices, const TArray<FMatrix>& NormalMatrices,
		TArray<FVertex>& OutVertices, TArray<uint32>& OutIndices) const;

private:
	USkeletalMesh* SkeletalMesh = nullptr;
	TArray<FAnimBoneTransform> CurrentLocalTransforms;
	TArray<FMatrix> CurrentGlobalMatrices;
	TUniquePtr<FVertexBuffer> SkinnedVertexBuffer;
	TUniquePtr<FStructuredBuffer> SkinMatrixBuffer;     // t30
	TUniquePtr<FStructuredBuffer> NormalMatrixBuffer;   // t31
	// GPU 모드에서는 EnsureCPUSkinnedVertices가 필요할 때 채우므로 mutable이다.
	mutable TArray<FVertex> SkinnedVertices;
	mutable TArray<uint32> SkinnedIndices;
	mutable bool bCPUVerticesStale = false;
	FBox SkinnedBounds;
	bool bHasSkinnedBounds = false;
	bool bPoseDirty = false;
	bool bGPUSkinnedPose = false;
	uint32 AppliedSkinningModeVersion = 0;
};