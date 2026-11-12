#pragma once

#include "LightweightStats.h"

namespace DecalStats
{
    // 버퍼 준비와 Draw 명령 제출에 걸린 CPU 시간이다. GPU가 픽셀을 칠하는 시간은 포함하지 않는다.
    DECLARE_CYCLE_STAT("Decal Pass CPU", STAT_RenderCPU);

    // Scene에 등록된 전체 Decal 수다.
    DECLARE_COUNTER_STAT("Decal Total", STAT_Total);
    // 카메라 Frustum에 걸쳐 Draw 대상이 된 Decal 수다. 여러 View는 합산한다.
    DECLARE_COUNTER_STAT("Decal Visible", STAT_Visible);
    // Fade로 건너뛴 Decal은 세지 않고, 여러 View의 Draw는 합산한다.
    DECLARE_COUNTER_STAT("Decal Draw Calls", STAT_DrawCalls);
    DECLARE_COUNTER_STAT("Decal Additive Draw Calls", STAT_AdditiveDrawCalls);

    // 투영 메시를 다시 자른 CPU 시간과 횟수다. 아무것도 바뀌지 않은 프레임에는 0이다.
    DECLARE_CYCLE_STAT("Projected Decal Build CPU", STAT_ProjectedBuildCPU);
    DECLARE_COUNTER_STAT("Projected Decal Builds", STAT_ProjectedBuilds);
    // Projected 모드에서 전체 Decal이 가진 투영 메시 정점 수다.
    DECLARE_COUNTER_STAT("Projected Decal Vertices", STAT_ProjectedVertices);
}
