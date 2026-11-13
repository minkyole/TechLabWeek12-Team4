#include "EnginePCH.h"
#include "SpriteParticleSceneProxy.h"

#include "Asset/AssetManager.h"
#include "Components/SpriteParticleTestComponent.h"
#include "Render/Material.h"
#include "Render/RenderPacket.h"

#include <cassert>
#include <utility>

FSpriteParticleSceneProxy::FSpriteParticleSceneProxy(
    USpriteParticleTestComponent* Component)
    : FPrimitiveSceneProxy(Component)
{
}

void FSpriteParticleSceneProxy::UpdateDynamicData()
{
	const auto* Component =
	    static_cast<const USpriteParticleTestComponent*>(
	        GetComponent());

	QuadMesh =
	    UAssetManager::GetAssetByPath<UStaticMesh>("ParticleQuad");

	DynamicData = MakeUnique<FDynamicSpriteEmitterData>(
	    Component->BuildReplayData());
	
	const auto& Source = DynamicData->GetSource();

	// 이번 단계에서 지원하는 실제 재질 타입은 UMaterial이다.
	Material = Cast<UMaterial>(Source.MaterialInterface);

	assert(QuadMesh);
	assert(Material);
	assert(!Source.bUseLocalSpace);

	// 주소를 패킷에 전달하기 전에 크기를 확정한다.
	ParticleParams.SetNum(Source.ActiveParticleCount);

	for (int32 ActiveIndex = 0;
		ActiveIndex < Source.ActiveParticleCount;
		++ActiveIndex)
	{
		const FBaseParticle& Particle =
		    Source.GetParticle(ActiveIndex);

		FSubUVConstants Params{};

		// Atlas 전체를 하나의 Sprite로 사용한다.
		Params.CurrentFrame = 0.0f;
		Params.AtlasRowSize = 1.0f;
		Params.AtlasColSize = 1.0f;

		Params.Alpha = Particle.Color.A;
		Params.Tint = FVector4(
		    Particle.Color.R,
		    Particle.Color.G,
		    Particle.Color.B,
		    1.0f);

		// 입력 색은 FLinearColor이고 텍스처는 흰색이다.
		// 기존 셰이더의 Tint sRGB 변환을 사용하지 않는다.
		Params.DecodeSRGB = 0.0f;

		ParticleParams[ActiveIndex] = Params;
	}
}

bool FSpriteParticleSceneProxy::GatherDynamicRenderPackets(
    FRenderQueue& RenderQueue,
    FRenderQueue*,
    const FVector& CameraLocation,
    FBillboardMatrixBuilder BuildBillboardMatrix,
    const void* ViewContext,
    TArray<int32>&,
    TArray<float>&) const
{
	if (!DynamicData || !QuadMesh || !Material)
		return true;

	if (!BuildBillboardMatrix)
		return true;

	const auto& Source = DynamicData->GetSource();

	assert(!Source.bUseLocalSpace);
	assert(ParticleParams.Num() == Source.ActiveParticleCount);

	for (int32 ActiveIndex = 0;
		ActiveIndex < Source.ActiveParticleCount;
		++ActiveIndex)
	{
		const FBaseParticle& Particle =
		    Source.GetParticle(ActiveIndex);

		const FVector WorldPosition = Particle.Location;

		const float Width = Particle.Size.X * Source.Scale.X;
		const float Height = Particle.Size.Y * Source.Scale.Y;

		const FMatrix WorldMatrix = BuildBillboardMatrix(
		    WorldPosition,
		    Width,
		    Height,
		    ViewContext);

		FRenderPacket Packet;

		Packet.Mesh = QuadMesh;
		Packet.Material = Material;

		Packet.Model =
		    RenderQueue.StoreWorldMatrix(WorldMatrix);

		Packet.CameraToParticleDistance =
		    (WorldPosition - CameraLocation).SizeSquared();

		Packet.MaterialParamData =
		    &ParticleParams[ActiveIndex];

		Packet.MaterialParamDataSize =
		    static_cast<uint32>(sizeof(FSubUVConstants));

		RenderQueue.Add(Packet);
	}

	return true;
}