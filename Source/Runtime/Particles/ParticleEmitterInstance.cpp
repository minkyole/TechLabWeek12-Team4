#include "EnginePCH.h"
#include "ParticleEmitterInstance.h"
#include "Core/HAL/UnrealMemory.h"
#include "ParticleHelper.h"
#include "Components/ParticleSystemComponent.h"

FParticleEmitterInstance::~FParticleEmitterInstance()
{
	Release();
}

void FParticleEmitterInstance::Tick(float DeltaTime)
{ 
	AgeAndKillParticles(DeltaTime);
	ResetParticleParameters();
	Spawn(DeltaTime);
	//(UpdateModules 자리)
	IntegrateParticles(DeltaTime);
}

void FParticleEmitterInstance::AgeAndKillParticles(float DeltaTime)
{ 
	for (int32 Index = ActiveParticles - 1; Index >= 0; Index--)
	{
		uint16 NextFreeIndex = ParticleIndices[Index];
		DECLARE_PARTICLE_PTR(Particle, ParticleData + ParticleStride * NextFreeIndex);

		Particle->RelativeTime += DeltaTime * Particle->OneOverMaxLifetime;
		if (Particle->RelativeTime >= 1)
		{
			KillParticle(Index);
		}
	}
}

void FParticleEmitterInstance::ResetParticleParameters()
{
	for (int32 Index = 0; Index < ActiveParticles; Index++)
	{
		uint16 NextFreeIndex = ParticleIndices[Index];
		DECLARE_PARTICLE_PTR(Particle, ParticleData + ParticleStride * NextFreeIndex);

		Particle->Velocity = Particle->BaseVelocity;
		Particle->Color = Particle->BaseColor;
		Particle->Size = Particle->BaseSize;
		Particle->RotationRate = Particle->BaseRotationRate;
	}
}

void FParticleEmitterInstance::IntegrateParticles(float DeltaTime)
{
	for (int32 Index = 0; Index < ActiveParticles; Index++)
	{
		uint16 NextFreeIndex = ParticleIndices[Index];
		DECLARE_PARTICLE_PTR(Particle, ParticleData + ParticleStride * NextFreeIndex);

		if (Particle->Flags & STATE_Particle_JustSpawned)
		{
			Particle->Flags &= ~STATE_Particle_JustSpawned;
		}
		else
		{
			Particle->OldLocation = Particle->Location;
			Particle->Location += Particle->Velocity * DeltaTime;
			Particle->Rotation += Particle->RotationRate * DeltaTime;
		}
	}
}


void FParticleEmitterInstance::Spawn(float DeltaTime)
{
	// 임시값
	float Rate = 25.0f;
	FVector SpawnLocation = FVector(1.0f, 1.0f, 1.0f);
	FVector SpawnVelocity = FVector(0.0f, 0.0f, 1.0f);
	//

	if (Rate <= 0)
	{
		return;
	}

	float PrevSpawnFraction = SpawnFraction;
	SpawnFraction = PrevSpawnFraction + Rate * DeltaTime;
	float Increment = 1.0f / Rate;
	float StartTime = DeltaTime + PrevSpawnFraction * Increment - Increment;
	const int32 SpawnCount = static_cast<int32>(std::floor(SpawnFraction));
	SpawnParticles(SpawnCount, StartTime, Increment, SpawnLocation, SpawnVelocity);
	SpawnFraction -= SpawnCount;
}

void FParticleEmitterInstance::PreSpawn(FBaseParticle* Particle, 
	const FVector& InitialLocation, const FVector& InitialVelocity)
{
	FMemory::Memzero(Particle, ParticleSize);
	Particle->Location = InitialLocation;
	Particle->OldLocation = InitialLocation;
	Particle->BaseVelocity = InitialVelocity;
	Particle->Velocity = InitialVelocity;
}

void FParticleEmitterInstance::PostSpawn(FBaseParticle* Particle, 
	float InterpolationPercentage, float SpawnTime)
{
	//if (CurrentLODLevel->RequiredModule->bUseLocalSpace == false)
	//{
	//	if (FVector::Distance(Particle->OldLocation, Particle->Location) > 1.f)
	//	{
	//		Particle->Location += (Particle->OldLocation - Particle->Location) * InterpolationPercentage;
	//	}
	//}

	Particle->OldLocation = Particle->Location;
	Particle->Location += FVector(Particle->Velocity) * SpawnTime;

	Particle->Flags |= ((ParticleCounter++) & STATE_CounterMask);
	Particle->Flags |= STATE_Particle_JustSpawned;
}

void FParticleEmitterInstance::SpawnParticles(int32 Count, float StartTime, float Increment, 
	const FVector& InitialLocation, const FVector& InitialVelocity)
{
	float SpawnTime;
	float InterpolationPercentage = 1.0f;

	for (int32 Index = 0; Index < Count; Index++)
	{
		if (ActiveParticles >= MaxActiveParticles)
		{
			return;
		}
		uint16 NextFreeIndex = ParticleIndices[ActiveParticles];
		DECLARE_PARTICLE_PTR(Particle, ParticleData + ParticleStride * NextFreeIndex);
		PreSpawn(Particle, InitialLocation, InitialVelocity);

		//for (int32 ModuleIndex = 0; ModuleIndex < LODLevel->SpawnModules.Num(); ModuleIndex++)
		{
			// [1단계 임시] 하드코딩 초기값
			const float Lifetime = 1.0f;                       // 수명 1초
			Particle->OneOverMaxLifetime = 1.0f / Lifetime;
			Particle->RelativeTime = 0.0f;

			Particle->BaseSize = FVector(1.0f, 1.0f, 1.0f);    // Base와 현재값을 둘 다
			Particle->Size = Particle->BaseSize;

			Particle->BaseColor = FLinearColor(1.0f, 1.0f, 1.0f, 1.0f);
			Particle->Color = Particle->BaseColor;
			//
		}
		SpawnTime = StartTime - Increment * Index;
		PostSpawn(Particle, InterpolationPercentage, SpawnTime);
		InterpolationPercentage = InterpolationPercentage != 0 ? InterpolationPercentage - 1.0f / Count : 0;
		ActiveParticles++;
	}
}

void FParticleEmitterInstance::KillParticle(int32 Index)
{
	if (Index < 0 || Index >= ActiveParticles)
	{
		return;
	}
	std::swap(ParticleIndices[Index], ParticleIndices[ActiveParticles - 1]);
	ActiveParticles--;
}

void FParticleEmitterInstance::Init(int32 InMaxParticles, int32 InParticleSize)
{
	if (ParticleData)
	{
		Release();
	}

	MaxActiveParticles = std::clamp(InMaxParticles, 1, 65535);
	ParticleSize = InParticleSize;
	ParticleStride = static_cast<int32>(FMemory::QuantizeSize(InParticleSize, 16));
	int32 BlockSize = MaxActiveParticles * ParticleStride +
		MaxActiveParticles * sizeof(uint16);
	ParticleData = (uint8*)FMemory::Malloc(BlockSize);
	if (!ParticleData)
	{
		ParticleSize = 0;
		ParticleStride = 0;
		MaxActiveParticles = 0;
		return;
	}
	ParticleIndices = (uint16*)(ParticleData + MaxActiveParticles * ParticleStride);
	for (uint16 Index = 0; Index < MaxActiveParticles; Index++)
	{
		ParticleIndices[Index] = Index;
	}
	ActiveParticles = 0;
	ParticleCounter = 0;
	SpawnFraction = 0.0f;
}

void FParticleEmitterInstance::Release()
{
	if (ParticleData)
	{
		FMemory::Free(ParticleData);
		ParticleData = nullptr;
		ParticleIndices = nullptr;
		ActiveParticles = 0;
		MaxActiveParticles = 0;
		ParticleSize = 0;
		ParticleStride = 0;
	}
}

bool FParticleEmitterInstance::FillReplayData(
	FDynamicEmitterReplayDataBase& OutDynamicEmitterReplayDataBase) const
{
	// [임시] C의 FParticleDataContainer 구현(생성자/소멸자/CopyFrom)이 올라오면 #if 0 블록 해제
#if 0
	if (!ParticleData || ActiveParticles <= 0 || !Component)
	{
		return false;
	}
	OutDynamicEmitterReplayDataBase.ActiveParticleCount = ActiveParticles;
	OutDynamicEmitterReplayDataBase.ParticleStride = ParticleStride;
	OutDynamicEmitterReplayDataBase.Scale = Component->GetWorldScale3D();
	// OutDynamicEmitterReplayDataBase.SortMode =  TODO

	OutDynamicEmitterReplayDataBase.DataContainer.CopyFrom(
		ParticleData, 
		MaxActiveParticles * ParticleStride, 
		ParticleIndices, 
		ActiveParticles
	);

	// TODO: 에셋(RequiredModule)이 생기면 공통 스프라이트 정보 채우기
	// 모든 ReplayData 타입(Sprite/Mesh)이 FDynamicSpriteEmitterReplayDataBase를 상속하므로 static_cast로 접근
	// - bUseLocalSpace : 로컬 좌표 사용 여부 (지금은 기본값 false, 월드 좌표)
	// MaterialInterface는 타입마다 달라서 파생 클래스의 FillReplayData에서 설정

	return true;
#else
	(void)OutDynamicEmitterReplayDataBase;
	return false;
#endif
}

FDynamicEmitterDataBase* FParticleEmitterInstance::GetDynamicData() const
{
	return nullptr;
}

/*-----------------------------------------------------------------------------
	FParticleSpriteEmitterInstance
-----------------------------------------------------------------------------*/

bool FParticleSpriteEmitterInstance::FillReplayData(
	FDynamicEmitterReplayDataBase& OutDynamicEmitterReplayDataBase) const
{
	if (!FParticleEmitterInstance::FillReplayData(OutDynamicEmitterReplayDataBase))
	{
		return false;
	}

	OutDynamicEmitterReplayDataBase.eEmitterType = EDynamicEmitterType::Sprite;

	// TODO: 에셋(RequiredModule)이 생기면 머티리얼 설정
	// FDynamicSpriteEmitterReplayData* SpriteReplayData =
	//     static_cast<FDynamicSpriteEmitterReplayData*>(&OutDynamicEmitterReplayDataBase);
	// SpriteReplayData->MaterialInterface = ...;

	return true;
}

FDynamicEmitterDataBase* FParticleSpriteEmitterInstance::GetDynamicData() const
{
	// [임시] C의 FParticleDataContainer, FDynamicSpriteEmitterData 구현이 올라오면 #if 0 블록 해제
#if 0
	FDynamicSpriteEmitterReplayData ReplayData;
	if (!FillReplayData(ReplayData))
	{
		return nullptr;
	}

	// UE는 FDynamicSpriteEmitterData를 먼저 new하고 Source를 직접 채우지만,
	// 우리 FDynamicSpriteEmitterData는 생성자에서 ReplayData를 이동으로 받으므로 채운 뒤 넘긴다
	return new FDynamicSpriteEmitterData(std::move(ReplayData));
#else
	return nullptr;
#endif
}

bool FParticleMeshEmitterInstance::FillReplayData(
	FDynamicEmitterReplayDataBase& OutDynamicEmitterReplayDataBase) const
{
	if (!FParticleEmitterInstance::FillReplayData(OutDynamicEmitterReplayDataBase))
	{
		return false;
	}

	OutDynamicEmitterReplayDataBase.eEmitterType = EDynamicEmitterType::Mesh;

	 FDynamicMeshEmitterReplayData* MeshReplayData =
	     static_cast<FDynamicMeshEmitterReplayData*>(&OutDynamicEmitterReplayDataBase);
	 // TODO: B의 UParticleModuleTypeDataMesh가 생기면 해제
	 //MeshReplayData->Mesh = MeshTypeData ? MeshTypeData->Mesh : nullptr;
	 MeshReplayData->MeshRotationPayloadOffset  = MeshRotationOffset;
	 MeshReplayData->MaterialInterface = nullptr;

	return true;
}

FDynamicEmitterDataBase* FParticleMeshEmitterInstance::GetDynamicData() const
{ 
	// [임시] C의 FParticleDataContainer, FDynamicMeshEmitterReplayData, FDynamicMeshEmitterData 구현이 올라오면 #if 0 블록 해제
#if 0
	FDynamicMeshEmitterReplayData ReplayData;
	if (!FillReplayData(ReplayData))
	{
		return nullptr;
	}

	// UE는 FDynamicMeshEmitterData를 먼저 new하고 Source를 직접 채우지만,
	// 우리 FDynamicMeshEmitterData는 생성자에서 ReplayData를 이동으로 받으므로 채운 뒤 넘긴다
	return new FDynamicMeshEmitterData(std::move(ReplayData));
#else
	return nullptr;
#endif
}