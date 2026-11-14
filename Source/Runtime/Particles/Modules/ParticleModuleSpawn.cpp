#include "EnginePCH.h"
#include "Particles/Modules/ParticleModuleSpawn.h"

FParticleSpawnBatch UParticleModuleSpawn::GetRateBatch(float DeltaTime, float& InOutSpawnFraction) const
{
	// InOutSpawnFraction : 지난 프레임에서 "만들다 남은 소수점"
	FParticleSpawnBatch Batch;

	if (SpawnRate <= 0.0f || DeltaTime <= 0.0f)
	{
		return Batch;
	}

	const float PrevFraction = InOutSpawnFraction; // 덮어쓰기 전에 보관
	const float Total = SpawnRate * DeltaTime + PrevFraction;

	Batch.Count = static_cast<int32>(floorf(Total));              // 이번에 만들 개수
	InOutSpawnFraction = Total - static_cast<float>(Batch.Count); // 장부에 남길 소수점

	Batch.Increment = 1.0f / SpawnRate; // 파티클 사이 간격
	Batch.StartTime = DeltaTime + PrevFraction * Batch.Increment - Batch.Increment;

	return Batch;
}

void UParticleModuleSpawn::GetBurstBatches(float PrevEmitterTime, float CurrEmitterTime, float Duration, TArray<FParticleSpawnBatch>& Out) const
{
	// 동작:
	// 이 Brust의 Time이 [Prev, Curr) 구간 안에 있나 확인
	// 있으면 FParticleSpawnBatch를 하나 만들어 Out에 추가한다.

	for (const FParticleBurst& Burst : Bursts)
	{
		if (Burst.Count <= 0)
		{
			continue;
		}

		float Elapsed = -1.0f; // 폭죽이 터진 뒤 지난 시간. -1이면 이번 프레임에 안 터짐

		if (CurrEmitterTime >= PrevEmitterTime)
		{
			// 한 바퀴 안 돌았음
			if (Burst.Time >= PrevEmitterTime && Burst.Time < CurrEmitterTime)
			{
				Elapsed = CurrEmitterTime - Burst.Time;
			}
		}
		else
		{
			// 이번 프레임에 한 바퀴 돌아서 시계가 0으로 돌아옴
			if (Burst.Time >= PrevEmitterTime && Burst.Time < Duration)
			{
				Elapsed = (Duration - Burst.Time) + CurrEmitterTime;
			}
			else if (Burst.Time >= 0.0f && Burst.Time < CurrEmitterTime)
			{
				Elapsed = CurrEmitterTime - Burst.Time;
			}
		}

		if (Elapsed >= 0.0f)
		{
			FParticleSpawnBatch Batch;
			Batch.Count = Burst.Count;
			Batch.StartTime = Elapsed;
			Batch.Increment = 0.0f;
			Out.Add(Batch);
		}
	}
}
