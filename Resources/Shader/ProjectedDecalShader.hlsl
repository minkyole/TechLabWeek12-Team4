#pragma pack_matrix(row_major)
#include "ColorSpace.hlsli"
#include "DecalCommon.hlsli"

// DecalShader.hlsl과 같은 상수 버퍼를 쓴다. 투영 메시는 이미 월드 좌표라 DecalToClip에 ViewProjection이 들어온다.
cbuffer DecalConstants : register(b0)
{
    float4x4 DecalToClip;
    float4x4 InverseViewProjection;
    float4x4 WorldToDecal;
    float4 DecalColor;
    float2 ViewSize;
    float NormalThreshold; // 투영 메시는 생성할 때 각도로 걸러 여기서는 쓰지 않는다.
    float bHasNormalTexture; // 1이면 노말 맵을 쓴다
};

Texture2D<float> SceneDepth : register(t0);
Texture2D DecalTexture : register(t1);
Texture2D DecalNormalTexture : register(t2);
SamplerState DecalSampler : register(s0);

struct VSInput
{
    float3 Position : POSITION;
    float2 UV : TEXCOORD0;
    float3 Normal : NORMAL;
};

struct PSInput
{
    float4 Position : SV_POSITION;
    float2 UV : TEXCOORD0;
    float3 Normal : NORMAL;
};

PSInput mainVS(VSInput Input)
{
    PSInput Output;
    Output.Position = mul(float4(Input.Position, 1.0f), DecalToClip);
    Output.UV = Input.UV;
    Output.Normal = Input.Normal;
    return Output;
}

// 투영 깊이(0~1)를 같은 화면 픽셀의 월드 위치로 되돌린다.
float3 ReconstructWorldPosition(float2 NDC, float Depth)
{
    float4 WorldPos = mul(float4(NDC, Depth, 1.0f), InverseViewProjection);
    return WorldPos.xyz / WorldPos.w;
}

// 깊이 버퍼 한 칸은 거리의 제곱에 비례해 커지므로, 가려짐 판정 여유도 거리에 비례해 늘린다.
// 너무 크면 데칼 바로 앞의 얇은 물체 뒤로 데칼이 비친다.
static const float OcclusionToleranceMin = 0.002f;    // 가까운 곳의 최소 여유(월드 단위)
static const float OcclusionToleranceRatio = 0.0005f; // 거리 1당 늘어나는 여유

FDecalOutput mainPS(PSInput Input)
{
    // Decal 패스에는 깊이 버퍼를 묶지 않으므로 Pre-depth와 직접 비교해 다른 물체에 가려진 조각을 버린다.
    // 투영 깊이는 멀수록 정밀도가 떨어져 표면 위의 데칼과 원래 메시를 구분하지 못하므로,
    // 같은 시선 위의 두 점을 월드로 되돌려 선형 거리로 비교한다.
    const float Depth = SceneDepth.Load(int3(Input.Position.xy, 0));
    const float2 ScreenUV = Input.Position.xy / ViewSize;
    const float2 NDC = ScreenUV * float2(2.0f, -2.0f) + float2(-1.0f, 1.0f);

    const float3 NearPosition = ReconstructWorldPosition(NDC, 0.0f);
    const float SceneDistance = distance(ReconstructWorldPosition(NDC, Depth), NearPosition);
    const float DecalDistance = distance(ReconstructWorldPosition(NDC, Input.Position.z), NearPosition);

    // 데칼 조각이 장면 표면보다 여유 이상 뒤에 있을 때만 가려진 것으로 본다.
    const float Tolerance = max(OcclusionToleranceMin, DecalDistance * OcclusionToleranceRatio);
    clip(SceneDistance + Tolerance - DecalDistance);

    float4 Tex = DecalTexture.Sample(DecalSampler, Input.UV);
    float3 Color = SRGBToLinear(Tex.rgb) * SRGBToLinear(DecalColor.rgb);
    const float Alpha = Tex.a * DecalColor.a;

    FDecalOutput Output;
    Output.Color = float4(Color, Alpha);
    Output.Normal = float4(0.0f, 0.0f, 0.0f, 0.0f);
    if (bHasNormalTexture > 0.5f)
    {
        // 언리얼처럼 법선도 색과 같은 알파로 덮는다.
        const float3 TangentNormal = DecalNormalTexture.Sample(DecalSampler, Input.UV).xyz * 2.0f - 1.0f;
        Output.Normal = EncodeDecalNormal(GetDecalWorldNormal(Input.Normal, WorldToDecal, TangentNormal), Alpha);
    }
    return Output;
}
