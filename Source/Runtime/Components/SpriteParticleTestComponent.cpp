#include "EnginePCH.h"
#include "SpriteParticleTestComponent.h"

#include "Asset/AssetManager.h"
#include "Engine/SpriteParticleSceneProxy.h"
#include "Render/Material.h"

#include <cassert>

USpriteParticleTestComponent::USpriteParticleTestComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	UMaterial* BaseMaterial =
	    UAssetManager::GetAssetByPath<UMaterial>("SubUVMaterial");

	UTexture2D* WhiteTexture =
	    UAssetManager::GetAssetByPath<UTexture2D>("WhiteTexture");

	assert(BaseMaterial);
	assert(WhiteTexture);

	// 공유 SubUVMaterial 자체를 수정하지 않는다.
	Material = UMaterial::CreateInstance(BaseMaterial);
	assert(Material);

	Material->Textures.Reset();
	Material->Textures.Add(WhiteTexture);

	Material->SetBlendState(EBlendState::AlphaBlend);
	Material->DepthStencilState = EDepthStencilState::ReadOnly;
	Material->SamplerState = ESamplerState::LinearClamp;
}

FPrimitiveSceneProxy*
USpriteParticleTestComponent::CreateSceneProxy()
{
	return new FSpriteParticleSceneProxy(this);
}

FBox USpriteParticleTestComponent::CalcLocalBounds() const
{
	// 테스트 파티클 전체를 포함하는 임시 Bounds.
	// Component 스케일은 (1, 1, 1)을 사용한다.
	FBox Bounds;
	Bounds.Min = FVector(-4.0f, -4.0f, -4.0f);
	Bounds.Max = FVector(4.0f, 4.0f, 4.0f);
	return Bounds;
}

FDynamicSpriteEmitterReplayData
USpriteParticleTestComponent::BuildReplayData() const
{
	FBaseParticle Particles[3]{};

	const FVector Origin = GetWorldLocation();

	// 카메라가 -x에서 +x 방향을 바라보는 테스트 배치를 가정한다.
	// x가 작을수록 카메라에 가깝다.

	Particles[0].Location = Origin + FVector(-2.0f, -0.6f, 0.0f);
	Particles[0].Size = FVector(1.5f, 1.5f, 1.0f);
	Particles[0].Color = FLinearColor(1.0f, 0.0f, 0.0f, 1.0f);

	Particles[1].Location = Origin + FVector(0.0f, 1.5f, 0.0f);
	Particles[1].Size = FVector(1.0f, 2.0f, 1.0f);
	Particles[1].Color = FLinearColor(0.0f, 1.0f, 0.0f, 0.5f);

	Particles[2].Location = Origin + FVector(2.0f, 0.6f, 0.0f);
	Particles[2].Size = FVector(2.0f, 1.0f, 1.0f);
	Particles[2].Color = FLinearColor(0.0f, 0.0f, 1.0f, 0.25f);

	// 물리 슬롯과 활성 목록 순서가 달라도 읽히는지 함께 검증한다.
	const uint16 ParticleIndices[3] = { 2, 0, 1 };

	FDynamicSpriteEmitterReplayData Replay;

	Replay.ActiveParticleCount = 3;
	Replay.ParticleStride =
	    static_cast<int32>(sizeof(FBaseParticle));

	Replay.MaterialInterface = GetMaterial(0);
	Replay.bUseLocalSpace = false;
	Replay.Scale = FVector(1.0f, 1.0f, 1.0f);

	// 이번에는 Renderer의 공통 반투명 정렬을 사용한다.
	Replay.SortMode = EParticleSortMode::None;

	Replay.DataContainer.CopyFrom(
	    reinterpret_cast<const uint8*>(Particles),
	    static_cast<int32>(sizeof(Particles)),
	    ParticleIndices,
	    3);

	return Replay;
}
