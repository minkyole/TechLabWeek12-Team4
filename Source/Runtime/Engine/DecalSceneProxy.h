#pragma once

#include "Components/DecalComponent.h"
#include "Math/Frustum.h"
#include "Render/Vertex.h"
#include "Render/Buffer.h"
#include "SceneManagement.h"

class FScene;

struct FDecalTriangle
{
	FVector P[3];
};

class FDecalSceneProxy
{
public:
	explicit FDecalSceneProxy(const UDecalComponent* InComponent);

	void UpdateTransform();
	void UpdateRenderState();

	FScene* GetScene() const { return Scene; }
	const FMatrix& GetDecalToWorld() const { return DecalToWorld; }
	const FMatrix& GetWorldToDecal() const { return WorldToDecal; }
	const FAABB& GetBounds() const { return Bounds; }
	UTexture2D* GetDecalTexture() const { return DecalTexture; }
	UTexture2D* GetDecalNormalTexture() const { return DecalNormalTexture; }
	const FLinearColor& GetDecalColor() const { return DecalColor; }
	int32 GetSortOrder() const { return SortOrder; }
	float GetFadeAlpha() const { return FadeAlpha; }
	float GetFadeScreenSize() const { return FadeScreenSize; }
	bool IsAdditive() const { return bAdditive; }
	FVertexBuffer* GetProjectedVertexBuffer() const { return ProjectedVertexBuffer.get(); }
	uint32 GetProjectedVertexCount() const { return ProjectedVertexCount; }
	void BuildProjectedMesh(const FScene& Scene);

private:
	static void ClipPolygonAgainstPlane
	(
		const TArray<FVector>& InPolygon,
		TArray<FVector>& OutClipped,
		int32 Axis,
		float Sign
	);

	friend class FScene;


	int32	SortOrder = 0;
	int32	PackedIndex = INDEX_NONE;
	float	FadeAlpha = 1.0f;
	float   FadeScreenSize = 0.01f; // 화면 비율
	bool	bAdditive = false;
	bool	bQueuedForUpdate = false;
	bool	bRenderStateQueued = false;
	FMatrix DecalToWorld;
	FMatrix WorldToDecal;
	FAABB	Bounds;
	FScene* Scene = nullptr;
	UTexture2D*		DecalTexture = nullptr;
	UTexture2D*		DecalNormalTexture = nullptr;
	FLinearColor	DecalColor = FLinearColor(1, 1, 1, 1);
	const UDecalComponent* Component = nullptr;

	TArray<FDecalVertex>		ProjectedVertices;
	TUniquePtr<FVertexBuffer>	ProjectedVertexBuffer = nullptr;
	uint32	ProjectedVertexCapacity = 0;
	uint32	ProjectedVertexCount = 0;
	bool	bProjectedMeshDirty = true;

	// 생성 중에만 쓰는 작업 공간. 매번 Reset해 할당을 재사용한다.
	TArray<FDecalTriangle> ProjectedCandidateTriangles;

	
};
