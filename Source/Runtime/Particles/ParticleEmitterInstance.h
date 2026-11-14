#pragma once
#include "ParticleTypes.h"
#include "ParticleReplayData.h"
#include "ParticleRenderData.h"

class UParticleSystemComponent;
class UParticleEmitter;
class UParticleLODLevel;
class UParticleModuleTypeDataMesh;
class UMaterialInterface;

struct FParticleEmitterInstance
{
protected:
	void PreSpawn(FBaseParticle* Particle, const FVector& InitialLocation, const FVector& InitialVelocity);
	void PostSpawn(FBaseParticle* Particle, float InterpolationPercentage, float SpawnTime);

	/** 모든 이미터 타입에 공통인 ReplayData 필드를 채운다. 파생 클래스의 override에서 먼저 호출 */
	virtual bool FillReplayData(FDynamicEmitterReplayDataBase& OutDynamicEmitterReplayDataBase) const;

	UMaterialInterface* GetCurrentMaterial() const;

public:
	FParticleEmitterInstance() = default;
	FParticleEmitterInstance(const FParticleEmitterInstance&) = delete;
	FParticleEmitterInstance& operator= (const FParticleEmitterInstance&) = delete;

	virtual ~FParticleEmitterInstance();
	/** 이미터 타입에 맞는 DynamicEmitterData를 생성해 반환. 호출자가 delete 책임. 베이스는 nullptr */
	virtual FDynamicEmitterDataBase* GetDynamicData() const;

	void Spawn(float DeltaTime);
	void SpawnParticles(int32 Count, float StartTime, float Increment, const FVector& InitialLocation,
		const FVector& InitialVelocity);

	void KillParticle(int32 Index);
	void Init(int32 InMaxParticles, int32 InParticleSize);
	void Release();

	void Tick(float DeltaTime);
	void AgeAndKillParticles(float DeltaTime);
	void ResetParticleParameters();
	void IntegrateParticles(float DeltaTime);

	//Asset
	UParticleEmitter* SpriteTemplate = nullptr;

	// Owner
	UParticleSystemComponent* Component = nullptr;
	int32 CurrentLODLevelIndex = 0;
	UParticleLODLevel* CurrentLODLevel = nullptr;

	UMaterialInterface* CurrentMaterial = nullptr; // TODO


	//Particle
	/** 파티클 데이터 배열(메모리 블록 시작 주소). 해제할 대상은 이 포인터 하나 */
	uint8* ParticleData = nullptr;
	/** 파티클 인덱스 배열. 살아 있는 파티클의 슬롯 번호 명단 (블록 끝부분, 따로 할당하지 않음) */
	uint16* ParticleIndices = nullptr;
	/** 인스턴스 데이터 배열. 파티클마다가 아니라 인스턴스당 하나인 모듈 데이터 */
	uint8* InstanceData = nullptr;
	/** 인스턴스 데이터 배열의 크기(바이트) */
	int32 InstancePayloadSize = 0;
	/** 파티클 안에서 모듈 Payload가 시작되는 위치(바이트) */
	int32 PayloadOffset = 0;
	/** 파티클 한 개의 전체 크기(바이트). FBaseParticle + 모듈 Payload 합 */
	int32 ParticleSize = 0;
	/** ParticleData 배열에서 파티클 사이 간격(바이트). ParticleSize를 16의 배수로 올린 값 */
	int32 ParticleStride = 0;
	/** 이미터에서 현재 살아 있는 파티클 수 */
	int32 ActiveParticles = 0;
	/** 계속 증가하는 생성 카운터. Flags 하위 비트에 생성 순번으로 기록 */
	uint32 ParticleCounter = 0;
	/** 파티클 데이터 배열에 담을 수 있는 최대 파티클 수(슬롯 개수) */
	int32 MaxActiveParticles = 0;
	/** 스폰하고 남은 시간의 소수 부분 */
	float SpawnFraction = 0.0f;



};

struct FParticleSpriteEmitterInstance : public FParticleEmitterInstance
{
public:
	virtual FDynamicEmitterDataBase* GetDynamicData() const override;

protected:
	virtual bool FillReplayData(FDynamicEmitterReplayDataBase& OutDynamicEmitterReplayDataBase) const override;
};

struct FParticleMeshEmitterInstance : public FParticleEmitterInstance
{
public:
	virtual FDynamicEmitterDataBase* GetDynamicData() const override;

	UParticleModuleTypeDataMesh* MeshTypeData = nullptr;
	int32 MeshRotationOffset = InvalidPayloadOffset;

protected:
	virtual bool FillReplayData(FDynamicEmitterReplayDataBase& OutDynamicEmitterReplayDataBase) const override;
};