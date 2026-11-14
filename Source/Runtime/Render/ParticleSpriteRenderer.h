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
	float Padding0 = 0.0f;

	FVector CameraUp;
	float Padding1 = 0.0f;
};

class FParticleSpriteRenderer
{
public:
	bool Init();

	bool Draw(
	    const TArray<FParticleSpriteInstanceVertex>& Instances,
	    UMaterial* Material,
	    const FMatrix& ViewProjection,
	    const FVector& CameraRight,
	    const FVector& CameraUp);

	// 일반 메시 큐에서 Sprite 패킷만 이동해서 분리한다.
	void CollectPackets(
	    FRenderQueue& InOutRenderQueue,
	    TArray<FRenderPacket>& OutParticlePackets);

	// 현재 바인딩된 렌더 타깃에 Sprite 배치를 그린다.
	void OnRender(
	    const TArray<FRenderPacket>& ParticlePackets,
	    const FMatrix& ViewProjection,
	    const FVector& CameraRight,
	    const FVector& CameraUp);

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
