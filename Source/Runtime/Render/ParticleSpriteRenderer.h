#pragma once

#include "Render/Buffer.h"
#include "Particles/ParticleVertex.h"

class UMaterial;
class FShaderProgram;
struct FRenderPacket;
class FRenderQueue;

struct alignas(16) FParticleSpriteViewConstants
{
	FMatrix ViewProjection;

	FVector CameraRight;
	uint32 ScreenAlignment = 0;

	FVector CameraUp;
	uint32 AxisLock = 0;

	FVector CameraPosition;
	uint32 bOrthographic = 0;

	FVector CameraFowward;
	float Padding = 0.0f;
};

struct FParticleSpriteView
{
	FMatrix ViewProjection;

	FVector CameraRight;
	FVector CameraUp;
	FVector CameraPosition;
	FVector CameraForward;

	bool bOrthographic = false;
};

class FParticleSpriteRenderer
{
public:
	bool Init();

	bool Draw(
	    const TArray<FParticleSpriteInstanceVertex>& Instances,
	    UMaterial* Material,
	    const FParticleSpriteView& View,
		EParticleScreenAlignment ScreenAlignment,
		EParticleAxisLock AxisLock);

	// 일반 메시 큐에서 Sprite 패킷만 이동해서 분리한다.
	void CollectPackets(
	    FRenderQueue& InOutRenderQueue,
	    TArray<FRenderPacket>& OutParticlePackets);

	// 현재 바인딩된 렌더 타깃에 Sprite 배치를 그린다.
	void OnRender(
	    const TArray<FRenderPacket>& ParticlePackets,
	    const FParticleSpriteView& View);

private:
	bool EnsureCapacity(uint32 RequiredCount);

private:
	TUniquePtr<FVertexBuffer> QuadVB;
	TUniquePtr<FIndexBuffer> QuadIB;

	TUniquePtr<FVertexBuffer> InstanceVB;
	TUniquePtr<FConstantBuffer> ViewCB;

	FShaderProgram* Shader = nullptr;
	ComPtr<ID3D11InputLayout> InputLayout;

	uint32 InstanceCapacity = 0;
};
