#pragma pack_matrix(row_major)

cbuffer ParticleViewConstants : register(b3)
{
    float4x4 ViewProjection;
    
    float3 CameraRight;
    float ParticleViewPadding0;
    
    float3 CameraUp;
    float ParticleViewPadding1;
};

Texture2D SpriteTexture : register(t0);
SamplerState SpriteSampler : register(s0);

struct VS_INPUT
{
    // Slot 0: 공통 Quad.
    float2 Corner : POSITION0;
    float2 UV : TEXCOORD0;
    
    // Slot 1: 파티클별 데이터.
    float3 Center : INSTANCE_POSITION0;
    float Rotation : INSTANCE_ROTATION0;
    float2 Size : INSTANCE_SIZE0;
    float4 Color : INSTANCE_COLOR0;
    float4 UVRect : INSTANCE_UVRECT0;
};

struct PS_INPUT
{
    float4 Position : SV_POSITION;
    float2 UV : TEXCOORD0;
    float4 Color : COLOR0;
};

PS_INPUT mainVS(VS_INPUT Input)
{
    PS_INPUT Output;
    
    // 먼저 전체 너비와 높이를 적용한다.
    float2 Offset = Input.Corner * Input.Size;
    
    float S;
    float C;
    sincos(Input.Rotation, S, C);
    
    float2 RotatedOffset;
    
    RotatedOffset.x =
        Offset.x * C - Offset.y * S;
    
    RotatedOffset.y =
        Offset.x * S + Offset.y * C;
    
    float3 WorldPosition =
        Input.Center
        + CameraRight * RotatedOffset.x
        + CameraUp * RotatedOffset.y;
    
    Output.Position = mul(
        float4(WorldPosition, 1.0f),
        ViewProjection);
    
    Output.UV =
        Input.UVRect.xy
        +Input.UV * Input.UVRect.zw;

    Output.Color = Input.Color;
    
    return Output;
}

float4 mainPS(PS_INPUT Input) : SV_Target
{
    return SpriteTexture.Sample(
        SpriteSampler,
        Input.UV) * Input.Color;
}
