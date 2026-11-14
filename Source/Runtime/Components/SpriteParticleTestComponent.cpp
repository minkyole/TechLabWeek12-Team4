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
	constexpr int32 ParticleCount = 1000;
	constexpr int32 ColumnCount = 40;
	constexpr float Spacing = 0.15f;
	constexpr float Pi = 3.14159265358979323846f;

	constexpr int32 RowCount =
	    (ParticleCount + ColumnCount - 1) / ColumnCount;

	TArray<FBaseParticle> Particles;
	Particles.SetNum(ParticleCount);

	TArray<uint16> ParticleIndices;
	ParticleIndices.SetNum(ParticleCount);

	const FVector Origin = GetWorldLocation();

	for (int32 Index = 0; Index < ParticleCount; ++Index)
	{
		FBaseParticle& Particle = Particles[Index];

		const int32 Column = Index % ColumnCount;
		const int32 Row = Index / ColumnCount;

		const float Horizontal =
		    (static_cast<float>(Column) - (ColumnCount - 1) * 0.5f) * Spacing;

		const float Vertical =
		    (static_cast<float>(Row) - (RowCount - 1) * 0.5f) * Spacing;

		// YZ 평면에 40열 × 25행으로 배치.
		Particle.Location =
		    Origin + FVector(0.0f, Horizontal, Vertical);

		// 회전 여부를 확인하기 위한 직사각형.
		Particle.Size = FVector(0.1f, 0.06f, 1.0f);

		Particle.Rotation =
		    static_cast<float>(Index % 12) * Pi / 6.0f;

		Particle.Color =
		    FLinearColor(1.0f, 0.6f, 0.2f, 0.8f);

		// 비연속 순서로 읽는 경로도 함께 검증.
		ParticleIndices[Index] =
		    static_cast<uint16>(ParticleCount - 1 - Index);
	}

	FDynamicSpriteEmitterReplayData Replay;

	Replay.ActiveParticleCount = ParticleCount;

	Replay.ParticleStride =
	    static_cast<int32>(sizeof(FBaseParticle));

	Replay.MaterialInterface = GetMaterial(0);
	Replay.bUseLocalSpace = false;
	Replay.Scale = FVector(1.0f, 1.0f, 1.0f);
	Replay.SortMode = EParticleSortMode::None;

	Replay.DataContainer.CopyFrom(
	    reinterpret_cast<const uint8*>(
	        Particles.GetData()),
	    ParticleCount *
	        static_cast<int32>(sizeof(FBaseParticle)),
	    ParticleIndices.GetData(),
	    ParticleCount);

	return Replay;
}
