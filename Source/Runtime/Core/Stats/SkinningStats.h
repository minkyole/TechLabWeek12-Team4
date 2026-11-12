#pragma once

#include "LightweightStats.h"

// CPU / GPU 스키닝 비교용 구간 Stat. "stat skinning"으로 오버레이에 표시한다.
namespace SkinningStats
{
	// 애니메이션 평가: AnimInstance 갱신과 포즈 평가, ApplyPose까지.
	DECLARE_CYCLE_STAT("Skinning: Anim Evaluate", STAT_AnimEvaluate);
	// 스키닝: FK, 스킨 행렬 작성, 본 상자 바운드. CPU 모드는 정점 가중합과 탄젠트 재계산이 더해진다.
	DECLARE_CYCLE_STAT("Skinning: Skin", STAT_Skin);
	// 업로드: CPU 모드는 스킨된 정점 버퍼, GPU 모드는 본 행렬 버퍼(t30, t31).
	DECLARE_CYCLE_STAT("Skinning: Upload", STAT_Upload);
	// GPU 모드에서 피킹, Decal이 요청해 CPU로 한 번 더 스키닝한 시간.
	DECLARE_CYCLE_STAT("Skinning: CPU On-Demand", STAT_OnDemand);

	// 이번 프레임에 스키닝한 컴포넌트 수와 그중 GPU 경로, CPU 경로, GPU 모드인데 CPU로 대체된 수.
	DECLARE_COUNTER_STAT("Skinning Components", STAT_Components);
	DECLARE_COUNTER_STAT("Skinning GPU Components", STAT_GPUComponents);
	DECLARE_COUNTER_STAT("Skinning CPU Components", STAT_CPUComponents);
	DECLARE_COUNTER_STAT("Skinning CPU Fallbacks", STAT_Fallbacks);
	// 스키닝한 메시들의 원본 정점 수와 본 수 합계. 두 모드가 같은 일을 했는지 확인하는 기준이다.
	DECLARE_COUNTER_STAT("Skinning Vertices", STAT_Vertices);
	DECLARE_COUNTER_STAT("Skinning Bones", STAT_Bones);
	// CPU가 정점을 실제로 스키닝한 수 (CPU 경로 + On-Demand).
	DECLARE_COUNTER_STAT("Skinning CPU Vertices", STAT_CPUVertices);
	DECLARE_COUNTER_STAT("Skinning On-Demand Count", STAT_OnDemandCount);
	// 이번 프레임에 GPU로 올린 바이트 수.
	DECLARE_COUNTER_STAT("Skinning Upload Bytes", STAT_UploadBytes);
}
