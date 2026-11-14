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
	FDynamicSpriteEmitterReplayData Replay;

	Replay.ActiveParticleCount = TestParticleIndices.Num();
	Replay.ParticleStride =
	    static_cast<int32>(sizeof(FBaseParticle));

	Replay.MaterialInterface = GetMaterial(0);
	Replay.bUseLocalSpace = bUseLocalSpace;
	Replay.Scale = GetParticleRenderScale();
	Replay.SortMode = EParticleSortMode::DistanceToView;
	Replay.BillboardMode = eBillboardMode;

	if (Replay.ActiveParticleCount > 0)
	{
		Replay.DataContainer.CopyFrom(
		    reinterpret_cast<const uint8*>(
		        TestParticles.GetData()),
		    TestParticles.Num() *
		        static_cast<int32>(sizeof(FBaseParticle)),
		    TestParticleIndices.GetData(),
		    TestParticleIndices.Num());
	}

	return Replay;
}

void USpriteParticleTestComponent::ResetTestParticles(
	bool bInLocalSpace)
{
	bUseLocalSpace = bInLocalSpace;

	constexpr int32 ParticleCount = 1000;
	constexpr int32 ColumnCount = 40;
	constexpr int32 RowCount =
	    (ParticleCount + ColumnCount - 1) / ColumnCount;

	constexpr float Spacing = 0.15f;
	constexpr float Pi = 3.14159265358979323846f;

	TestParticles.SetNum(ParticleCount);
	TestParticleIndices.SetNum(ParticleCount);

	const FMatrix LocalToWorld = GetWorldMatrix();

	for (int32 Index = 0; Index < ParticleCount; ++Index)
	{
		FBaseParticle& Particle = TestParticles[Index];
		Particle = FBaseParticle{};

		const int32 Column = Index % ColumnCount;
		const int32 Row = Index / ColumnCount;

		const float Horizontal =
		    (Column - (ColumnCount - 1) * 0.5f) * Spacing;

		const float Vertical =
		    (Row - (RowCount - 1) * 0.5f) * Spacing;

		const FVector LocalPosition(
		    0.0f, Horizontal, Vertical);

		Particle.Location = bUseLocalSpace
		                        ? LocalPosition
		                        : LocalToWorld.TransformPosition(LocalPosition);

		Particle.OldLocation = Particle.Location;

		Particle.Size = FVector(0.1f, 0.06f, 1.0f);
		Particle.BaseSize = Particle.Size;

		Particle.Rotation =
		    static_cast<float>(Index % 12) * Pi / 6.0f;

		Particle.Color =
		    FLinearColor(1.0f, 0.6f, 0.2f, 0.8f);

		Particle.BaseColor = Particle.Color;

		TestParticleIndices[Index] =
		    static_cast<uint16>(ParticleCount - 1 - Index);
	}

	// 파티클 변경은 컴포넌트 이동 없이도 Bounds를 변경한다.
	OnTransformDirty();

	if (FPrimitiveSceneProxy* Proxy = GetSceneProxy())
	{
		Proxy->InvalidateDynamicData();
	}
}

FVector USpriteParticleTestComponent::GetParticleRenderScale() const
{
	const FVector Scale = GetWorldScale3D();

	return FVector(
	    std::abs(Scale.X),
	    std::abs(Scale.Y),
	    std::abs(Scale.Z));
}

void USpriteParticleTestComponent::SetBillboardMode(
    EParticleBillboardMode InMode)
{
	eBillboardMode = InMode;
	if (FPrimitiveSceneProxy* Proxy = GetSceneProxy())
	{
		Proxy->InvalidateDynamicData();
	}
}
