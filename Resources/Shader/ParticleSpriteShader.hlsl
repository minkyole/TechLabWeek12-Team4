#pragma pack_matrix(row_major)

cbuffer ParticleViewConstants : register(b3)
{
    float4x4 ViewProjection;
    
    float3 CameraRight;
    uint ScreenAlignment;
    
    float3 CameraUp;
    uint AxisLock;
    
    float3 CameraPosition;
    uint bOrthographic;

    float3 CameraForward;
    float ParticleViewPadding;
};

static const uint ALIGN_FACING_CAMERA_POSITION = 0u;
static const uint ALIGN_SQUARE = 1u;
static const uint ALIGN_RECTANGLE = 2u;
static const uint ALIGN_VELOCITY = 3u;
static const uint ALIGN_DISTANCE_BLEND = 4u;

static const uint AXIS_NONE = 0u;
static const uint AXIS_X = 1u;
static const uint AXIS_Y = 2u;
static const uint AXIS_Z = 3u;


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
    float3 Velocity : INSTANCE_VELOCITY0;
};

struct PS_INPUT
{
    float4 Position : SV_POSITION;
    float2 UV : TEXCOORD0;
    float4 Color : COLOR0;
};


float3 SafeNormalize(float3 V, float3 Fallback)
{
    float LengthSquared = dot(V, V);
    
    if (LengthSquared < 1e-8f)
    {
        return Fallback;
    }
    
    return V * rsqrt(LengthSquared);
}

// N과 평행하지 않은 기준 축을 반환한다.
// N이 수직에 가까우면 Y축을, 그렇지 않으면 Z축을 반환한다.
float3 ChooseReferenceAxis(float3 N)
{
    return abs(N.z) < 0.9f
        ? float3(0.0f, 0.0f, 1.0f)
        : float3(0.0f, 1.0f, 0.0f);
}

float3 GetToCamera(float3 Center)
{
    float3 Fallback = SafeNormalize(
        -CameraForward,
        float3(-1.0f, 0.0f, 0.0f));
    
    if (bOrthographic != 0u)
    {
        return Fallback;
    }
    
    return SafeNormalize(
        CameraPosition - Center,
        Fallback);
}

void BuildFacingAxes(
    float3 ToCamera,
    out float3 Right,
    out float3 Up)
{
    float3 ReferenceUp = SafeNormalize(
        CameraUp,
        float3(0.0f, 0.0f, 1.0f));
    
    float3 RightCandidate = cross(ToCamera, ReferenceUp);
    
    if (dot(RightCandidate, RightCandidate) < 1e-8f)
    {
        ReferenceUp = ChooseReferenceAxis(ToCamera);
        RightCandidate = cross(ToCamera, ReferenceUp);
    }
    
    Right = SafeNormalize(
        RightCandidate,
        float3(0.0f, 1.0f, 0.0f));
    
    Up = SafeNormalize(
        cross(Right, ToCamera),
        ReferenceUp);
}

float3 GetLockedUp()
{
    if (AxisLock == AXIS_X)
    {
        return float3(1.0f, 0.0f, 0.0f);
    }
    
    if (AxisLock == AXIS_Y)
    {
        return float3(0.0f, 1.0f, 0.0f);
    }
    
    // AxisLock == AXIS_Z
    return float3(0.0f, 0.0f, 1.0f);
}

void BuildLockedAxes(
    float3 ToCamera,
    out float3 Right,
    out float3 Up)
{
    Up = GetLockedUp();
    
    // 카메라 방향을 고정 축에 수직인 평면에 투영한다.
    float3 Facing = ToCamera - Up * dot(ToCamera, Up);
    
    // 축 위에서 바라보면 수평 방향이 정의되지 않는다.
    if (dot(Facing, Facing) < 1e-8f)
    {
        float3 Reference = ChooseReferenceAxis(Up);
        
        Facing = Reference - Up * dot(Reference, Up);
    }
    
    Facing = SafeNormalize(Facing, float3(1.0f, 0.0f, 0.0f));
    
    Right = SafeNormalize(
        cross(Facing, Up),
        float3(0.0f, 1.0f, 0.0f));
}

void BuildVelocityAxes(
    float3 ToCamera,
    float3 Velocity,
    out float3 Right,
    out float3 Up)
{
    float3 ProjectedVelocity =
        Velocity - ToCamera * dot(Velocity, ToCamera);
    
    if (dot(ProjectedVelocity, ProjectedVelocity) < 1e-8f)
    {
        BuildFacingAxes(ToCamera, Right, Up);
        return;
    }
    
    Up = normalize(ProjectedVelocity);
    Right = normalize(cross(ToCamera, Up));
    Up = normalize(cross(Right, ToCamera));
}

void BuildBillboardAxes(
    float3 Center,
    float3 Velocity,
    out float3 Right,
    out float3 Up)
{
    float3 ToCamera = GetToCamera(Center);
    
    // 축 고정이 방향 정렬보다 우선한다.
    if (AxisLock != AXIS_NONE)
    {
        BuildLockedAxes(ToCamera, Right, Up);
        return;
    }
    
    if (ScreenAlignment == ALIGN_VELOCITY)
    {
        BuildVelocityAxes(ToCamera, Velocity, Right, Up);
        return;
    }

    if (ScreenAlignment == ALIGN_FACING_CAMERA_POSITION)
    {
        BuildFacingAxes(ToCamera, Right, Up);
        return;
    }
    
    // Square / Rectangle은 화면 평면 정렬.
    //
    // Velocity / DistanceBlend도 추가 구현 전에는
    // 임시로 화면 평면 정렬을 사용한다.
    Right = SafeNormalize(
        CameraRight,
        float3(0.0f, 1.0f, 0.0f));
    
    Up = SafeNormalize(
        CameraUp,
        float3(0.0f, 0.0f, 1.0f));
}


PS_INPUT mainVS(VS_INPUT Input)
{
    PS_INPUT Output;
    
    float3 Right;
    float3 Up;
    
    BuildBillboardAxes(
        Input.Center,
        Input.Velocity,
        Right,
        Up);
    
    float2 Size = abs(Input.Size);
    
    if (ScreenAlignment == ALIGN_SQUARE)
    {
        Size.y = Size.x;
    }
    
    // Corner가 ±0.5이므로 Size는 전체 너비와 높이.
    float2 Offset = Input.Corner * Size;
    
    // 고정 축을 보존하기 위해 축 고정 시 회전을 무시한다.
    bool bAllowRotation =
        AxisLock == AXIS_NONE &&
        ScreenAlignment != ALIGN_VELOCITY;
    
    float Rotation = bAllowRotation
        ? Input.Rotation
        : 0.0f;
    
    float S;
    float C;
    sincos(Rotation, S, C);
    
    float2 RotatedOffset;
    
    RotatedOffset.x =
        Offset.x * C - Offset.y * S;
    
    RotatedOffset.y =
        Offset.x * S + Offset.y * C;
    
    float3 WorldPosition =
        Input.Center
        + Right * RotatedOffset.x
        + Up * RotatedOffset.y;
    
    Output.Position = mul(
        float4(WorldPosition, 1.0f),
        ViewProjection);
    
    Output.UV =
        Input.UVRect.xy
        + Input.UV * Input.UVRect.zw;

    Output.Color = Input.Color;
    
    return Output;
}

float4 mainPS(PS_INPUT Input) : SV_Target
{
    return SpriteTexture.Sample(
        SpriteSampler,
        Input.UV) * Input.Color;
}
