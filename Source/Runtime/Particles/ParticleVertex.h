#pragma once

// Sprite 전체가 공유하는 Quad 정점.
// 정적 VertexBuffer에 저장.
struct FParticleSpriteVertex
{
	// 예: (-0.5, 0.5) ~ (0.5, 0.5)
	FVector2 Corner;
	FVector2 UV;
};

// Sprite 파티클마다 달라지는 데이터.
// 동적 InstanceBuffer에 저장.
struct alignas(16) FParticleSpriteInstanceVertex
{
	FVector Position;
	float Rotation; // 라디안

	FVector2 Size;
	FVector2 Padding;

	FVector4 Color;

	// xy: UV 시작점, zw: UV 범위
	FVector4 UVRect;

	FVector Velocity;
	float VelocityPadding = 0.0f;
};

// Mesh 파티클마다 달라지는 데이터
// 동적 Instancebuffer에 저장.
struct alignas(16) FMeshParticleInstanceVertex
{
	// 메시 로컬 좌표를 렌더링 공간으로 변환하는 행렬.
	FMatrix Transform;

	FVector4 Color;
};

