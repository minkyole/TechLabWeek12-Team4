cbuffer MinimapConstants : register(b0) {
    float2 ClipOrigin;
    float2 ClipSize;
    float2 PlayerCell;
    float Radius;
    float CellSize;
    float2 ViewDirection;
    float CosHalfFieldOfView;
    float PixelDiameter;
    uint2 GridSize;
    float2 Padding;
    uint4 BlockedRows[8];
};

struct PSInput {
    float4 mPosition : SV_POSITION;
    float2 mUV : TEXCOORD0;
};

PSInput mainVS(uint VertexID : SV_VertexID) {
    const float2 UVs[6] = { float2(0.0f, 0.0f), float2(1.0f, 0.0f), float2(0.0f, 1.0f), float2(0.0f, 1.0f), float2(1.0f, 0.0f), float2(1.0f, 1.0f) };
    PSInput Output;
    Output.mUV = UVs[VertexID];
    Output.mPosition = float4(ClipOrigin + Output.mUV * ClipSize, 0.0f, 1.0f);
    return Output;
}

bool IsBlocked(int2 Cell) {
    if (any(Cell < 0) || any(Cell >= int2(GridSize))) {
        return false;
    }
    const uint Row = uint(Cell.y);
    return (BlockedRows[Row >> 2][Row & 3u] & (1u << uint(Cell.x))) != 0;
}

bool HasLineOfSight(float2 Target) {
    const float2 Delta = Target - PlayerCell;
    const int2 Step = int2(sign(Delta));
    const float2 Interval = 1.0f / max(abs(Delta), 0.00001f);
    int2 Cell = int2(floor(PlayerCell));
    const float2 Boundary = float2(Cell) + float2(Delta.x >= 0.0f ? 1.0f : 0.0f, Delta.y >= 0.0f ? 1.0f : 0.0f);
    float2 Next = abs(Boundary - PlayerCell) * Interval;
    Next = float2(Step.x == 0 ? 1.0e10f : Next.x, Step.y == 0 ? 1.0e10f : Next.y);

    [loop]
    for (uint Index = 0; Index < 64; ++Index) {
        if (min(Next.x, Next.y) >= 1.0f) {
            return true;
        }
        if (abs(Next.x - Next.y) < 0.00001f) {
            if (IsBlocked(Cell + int2(Step.x, 0)) || IsBlocked(Cell + int2(0, Step.y))) {
                return false;
            }
            Cell += Step;
            Next += Interval;
        } else if (Next.x < Next.y) {
            Cell.x += Step.x;
            Next.x += Interval.x;
        } else {
            Cell.y += Step.y;
            Next.y += Interval.y;
        }
        if (IsBlocked(Cell)) {
            return false;
        }
    }
    return false;
}

float4 mainPS(PSInput Input) : SV_TARGET {
    const float2 Offset = Input.mUV * 2.0f - 1.0f;
    const float Distance = length(Offset);
    const float Edge = 2.0f / max(PixelDiameter, 1.0f);
    clip(1.0f - Distance);

    const float2 CellPosition = PlayerCell + Offset * Radius / CellSize;
    const bool Blocked = IsBlocked(int2(floor(CellPosition)));
    float3 Color = Blocked ? float3(0.38f, 0.44f, 0.45f) : float3(0.025f, 0.045f, 0.05f);
    const float Forward = dot(Offset, ViewDirection);
    const float Cone = smoothstep(CosHalfFieldOfView - Edge, CosHalfFieldOfView + Edge, Forward / max(Distance, 0.00001f));
    if (!Blocked && Cone > 0.0f && HasLineOfSight(CellPosition)) {
        Color = lerp(Color, float3(0.12f, 0.40f, 0.33f), Cone * (0.85f - Distance * 0.3f));
    }

    const float Rim = smoothstep(1.0f - Edge * 2.0f, 1.0f - Edge, Distance);
    Color = lerp(Color, float3(0.48f, 0.60f, 0.57f), Rim);
    const float2 PixelOffset = Offset * PixelDiameter * 0.5f;
    const float ForwardPixels = dot(PixelOffset, ViewDirection);
    const float SidePixels = abs(dot(PixelOffset, float2(-ViewDirection.y, ViewDirection.x)));
    const float Player = smoothstep(-5.0f, -4.0f, ForwardPixels) * (1.0f - smoothstep(8.0f, 9.0f, ForwardPixels)) * (1.0f - smoothstep((9.0f - ForwardPixels) * 0.45f - 1.0f, (9.0f - ForwardPixels) * 0.45f, SidePixels));
    Color = lerp(Color, float3(0.85f, 1.0f, 0.92f), Player);
    return float4(Color, 1.0f - smoothstep(1.0f - Edge, 1.0f, Distance));
}
