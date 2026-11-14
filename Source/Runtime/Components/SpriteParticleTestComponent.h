#pragma once

#include "Components/BillboardComponent.h"
#include "Particles/ParticleReplayData.h"

#include "SpriteParticleTestComponent.generated.h"

// 렌더링 검증용 컴포넌트
// 시뮬레이션 없이 고정된 파티클을 제공한다.
UCLASS()
class USpriteParticleTestComponent : public UBillboardComponent
{
	GENERATED_BODY()
public:
	USpriteParticleTestComponent();

	FPrimitiveSceneProxy* CreateSceneProxy() override;

	FBox CalcLocalBounds() const override;
	FBox CalcBounds() const override;

	FDynamicSpriteEmitterReplayData BuildReplayData() const;

	void ResetTestParticles(bool bInLocalSpace);
	void SetBillboardMode(EParticleBillboardMode InMode);
	FVector GetParticleRenderScale() const;

private:
	bool bUseLocalSpace = false;
	EParticleBillboardMode eBillboardMode = EParticleBillboardMode::ScreenAligned;

	TArray<FBaseParticle> TestParticles;
	TArray<uint16> TestParticleIndices;
};
