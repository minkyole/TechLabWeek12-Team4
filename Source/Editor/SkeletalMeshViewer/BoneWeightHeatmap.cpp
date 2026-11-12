#include "EnginePCH.h"
#include "BoneWeightHeatmap.h"
#include "Render/RenderCommand.h"
#include "Render/Mesh.h"

bool FBoneWeightHeatmap::Update(const USkeletalMesh* Mesh, int32 SelectedBoneIndex)
{
	if (!Mesh) return false;

	const FSkeletalMeshData& MeshData = Mesh->GetMeshData();
	if (SelectedBoneIndex < 0 || SelectedBoneIndex >= MeshData.Bones.Num()) return false;

	if (Mesh == CachedMesh && SelectedBoneIndex == CachedBoneIndex && WeightBuffer)
		return true;

	const TArray<float> Weights = BuildVertexWeightsForBone(MeshData, static_cast<uint32>(SelectedBoneIndex));
	const uint32 Count = static_cast<uint32>(Weights.Num());
	if (Count == 0) return false;

	if (!WeightBuffer || WeightBuffer->GetMaxCount() < Count)
	{
		WeightBuffer = RenderCommand::CreateDynamicStructuredBuffer(sizeof(float), Count);
	}
	if (!WeightBuffer || !WeightBuffer->GetBuffer())
	{
		WeightBuffer.reset();
		Invalidate();
		return false;
	}
	RenderCommand::UpdateBufferData(WeightBuffer.get(), Weights.GetData(), Count * static_cast<uint32>(sizeof(float)));

	CachedMesh = Mesh;
	CachedBoneIndex = SelectedBoneIndex;
	return true;
}

void FBoneWeightHeatmap::Invalidate()
{
	CachedMesh = nullptr;
	CachedBoneIndex = -1;
}

void FBoneWeightHeatmap::Bind() const
{
	if (WeightBuffer)
		RenderCommand::BindShaderResource(ShaderSlot, WeightBuffer.get(), EShaderBindFlagBits::Vertex);
}

void FBoneWeightHeatmap::Unbind() const
{
	RenderCommand::UnbindShaderResource(ShaderSlot, EShaderBindFlagBits::Vertex);
}

TArray<float> FBoneWeightHeatmap::BuildVertexWeightsForBone(const FSkeletalMeshData& SkeletalMeshData, uint32 SelectedBoneNumber)
{
	TArray<float> VertexWeights;

	uint32 ArraySize = 0;
	for (const auto& Mesh : SkeletalMeshData.Meshes)
	{
		ArraySize += Mesh.Vertices.Num();
	}
	VertexWeights.Reserve(ArraySize);

	uint32 BonesNum = static_cast<uint32>(SkeletalMeshData.Bones.Num());


		for (const auto& Mesh : SkeletalMeshData.Meshes)
		{
			for (const auto& Vertex : Mesh.Vertices)
			{
				float SelectedWeight = 0;
				float ValidWeightSum = 0;
				for (uint32 Index = 0; Index < 4; Index++)
				{
					uint32 BoneIndex = Vertex.BoneIndices[Index];

					if (!IsValidBoneInfluence(Vertex, Index, BonesNum))
					{
						continue;
					}

					ValidWeightSum += Vertex.BoneWeights[Index];
					if (SelectedBoneNumber == BoneIndex)
					{
						SelectedWeight += Vertex.BoneWeights[Index];
					}
				}
				if (ValidWeightSum > 0)
				{
					SelectedWeight /= ValidWeightSum;
				}

				VertexWeights.Add(SelectedWeight);
			}
		}

	return (VertexWeights);
}
