#ifndef DECAL_COMMON_HLSLI
#define DECAL_COMMON_HLSLI

// SV_Target0은 DBuffer(색), SV_Target1은 Normal DBuffer다. Additive 패스는 RTV가 하나라 Target1은 버려진다.
struct FDecalOutput
{
    float4 Color : SV_Target0;
    float4 Normal : SV_Target1;
};

// Decal 노말 맵 값을 표면 기준 접선 축으로 월드 법선으로 바꾼다.
// N은 표면 법선, T와 B는 Decal의 U(+Y), V(-Z) 방향을 표면에 눕힌 축이다. 식은 UberLit의 노말 맵과 같다.
float3 GetDecalWorldNormal(float3 SurfaceNormal, float4x4 WorldToDecal, float3 TangentNormal)
{
    // WorldToDecal의 열은 Decal 로컬 축의 월드 방향이다.
    const float3 AxisU = float3(WorldToDecal[0][1], WorldToDecal[1][1], WorldToDecal[2][1]);
    const float3 AxisV = -float3(WorldToDecal[0][2], WorldToDecal[1][2], WorldToDecal[2][2]);

    const float3 N = normalize(SurfaceNormal);
    float3 T = AxisU - N * dot(N, AxisU);
    // 표면이 U 축과 평행하면 눕힐 수 없으므로 V 축으로 대신 만든다.
    T = dot(T, T) > 1e-8f ? normalize(T) : normalize(cross(AxisV, N));
    float3 B = cross(N, T);
    B = dot(B, AxisV) < 0.0f ? -B : B;

    return normalize(T * TangentNormal.x + B * -TangentNormal.y + N * TangentNormal.z);
}

// 법선과 덮은 정도를 Normal DBuffer 형식으로 만든다. 알파 블렌딩 뒤 rgb에는 알파를 곱한 값이 쌓인다.
float4 EncodeDecalNormal(float3 WorldNormal, float Alpha)
{
    return float4(WorldNormal * 0.5f + 0.5f, Alpha);
}

#endif
