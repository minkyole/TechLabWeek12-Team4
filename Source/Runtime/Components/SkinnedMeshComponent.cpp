#include "EnginePCH.h"
#include "SkinnedMeshComponent.h"
#include "Render/RenderCommand.h"
#include "Collision/Ray.h"
#include "Render/SkinningSettings.h"
#include "Render/RenderResourceManager.h"
#include "Render/Material.h"
#include "Core/Stats/SkinningStats.h"

namespace
{
	// 용량이 모자랄 때만 다시 만들고, 행렬 배열 전체를 올린다.
	void UploadMatrices(TUniquePtr<FStructuredBuffer>& Buffer, const TArray<FMatrix>& Matrices)
	{
		const uint32 Count = static_cast<uint32>(Matrices.Num());
		if (Count == 0) return;
		if (!Buffer || Buffer->GetMaxCount() < Count)
		{
			Buffer = RenderCommand::CreateDynamicStructuredBuffer(sizeof(FMatrix), Count);
		}
		if (!Buffer || !Buffer->GetBuffer()) return;
		RenderCommand::UpdateBufferData(Buffer.get(), Matrices.GetData(), Count * static_cast<uint32>(sizeof(FMatrix)));
	}

	void ExpandBounds(FBox& Bounds, bool& bValid, const FBox& Box)
	{
		if (!bValid) { Bounds = Box; bValid = true; return; }
		Bounds.Expand(Box);
	}
}

USkinnedMeshComponent::USkinnedMeshComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
}

void USkinnedMeshComponent::TickComponent(float DeltaTime)
{
	Super::TickComponent(DeltaTime);
	TickPose(DeltaTime);
	FlushPendingPose();
}

void USkinnedMeshComponent::InitializeComponent()
{
	Super::InitializeComponent();

	// 복제, 로드로 메시 포인터만 복원된 경우에도
	// 컴포넌트의 실행용 본 변환을 준비한다.
	if (SkeletalMesh &&
		CurrentLocalTransforms.Num() != SkeletalMesh->MeshData.Bones.Num())
	{
		ResetPoseToBindPose();
	}
}

void USkinnedMeshComponent::TickPose(float DeltaTime)
{
	(void)DeltaTime;
}

void USkinnedMeshComponent::SetSkeletalMesh(USkeletalMesh* InSkeletalMesh)
{
	if (SkeletalMesh == InSkeletalMesh){ return; }

	SkeletalMesh = InSkeletalMesh;
	PostApplyProperties(StaticClass()->FindPropertyByName(FName("SkeletalMesh")));
}

// 복원된 메시 참조를 기준으로 실행용 캐시를 구성하는 공통 후처리
void USkinnedMeshComponent::PostApplyProperties(const FProperty* ChangedProperty)
{
	Super::PostApplyProperties(ChangedProperty);

	if (ChangedProperty && ChangedProperty->Name != FName("SkeletalMesh"))
	{
		return;
	}

	// 명시적인 메시 교체에 따른 기존 재질 덮어쓰기 제거
	if (ChangedProperty)
	{
		ClearOverrideMaterials();
	}

	// 이전 메시의 본 변환과 컴포넌트별 생성 버퍼 초기화
	CurrentLocalTransforms.Reset();
	CurrentGlobalMatrices.Reset();
	SkinnedVertexBuffer.reset();
	SkinMatrixBuffer.reset();
	NormalMatrixBuffer.reset();
	SkinnedVertices.Reset();
	SkinnedIndices.Reset();

	// 이전 포즈와 스키닝 경로의 캐시 유효 상태 초기화
	bCPUVerticesStale = false;
	bHasSkinnedBounds = false;
	bPoseDirty = false;
	bGPUSkinnedPose = false;
	AppliedSkinningModeVersion = FSkinningSettings::GetModeVersion();

	// 실행 객체에 필요한 기준 포즈와 스키닝 자원의 준비
	if (!IsTemplate())
	{
		ResetPoseToBindPose();
	}

	// 메시 제거와 재구성 결과에 따른 렌더·공간 정보 갱신 예약
	MarkRenderStateDirty();
	OnTransformDirty();
}

void USkinnedMeshComponent::ResetPoseToBindPose()
{
	CurrentLocalTransforms.Reset();
	if (SkeletalMesh)
	{
		const TArray<FSkeletalBone>& Bones = SkeletalMesh->MeshData.Bones;
		CurrentLocalTransforms.Reserve(Bones.Num());
		for (const FSkeletalBone& Bone : Bones)
			CurrentLocalTransforms.Add(FAnimBoneTransform::FromTransform(Bone.RefPoseTransform));
	}
	bPoseDirty = false;
	UpdateSkinning();
}

FOutlineGeometry USkinnedMeshComponent::GetOutlineGeometry() const
{
	if (!SkeletalMesh) return {};
	// GPU 모드면 바인드 포즈 정점(96바이트)과 본 행렬을 넘겨 아웃라인 셰이더의 스킨 변형이 그리게 한다.
	if (bGPUSkinnedPose)
		return { SkeletalMesh->VertexBuffer.get(), SkeletalMesh->IndexBuffer.get(), SkinMatrixBuffer.get(), NormalMatrixBuffer.get() };
	return { SkinnedVertexBuffer.get(), SkeletalMesh->IndexBuffer.get() };
}

bool USkinnedMeshComponent::LineTraceComponentLocal(const FRay& LocalRay, float& OutT)
{
	EnsureCPUSkinnedVertices();
	const uint32 VertexCount = static_cast<uint32>(SkinnedVertices.Num());
	const uint32 IndexCount = static_cast<uint32>(SkinnedIndices.Num());
	float NearestT = OutT;
	for (uint32 Index = 0; Index + 2 < IndexCount; Index += 3)
	{
		const uint32 I0 = SkinnedIndices[Index];
		const uint32 I1 = SkinnedIndices[Index + 1];
		const uint32 I2 = SkinnedIndices[Index + 2];
		if (I0 >= VertexCount || I1 >= VertexCount || I2 >= VertexCount)
			continue;

		float T;
		if (RayIntersectsTriangle(LocalRay, SkinnedVertices[I0].Position, SkinnedVertices[I1].Position,
			SkinnedVertices[I2].Position, T) && T < NearestT)
			NearestT = T;
	}

	if (NearestT >= OutT) return false;
	OutT = NearestT;
	return true;
}

//포즈를 저장만 하고 스키닝은 FlushPendingPose에서 한다.
// 같은 프레임에 여러 번 부르면 마지막 포즈만 반영된다.
bool USkinnedMeshComponent::ApplyPose(const FAnimationPose& Pose)
{
	if (!Pose.TargetMesh || !SkeletalMesh ||  
		Pose.TargetMesh != SkeletalMesh ||
		Pose.LocalTransforms.Num() != CurrentLocalTransforms.Num())
	{
		return false;
	}

	for (int Index = 0; Index < CurrentLocalTransforms.Num(); Index++)
	{
		CurrentLocalTransforms[Index] = Pose.LocalTransforms[Index];
		CurrentLocalTransforms[Index].Rotation = CurrentLocalTransforms[Index].Rotation.Normalized();
	}

	bPoseDirty = true;

	return true;
}

void USkinnedMeshComponent::FlushPendingPose()
{
	// 포즈가 그대로여도 CPU/GPU 모드가 바뀌었거나 측정용 강제 모드면 다시 스키닝한다.
	if (!bPoseDirty && AppliedSkinningModeVersion == FSkinningSettings::GetModeVersion() &&
		!FSkinningSettings::IsForceEveryFrame())
	{
		return;
	}
	bPoseDirty = false;
	UpdateSkinning();
}

bool USkinnedMeshComponent::GetBoneLocalTransform(int32 BoneIndex, FTransform& OutTransform) const
{
	if (!CurrentLocalTransforms.IsValidIndex(BoneIndex))
	{
		return false;
	}
	OutTransform = CurrentLocalTransforms[BoneIndex].ToTransform();
	return true;
}

bool USkinnedMeshComponent::SetBoneLocalTransform(int32 BoneIndex, const FTransform& InTransform)
{
	if (!SkeletalMesh || !CurrentLocalTransforms.IsValidIndex(BoneIndex)) return false;

	CurrentLocalTransforms[BoneIndex] = FAnimBoneTransform::FromTransform(InTransform);
	bPoseDirty = false;
	UpdateSkinning();
	return true;
}

int32 USkinnedMeshComponent::GetNumMaterials() const
{
	return SkeletalMesh ? SkeletalMesh->Materials.Num() : 0;
}

FString USkinnedMeshComponent::GetMaterialSlotName(int32 SlotIndex) const
{
	if (!SkeletalMesh || SlotIndex < 0 || SlotIndex >= GetNumMaterials()) return {};
	// Materials는 파트별 MaterialSlots를 같은 순서로 이어 붙인 배열이다.
	for (const FSkeletalMesh& Part : SkeletalMesh->MeshData.Meshes)
	{
		if (SlotIndex < Part.MaterialSlots.Num()) return Part.MaterialSlots[SlotIndex].Name;
		SlotIndex -= Part.MaterialSlots.Num();
	}
	return {};
}

UMaterial* USkinnedMeshComponent::GetDefaultMaterial(int32 SlotIndex) const
{
	return SkeletalMesh && SlotIndex >= 0
		? SkeletalMesh->GetMaterial(static_cast<uint32>(SlotIndex)) : nullptr;
}

void USkinnedMeshComponent::SyncSkinningMode()
{
	if (AppliedSkinningModeVersion != FSkinningSettings::GetModeVersion())
		UpdateSkinning();
}

// GPU 스킨 셰이더 변형은 UberLit에만 있다. 정점 버퍼와 본 행렬 버퍼가 준비됐고 모든 머티리얼이 UberLit일 때만 GPU로 그린다.
bool USkinnedMeshComponent::CanUseGPUSkinning() const
{
	if (!SkeletalMesh || !SkeletalMesh->VertexBuffer || !SkeletalMesh->VertexBuffer->GetBuffer()) return false;
	if (SkeletalMesh->VertexBuffer->GetStride() != sizeof(FSkeletalVertex)) return false;
	if (!SkinMatrixBuffer || !SkinMatrixBuffer->GetSRV() || !NormalMatrixBuffer || !NormalMatrixBuffer->GetSRV()) return false;
	for (int32 Slot = 0; Slot < GetNumMaterials(); ++Slot)
	{
		const UMaterial* Material = GetMaterial(Slot);
		if (Material && !FRenderResourceManager::IsUberLitProgram(Material->Shader)) return false;
	}
	return true;
}

bool USkinnedMeshComponent::BuildSkinMatrices(TArray<FMatrix>& OutSkin, TArray<FMatrix>& OutNormal) const
{
	if (!SkeletalMesh) return false;
	// CPU 스키닝과 GPU 본 행렬 버퍼가 함께 쓰는 평면 배열. 칸 p * Stride + b = 파트 p의 본 b, 마지막 칸은 MeshTransform.
	const uint32 NumBones = static_cast<uint32>(SkeletalMesh->MeshData.Bones.Num());
	const uint32 NumParts = static_cast<uint32>(SkeletalMesh->MeshData.Meshes.Num());
	const uint32 Stride = SkeletalMesh->BoneStride;          // N + 1
	if (Stride != NumBones + 1) return false;   // GPU 정점 버퍼의 칸 번호와 어긋나면 진행하지 않는다.
	if (CurrentGlobalMatrices.Num() != static_cast<int32>(NumBones)) return false;
	OutSkin.SetNum(NumParts * Stride);
	OutNormal.SetNum(NumParts * Stride);

	for (uint32 p = 0; p < NumParts; ++p)
	{
		const FSkeletalMesh& Part = SkeletalMesh->MeshData.Meshes[p];
		const FMatrix MeshNormal = Part.MeshTransform.Inverse().GetTransposed();
		// InvBind가 본 수와 다르면 파트 전체를 MeshTransform으로 둔다. 바운드의 고정 상자 처리와 같다.
		const bool bHasInvBind = Part.InverseBindPoseMat.Num() == static_cast<int32>(NumBones);
		FMatrix* Skin = &OutSkin[p * Stride];
		FMatrix* Normal = &OutNormal[p * Stride];
		for (uint32 b = 0; b < NumBones; ++b)
		{
			if (bHasInvBind)
			{
				Skin[b] = Part.InverseBindPoseMat[b] * CurrentGlobalMatrices[b];
				Normal[b] = Skin[b].Inverse().GetTransposed();
			}
			else
			{
				Skin[b] = Part.MeshTransform;  Normal[b] = MeshNormal;
			}
		}
		Skin[NumBones] = Part.MeshTransform;  Normal[NumBones] = MeshNormal;   // 가짜 본
	}
	return true;
}

void USkinnedMeshComponent::SkinVerticesOnCPU(const TArray<FMatrix>& SkinMatrices, const TArray<FMatrix>& NormalMatrices,
	TArray<FVertex>& OutVertices, TArray<uint32>& OutIndices) const
{
	OutVertices.Reset();
	OutIndices.Reset();
	const uint32 NumBones = static_cast<uint32>(SkeletalMesh->MeshData.Bones.Num());
	const uint32 NumParts = static_cast<uint32>(SkeletalMesh->MeshData.Meshes.Num());
	const uint32 Stride = SkeletalMesh->BoneStride;

	for (uint32 i = 0; i < NumParts; i++)
	{
		const FSkeletalMesh& Mesh = SkeletalMesh->MeshData.Meshes[i];
		const uint32 BaseVertex = static_cast<uint32>(OutVertices.Num());
		const FMatrix* PartSkin = &SkinMatrices[i * Stride];
		const FMatrix* PartNormal = &NormalMatrices[i * Stride];
		const FMatrix& MeshNormalMatrix = PartNormal[NumBones];

		const int32 NumVerts = Mesh.Vertices.Num();
		for (int j = 0; j < NumVerts; j++)
		{
			FVector ResultPos = FVector::ZeroVector;
			FVector ResultNormal = FVector::ZeroVector;
			float TotalWeight = 0.0f;
			FVertex Vertex = Mesh.Vertices[j].Vertex;
			const auto& SkinnedVert = Mesh.Vertices[j];

			for (int k = 0; k < 4; k++)
			{
				if (!IsValidBoneInfluence(SkinnedVert, k, NumBones))
					continue;

				const float BoneWeight = SkinnedVert.BoneWeights[k];
				const uint32 BoneIndex = SkinnedVert.BoneIndices[k];
				ResultPos += PartSkin[BoneIndex].TransformPosition(Vertex.Position) * BoneWeight;
				ResultNormal += PartNormal[BoneIndex].TransformVector(Vertex.Normal) * BoneWeight;
				TotalWeight += BoneWeight;
			}

			if (TotalWeight > 0.0f)
			{
				Vertex.Position = ResultPos / TotalWeight;
				Vertex.Normal = ResultNormal.GetSafeNormal();
			}
			else
			{
				// 본 가중치가 없는 정점에는 메시 노드의 변환을 적용한다.
				Vertex.Position = Mesh.MeshTransform.TransformPosition(Vertex.Position);
				Vertex.Normal = MeshNormalMatrix.TransformVector(Vertex.Normal).GetSafeNormal();
			}
			OutVertices.Add(Vertex);
		}
		// 파트 로컬 인덱스를 합쳐진 OutVertices 기준으로 보정한다.
		for (uint32 Index : Mesh.Indices)
		{
			OutIndices.Add(BaseVertex + Index);
		}
	}
	if (!OutVertices.IsEmpty())
		BuildMeshTangentSpace(OutVertices, OutIndices);
}

void USkinnedMeshComponent::EnsureCPUSkinnedVertices() const
{
	if (!bGPUSkinnedPose || !bCPUVerticesStale || !SkeletalMesh) return;
	bCPUVerticesStale = false;
	SCOPE_CYCLE_COUNTER(SkinningStats::STAT_OnDemand);
	FStatRegistry::AddCounter(SkinningStats::STAT_OnDemandCount);

	TArray<FMatrix> SkinMatrices;
	TArray<FMatrix> NormalMatrices;
	if (!BuildSkinMatrices(SkinMatrices, NormalMatrices)) return;
	// 피킹, Decal은 배열만 읽으므로 정점 버퍼는 올리지 않는다.
	SkinVerticesOnCPU(SkinMatrices, NormalMatrices, SkinnedVertices, SkinnedIndices);
	FStatRegistry::AddCounter(SkinningStats::STAT_CPUVertices, SkinnedVertices.Num());
}

void USkinnedMeshComponent::UpdateSkinning()
{
	AppliedSkinningModeVersion = FSkinningSettings::GetModeVersion();
	bGPUSkinnedPose = false;
	bCPUVerticesStale = false;
	CurrentGlobalMatrices.Reset();
	if (!SkeletalMesh)
	{
		SkinnedVertexBuffer.reset();
		SkinMatrixBuffer.reset();
		NormalMatrixBuffer.reset();
		bHasSkinnedBounds = false;
		return;
	}
	const TArray<FSkeletalBone>& BoneArray = SkeletalMesh->MeshData.Bones;
	if (CurrentLocalTransforms.Num() != BoneArray.Num()) return;

	// 구간 Stat: FK + 행렬 + 바운드 + (CPU) 정점 루프는 Skin, 버퍼 복사는 Upload로 나눠 잰다.
	FScopeCycleCounter SkinTimer(SkinningStats::STAT_Skin);

	CurrentGlobalMatrices.Reserve(BoneArray.Num());
	for (int32 BoneIndex = 0; BoneIndex < BoneArray.Num(); ++BoneIndex)
	{
		FMatrix CurrentMatrix = CurrentLocalTransforms[BoneIndex].ToMatrix();
		const int32 ParentIndex = BoneArray[BoneIndex].ParentIndex;
		if (ParentIndex != -1)
		{
			if (ParentIndex < 0 || ParentIndex >= BoneIndex) return;
			CurrentMatrix = CurrentMatrix * CurrentGlobalMatrices[ParentIndex];
		}
		CurrentGlobalMatrices.Add(CurrentMatrix);
	}

	const uint32 NumBones = static_cast<uint32>(BoneArray.Num());
	TArray<FMatrix> SkinMatrices;
	TArray<FMatrix> NormalMatrices;
	if (!BuildSkinMatrices(SkinMatrices, NormalMatrices)) return;
	SkinTimer.Finish();

	int32 SourceVertexCount = 0;
	for (const FSkeletalMesh& Part : SkeletalMesh->MeshData.Meshes)
		SourceVertexCount += Part.Vertices.Num();
	FStatRegistry::AddCounter(SkinningStats::STAT_Components);
	FStatRegistry::AddCounter(SkinningStats::STAT_Vertices, SourceVertexCount);
	FStatRegistry::AddCounter(SkinningStats::STAT_Bones, NumBones);

	// GPU 스키닝용 본 행렬 버퍼(t30, t31). 바인딩은 렌더러가 한다.
	// CPU 모드에서는 쓰지 않으므로 올리지 않는다. 모드를 GPU로 바꾸면 ModeVersion 때문에 이 함수가 다시 돌아 올라간다.
	if (FSkinningSettings::IsGPU())
	{
		SCOPE_CYCLE_COUNTER(SkinningStats::STAT_Upload);
		UploadMatrices(SkinMatrixBuffer, SkinMatrices);
		UploadMatrices(NormalMatrixBuffer, NormalMatrices);
		FStatRegistry::AddCounter(SkinningStats::STAT_UploadBytes,
			static_cast<uint64>(SkinMatrices.Num() + NormalMatrices.Num()) * sizeof(FMatrix));
	}

	FScopeCycleCounter BoundsTimer(SkinningStats::STAT_Skin);

	// GPU 모드 바운드: 본 공간 상자를 현재 Global로 옮겨 합치고, 포즈와 무관한 고정 상자를 더한다.
	FBox BoneDrivenBounds;
	bool bHasBoneBounds = false;
	if (SkeletalMesh->BoneBounds.Num() == static_cast<int32>(NumBones) &&
		SkeletalMesh->BoneBoundsValid.Num() == static_cast<int32>(NumBones))
	{
		for (uint32 b = 0; b < NumBones; ++b)
		{
			if (!SkeletalMesh->BoneBoundsValid[b]) continue;
			ExpandBounds(BoneDrivenBounds, bHasBoneBounds, SkeletalMesh->BoneBounds[b].GetWorldAABB(CurrentGlobalMatrices[b]));
		}
	}
	if (SkeletalMesh->bHasUnweightedBounds)
	{
		ExpandBounds(BoneDrivenBounds, bHasBoneBounds, SkeletalMesh->UnweightedBounds);
	}

	// GPU 모드: 정점은 셰이더가 스키닝하므로 CPU 정점 루프를 건너뛰고 본 상자 바운드를 쓴다.
	// CPU 정점은 피킹, Decal이 요청할 때 EnsureCPUSkinnedVertices가 채운다.
	if (FSkinningSettings::IsGPU() && bHasBoneBounds && CanUseGPUSkinning())
	{
		FStatRegistry::AddCounter(SkinningStats::STAT_GPUComponents);
		bGPUSkinnedPose = true;
		bCPUVerticesStale = true;
		SkinnedBounds = BoneDrivenBounds;
		bHasSkinnedBounds = true;
		OnTransformDirty();
		return;
	}

	FStatRegistry::AddCounter(SkinningStats::STAT_CPUComponents);
	if (FSkinningSettings::IsGPU())
		FStatRegistry::AddCounter(SkinningStats::STAT_Fallbacks);

	TArray<FVertex> ResultVertex;
	TArray<uint32> ResultIndices;
	SkinVerticesOnCPU(SkinMatrices, NormalMatrices, ResultVertex, ResultIndices);
	if (ResultVertex.IsEmpty()) return;
	FStatRegistry::AddCounter(SkinningStats::STAT_CPUVertices, ResultVertex.Num());
	BoundsTimer.Finish();

	const uint32 BufferSize = static_cast<uint32>(ResultVertex.Num()) * sizeof(FVertex);

	// 처음이거나 기존 버퍼 용량이 부족하면 생성
	if (!SkinnedVertexBuffer || SkinnedVertexBuffer->GetBufferSize() < BufferSize)
	{
		SkinnedVertexBuffer = RenderCommand::CreateDynamicVertexBuffer(BufferSize, sizeof(FVertex));
	}

	if (!SkinnedVertexBuffer || !SkinnedVertexBuffer->GetBuffer()) return;

	// 배열 전체를 버퍼에 복사
	{
		SCOPE_CYCLE_COUNTER(SkinningStats::STAT_Upload);
		RenderCommand::UpdateBufferData(SkinnedVertexBuffer.get(), ResultVertex.GetData(), BufferSize);
		FStatRegistry::AddCounter(SkinningStats::STAT_UploadBytes, BufferSize);
	}
	FScopeCycleCounter CPUBoundsTimer(SkinningStats::STAT_Skin);

	// 이동한 정점에 맞춰 Proxy의 Bounds와 그림자 갱신을 알린다.
	SkinnedBounds = FBox{ ResultVertex[0].Position, ResultVertex[0].Position };
	for (const FVertex& Vertex : ResultVertex)
	{
		SkinnedBounds.Min.X = std::min(SkinnedBounds.Min.X, Vertex.Position.X);
		SkinnedBounds.Min.Y = std::min(SkinnedBounds.Min.Y, Vertex.Position.Y);
		SkinnedBounds.Min.Z = std::min(SkinnedBounds.Min.Z, Vertex.Position.Z);
		SkinnedBounds.Max.X = std::max(SkinnedBounds.Max.X, Vertex.Position.X);
		SkinnedBounds.Max.Y = std::max(SkinnedBounds.Max.Y, Vertex.Position.Y);
		SkinnedBounds.Max.Z = std::max(SkinnedBounds.Max.Z, Vertex.Position.Z);
	}

#if defined(_DEBUG)
	// 본 상자 바운드는 CPU 스키닝 정점을 항상 감싸야 한다. 어긋나면 한 번만 알린다.
	static bool bReportedBoneBoundsMiss = false;
	if (bHasBoneBounds && !bReportedBoneBoundsMiss)
	{
		const float Tolerance = 1e-3f;
		const FBox& Inner = SkinnedBounds;
		const FBox& Outer = BoneDrivenBounds;
		if (Inner.Min.X < Outer.Min.X - Tolerance || Inner.Min.Y < Outer.Min.Y - Tolerance || Inner.Min.Z < Outer.Min.Z - Tolerance ||
			Inner.Max.X > Outer.Max.X + Tolerance || Inner.Max.Y > Outer.Max.Y + Tolerance || Inner.Max.Z > Outer.Max.Z + Tolerance)
		{
			bReportedBoneBoundsMiss = true;
			HTR_LOG(Warning, "[GPUSkin] 본 상자 바운드가 CPU 스키닝 정점을 감싸지 못함: {}", GetName());
		}
	}
#endif

	SkinnedVertices = std::move(ResultVertex);
	SkinnedIndices = std::move(ResultIndices);
	bHasSkinnedBounds = true;
	OnTransformDirty();
}