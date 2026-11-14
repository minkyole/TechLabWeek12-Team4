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

void USpriteParticleTestComponent::OnRegister()
{
	Super::OnRegister();

	if (TestParticles.Num() == 0)
	{
		ResetTestParticles(bUseLocalSpace);
	}
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

FBox USpriteParticleTestComponent::CalcBounds() const
{
	if (TestParticleIndices.Num() == 0)
	{
		const FVector Origin = GetWorldLocation();
		return FBox{ Origin, Origin };
	}

	const FMatrix LocalToWorld = GetWorldMatrix();
	const FVector RenderScale = GetParticleRenderScale();

	FBox Result{};
	bool bInitialized = false;

	for (uint16 ParticleIndex : TestParticleIndices)
	{
		const FBaseParticle& Particle =
		    TestParticles[ParticleIndex];

		const FVector Center =
		    bUseLocalSpace
		        ? LocalToWorld.TransformPosition(Particle.Location)
		        : Particle.Location;

		const float Width =
		    std::abs(Particle.Size.X * RenderScale.X);

		const float Height =
		    std::abs(Particle.Size.Y * RenderScale.Y);

		// 전체 너비/높이 기준 반대각선 길이.
		const float Radius =
		    0.5f * std::sqrt(
		               Width * Width + Height * Height);

		const FVector Extent(Radius, Radius, Radius);

		const FBox ParticleBounds{
			Center - Extent,
			Center + Extent
		};
		
		if (!bInitialized)
		{
			Result = ParticleBounds;
			bInitialized = true;
		}
		else
		{
			Result.Expand(ParticleBounds);
		}
	}

	return Result;
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
	Replay.ScreenAlignment = eParticleScreenAlignment;
	Replay.AxisLock = eParticleAxisLock;

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

		// Velocity test
		Particle.Velocity = FVector(0.0f, 1.0f, 1.0f);
		Particle.BaseVelocity = Particle.Velocity;

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

void USpriteParticleTestComponent::SetScreenAlignment(
    EParticleScreenAlignment InAlignment)
{
	eParticleScreenAlignment = InAlignment;
	if (FPrimitiveSceneProxy* Proxy = GetSceneProxy())
	{
		Proxy->InvalidateDynamicData();
	}
}
void USpriteParticleTestComponent::SetAxisLock(
    EParticleAxisLock InAxisLock)
{
	eParticleAxisLock = InAxisLock;
	if (FPrimitiveSceneProxy* Proxy = GetSceneProxy())
	{
		Proxy->InvalidateDynamicData();
	}
}
