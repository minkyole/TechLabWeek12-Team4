#ifndef SKIN_COMMON_HLSLI
#define SKIN_COMMON_HLSLI

#pragma pack_matrix(row_major)

// GPU 스키닝 공용 코드. GPU_SKINNING 변형에서만 include한다.
// 칸 번호 = 파트 * BoneStride + 본 번호. 정점 버퍼(BLENDINDICES)에 이미 더해져 있다.
// 각 파트의 마지막 칸은 MeshTransform이라 가중치 없는 정점도 같은 식으로 처리된다.
// 템플릿 인자에는 row_major를 붙일 수 없어 구조체 멤버로 명시한다. C++ FMatrix를 전치 없이 그대로 올린다.
struct FBoneMatrix
{
    row_major float4x4 M;
};
StructuredBuffer<FBoneMatrix> SkinMatrices : register(t30);   // InvBind * Global
StructuredBuffer<FBoneMatrix> NormalMatrices : register(t31); // SkinMatrix의 역전치

// 가중치는 정점 버퍼를 만들 때 합 1로 맞춰 두었고, 빈칸은 가중치 0이다.
// 결과는 컴포넌트 공간이다. 이후 기존 World 변환을 그대로 적용한다.
void SkinVertex(uint4 Indices, float4 Weights, inout float3 Position, inout float3 Normal, inout float3 Tangent)
{
    float3 SkinnedPosition = 0.0f;
    float3 SkinnedNormal = 0.0f;
    float3 SkinnedTangent = 0.0f;

    [unroll]
    for (int Slot = 0; Slot < 4; ++Slot)
    {
        const float Weight = Weights[Slot];
        const uint Index = Indices[Slot];
        const float4x4 Skin = SkinMatrices[Index].M;
        SkinnedPosition += Weight * mul(float4(Position, 1.0f), Skin).xyz;
        SkinnedNormal += Weight * mul(float4(Normal, 0.0f), NormalMatrices[Index].M).xyz;
        // 탄젠트는 표면을 따라 놓인 방향이라 노멀 행렬이 아니라 SkinMatrix로 변환한다.
        SkinnedTangent += Weight * mul(float4(Tangent, 0.0f), Skin).xyz;
    }

    Position = SkinnedPosition;
    Normal = normalize(SkinnedNormal);
    Tangent = normalize(SkinnedTangent);
}

#endif
