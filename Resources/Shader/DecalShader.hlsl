#pragma pack_matrix(row_major)
#include "ColorSpace.hlsli"
#include "DecalCommon.hlsli"

cbuffer DecalConstants : register(b0)
{
    float4x4 DecalToClip; // 박스 정점 → 클립 공간
    float4x4 InverseViewProjection; // NDC → 월드
    float4x4 WorldToDecal; // 월드 → Decal 로컬
    float4 DecalColor;
    float2 ViewSize; // 뷰의 픽셀 크기
    float NormalThreshold; // cos(최대 각도)
    float bHasNormalTexture; // 1이면 노말 맵을 쓴다
};

Texture2D<float> SceneDepth : register(t0);
Texture2D DecalTexture : register(t1);
Texture2D DecalNormalTexture : register(t2);
SamplerState DecalSampler : register(s0);

struct PSInput
{
    float4 Position : SV_POSITION;
    float2 UV : TEXCOORD0;
};


float4 mainVS(float3 Position : POSITION) : SV_POSITION
{
    
    return mul(float4(Position * 2.0f, 1.0f), DecalToClip); // 큐브가 0.5라서 2를 곱한다.
}

FDecalOutput mainPS(float4 SvPosition : SV_POSITION)
{
    float Depth = SceneDepth.Load(int3(SvPosition.xy, 0));
    
    float2 ScreenUV = SvPosition.xy / ViewSize;
    float2 NDC = ScreenUV * float2(2.0f, -2.0f) + float2(-1.0f, 1.0f);
    float4 WorldPos = mul(float4(NDC, Depth, 1.0f), InverseViewProjection);
    WorldPos /= WorldPos.w;
    float3 WorldPosToDecal = mul(WorldPos, WorldToDecal).xyz;

    // 깊이만으로는 표면 방향을 모르므로 이웃 픽셀과의 위치 차이로 법선을 만든다. 미분은 clip보다 먼저 구한다.
    float3 SurfaceNormal = cross(ddx(WorldPos.xyz), ddy(WorldPos.xyz));
    // 외적 부호는 화면 축 방향에 따라 정해지므로, 카메라를 향하도록 맞춘다.
    float4 NearPos = mul(float4(NDC, 0.0f, 1.0f), InverseViewProjection);
    float3 ViewDirection = WorldPos.xyz - NearPos.xyz / NearPos.w;
    SurfaceNormal = dot(SurfaceNormal, ViewDirection) > 0.0f ? -SurfaceNormal : SurfaceNormal;

    clip(1.0f - abs(WorldPosToDecal));

    // WorldToDecal의 첫 열은 Decal 로컬 X(투영 방향)의 월드 방향이다.
    // 표면이 Decal을 마주 보는 방향(-투영 방향)과 이루는 각도가 설정값보다 크면 옆면, 뒷면으로 보고 버린다.
    float3 ProjectionDirection = float3(WorldToDecal[0][0], WorldToDecal[1][0], WorldToDecal[2][0]);
    clip(dot(normalize(SurfaceNormal), -normalize(ProjectionDirection)) - NormalThreshold);
    
    // +X로 투영할 때 이미지 오른쪽이 +Y, 위쪽이 +Z가 되게 맞춘다. V는 아래가 +라서 Z를 뒤집는다.
    float2 DecalUV = float2(WorldPosToDecal.y, -WorldPosToDecal.z) * 0.5f + 0.5f;
    float4 Tex = DecalTexture.Sample(DecalSampler, DecalUV);

    float3 Color = SRGBToLinear(Tex.rgb) * SRGBToLinear(DecalColor.rgb);
    const float Alpha = Tex.a * DecalColor.a;

    FDecalOutput Output;
    Output.Color = float4(Color, Alpha);
    Output.Normal = float4(0.0f, 0.0f, 0.0f, 0.0f);
    if (bHasNormalTexture > 0.5f)
    {
        // 언리얼처럼 법선도 색과 같은 알파로 덮는다.
        const float3 TangentNormal = DecalNormalTexture.Sample(DecalSampler, DecalUV).xyz * 2.0f - 1.0f;
        Output.Normal = EncodeDecalNormal(GetDecalWorldNormal(SurfaceNormal, WorldToDecal, TangentNormal), Alpha);
    }
    return Output;
}
