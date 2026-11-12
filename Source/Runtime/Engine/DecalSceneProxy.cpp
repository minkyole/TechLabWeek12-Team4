#include "EnginePCH.h"
#include "DecalSceneProxy.h"
#include "Engine/Scene.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SkinnedMeshComponent.h"
#include "Render/RenderCommand.h"
#include "Render/DecalSettings.h"

// 투영 메시를 표면에서 띄우는 거리(월드 단위). 같은 깊이에 그려 깜빡이는 것을 막는다.
static constexpr float ProjectedDecalSurfaceOffset = 0.002f;

FDecalSceneProxy::FDecalSceneProxy(const UDecalComponent* InComponent) :
	Component(InComponent) {}

void FDecalSceneProxy::UpdateTransform()
{
	DecalToWorld = Component->GetTransformIncludingDecalSize();
	WorldToDecal = DecalToWorld.Inverse();
	const FBox LocalBounds{ FVector(-1.0f, -1.0f, -1.0f), FVector(1.0f, 1.0f, 1.0f) };
	Bounds = MakeWorldBounds(LocalBounds.GetWorldAABB(DecalToWorld));
	bProjectedMeshDirty = true;
}

void FDecalSceneProxy::UpdateRenderState()
{
	DecalTexture = Component->GetDecalTexture();
	DecalNormalTexture = Component->GetDecalNormalTexture();
	DecalColor = Component->GetDecalColor();
	SortOrder = Component->GetSortOrder();
	bAdditive = Component->IsAdditive();
	FadeAlpha = Component->GetFadeAlpha();
	FadeScreenSize = Component->GetFadeScreenSize();
}

// InPolygon을 Decal 공간의 평면 하나(P[Axis] * Sign <= 1)로 자른다.
// 평면 안쪽 부분만 OutClipped에 순서대로 채운다. OutClipped는 매번 비우고 시작한다.
void FDecalSceneProxy::ClipPolygonAgainstPlane(
	const TArray<FVector>& InPolygon,
	TArray<FVector>& OutClipped,
	int32 Axis,
	float Sign)
{
	OutClipped.Reset();

	const int32 Count = InPolygon.Num();
	for (int32 Index = 0; Index < Count; ++Index)
	{
		// 마지막 점 다음은 첫 점이라 다각형이 닫힌다.
		const FVector& PointA = InPolygon[Index];
		const FVector& PointB = InPolygon[(Index + 1) % Count];

		// 평면까지 안쪽으로 남은 거리. 0 이상이면 안쪽이고, 평면 위의 점도 안쪽으로 본다.
		const float InnerDistanceA = 1.0f - Sign * PointA[Axis];
		const float InnerDistanceB = 1.0f - Sign * PointB[Axis];
		const bool bInsideA = InnerDistanceA >= 0.0f;
		const bool bInsideB = InnerDistanceB >= 0.0f;

		// 변이 평면을 나가거나 들어오면 그 지점을 넣는다.
		if (bInsideA != bInsideB)
		{
			const float Alpha = InnerDistanceA / (InnerDistanceA - InnerDistanceB);
			OutClipped.Add(PointA + (PointB - PointA) * Alpha);
		}

		if (bInsideB)
		{
			OutClipped.Add(PointB);
		}
	}
}

void FDecalSceneProxy::BuildProjectedMesh(const FScene& Scene)
{
	ProjectedCandidateTriangles.Reset();
	ProjectedVertices.Reset();

	// Bounds(FAABB: Center, Extent)를 Min/Max 박스로 바꿔 둔다.
	const FBox DecalBox{ Bounds.Center - Bounds.Extent, Bounds.Center + Bounds.Extent };
	const FBox UnitBox{ FVector(-1.0f, -1.0f, -1.0f), FVector(1.0f, 1.0f, 1.0f) };

	// Decal은 로컬 +X로 투영하므로, 찍힐 표면은 그 반대 방향을 바라본다.
	const FVector FacingDirection = DecalToWorld.TransformVector(FVector(-1.0f, 0.0f, 0.0f)).Normalized();
	const float NormalThreshold = FDecalSettings::GetNormalThreshold();

	const auto BoundsTest = [&](const FBox& Box)
		{
			return Box.Intersects(DecalBox);
		};

	// 메시 로컬 삼각형 하나를 Decal 공간으로 옮겨 박스, 각도 검사를 통과하면 후보에 넣는다.
	// StaticMesh와 스켈레탈 경로가 함께 쓴다.
	const auto TryAddTriangle = [&](const FMatrix& MeshToDecal, bool bMirrored,
		const FVector& LocalP0, const FVector& LocalP1, const FVector& LocalP2)
		{
			FDecalTriangle Triangle;
			Triangle.P[0] = MeshToDecal.TransformPosition(LocalP0);
			Triangle.P[1] = MeshToDecal.TransformPosition(LocalP1);
			Triangle.P[2] = MeshToDecal.TransformPosition(LocalP2);

			// 메시 공간 조회 박스는 회전 때문에 넉넉하므로, Decal 공간의 정확한 박스로 다시 거른다.
			FBox TriangleBox{ Triangle.P[0], Triangle.P[0] };
			TriangleBox.Expand(FBox{ Triangle.P[1], Triangle.P[1] });
			TriangleBox.Expand(FBox{ Triangle.P[2], Triangle.P[2] });
			if (!TriangleBox.Intersects(UnitBox))
			{
				return;
			}

			if (bMirrored)
			{
				std::swap(Triangle.P[1], Triangle.P[2]);
			}

			// Decal 공간은 축마다 스케일이 달라 각도가 틀어지므로, 각도는 월드에서 잰다.
			const FVector WorldNormal = FVector::Cross(
				DecalToWorld.TransformPosition(Triangle.P[1]) - DecalToWorld.TransformPosition(Triangle.P[0]),
				DecalToWorld.TransformPosition(Triangle.P[2]) - DecalToWorld.TransformPosition(Triangle.P[0]));
			const float NormalLength = WorldNormal.Size();

			// SSD 셰이더와 같은 각도 리젝션. 표면이 Decal을 마주 보는 방향(-투영 방향)과 이루는 각도가
			// 설정값보다 크면 옆면, 뒷면으로 보고 버린다. 넓이가 0인 삼각형도 여기서 빠진다.
			if (NormalLength <= 0.0f || FVector::Dot(WorldNormal, FacingDirection) < NormalThreshold * NormalLength)
			{
				return;
			}

			ProjectedCandidateTriangles.Add(Triangle);
		};

	Scene.BVH.Query(BoundsTest, [&](FPrimitiveSceneProxy* Proxy)
		{
			if (!Proxy->IsVisible() || !Proxy->IsDecalReceiver()) return;

			const FMatrix MeshToDecal = Proxy->GetLocalToWorld() * WorldToDecal;
			// 음수 스케일로 뒤집힌 메시는 감김 방향도 뒤집혀 있어, 저장할 때 바깥을 향하도록 되돌린다.
			const bool bMirrored = MeshToDecal.Determinant() < 0.0f;

			// 스켈레탈은 TriangleBVH가 없으므로 현재 자세의 스키닝 삼각형을 모두 검사한다.
			// 캐릭터 Bounds가 Decal과 겹칠 때만 여기까지 오고, 자세가 바뀌면 다시 잘린다.
			if (const auto* SkinnedComponent = Cast<USkinnedMeshComponent>(Proxy->GetComponent()))
			{
				const TArray<FVertex>& Vertices = SkinnedComponent->GetSkinnedVertices();
				const TArray<uint32>& Indices = SkinnedComponent->GetSkinnedIndices();
				const uint32 VertexCount = static_cast<uint32>(Vertices.Num());
				for (int32 Index = 0; Index + 2 < Indices.Num(); Index += 3)
				{
					const uint32 I0 = Indices[Index];
					const uint32 I1 = Indices[Index + 1];
					const uint32 I2 = Indices[Index + 2];
					if (I0 >= VertexCount || I1 >= VertexCount || I2 >= VertexCount) continue;
					TryAddTriangle(MeshToDecal, bMirrored, Vertices[I0].Position, Vertices[I1].Position, Vertices[I2].Position);
				}
				return;
			}

			const auto* StaticMeshComponent = Cast<UStaticMeshComponent>(Proxy->GetComponent());
			if (!StaticMeshComponent || StaticMeshComponent->IsEditorIcon() || !Proxy->GetMesh() || !Proxy->GetRenderState())
			{
				return;
			}

			const FStaticMeshData& MeshData = Proxy->GetMesh()->GetMeshData();
			if (!MeshData.TriangleBVH)
			{
				return;
			}

			// Decal 박스를 메시 로컬로 옮겨 조회하고, 찾은 삼각형은 Decal 공간으로 옮긴다.
			const FMatrix DecalToMesh = DecalToWorld * Proxy->GetWorldToLocal();
			const FBox QueryBox = UnitBox.GetWorldAABB(DecalToMesh);

			MeshData.TriangleBVH->Query(
				[&](const FBox& Box)
				{
					return Box.Intersects(QueryBox);
				},
				[&](const FMeshTriangleElement& Element)
				{
					TryAddTriangle(MeshToDecal, bMirrored,
						Element.V0, Element.V0 + Element.Edge1, Element.V0 + Element.Edge2);
				});
		});

	// 후보 삼각형마다 박스의 6개 면으로 잘라 안쪽 조각만 남긴다.
	// 두 배열은 삼각형마다 Reset해 재사용하고, 자를 때마다 서로 바꿔 Polygon에 최신 결과를 둔다.
	TArray<FVector> Polygon;
	TArray<FVector> Clipped;

	for (const FDecalTriangle& Triangle : ProjectedCandidateTriangles)
	{
		Polygon.Reset();
		Polygon.Add(Triangle.P[0]);
		Polygon.Add(Triangle.P[1]);
		Polygon.Add(Triangle.P[2]);

		for (int32 Axis = 0; Axis < 3 && Polygon.Num() >= 3; ++Axis)
		{
			for (const float Sign : { 1.0f, -1.0f })
			{
				ClipPolygonAgainstPlane(Polygon, Clipped, Axis, Sign);
				std::swap(Polygon, Clipped);
				if (Polygon.Num() < 3)
				{
					break;
				}
			}
		}

		// 점이 3개 미만이면 박스에 닿지 않은 삼각형이다.
		if (Polygon.Num() < 3)
		{
			continue;
		}

		// 잘린 다각형은 볼록이라 첫 점 기준 부채꼴로 나눌 수 있고, 점 순서가 유지돼 감김도 그대로다.
		for (int32 Index = 1; Index + 1 < Polygon.Num(); ++Index)
		{
			const FVector DecalPoints[3] = { Polygon[0], Polygon[Index], Polygon[Index + 1] };

			FDecalVertex Vertices[3];
			for (int32 Corner = 0; Corner < 3; ++Corner)
			{
				// UV는 SSD 셰이더와 같은 식을 Decal 공간 좌표에 적용해 두 모드가 같은 자리에 찍히게 한다.
				Vertices[Corner].UV = FVector2(DecalPoints[Corner].Y * 0.5f + 0.5f, -DecalPoints[Corner].Z * 0.5f + 0.5f);
				Vertices[Corner].Position = DecalToWorld.TransformPosition(DecalPoints[Corner]);
			}

			// 띄우는 거리를 월드 단위로 정하려고 법선은 월드 좌표에서 다시 구한다.
			const FVector Cross = FVector::Cross(
				Vertices[1].Position - Vertices[0].Position,
				Vertices[2].Position - Vertices[0].Position);
			if (Cross.SizeSquared() < 1e-12f)
			{
				continue;
			}

			const FVector SurfaceNormal = Cross.Normalized();
			const FVector Offset = SurfaceNormal * ProjectedDecalSurfaceOffset;
			for (FDecalVertex& Vertex : Vertices)
			{
				Vertex.Position = Vertex.Position + Offset;
				Vertex.Normal = SurfaceNormal;
				ProjectedVertices.Add(Vertex);
			}
		}
	}

	// 버퍼는 커질 때만 새로 만들고, 나머지는 덮어써 드래그 중 매 프레임 생성과 해제가 반복되지 않게 한다.
	ProjectedVertexCount = ProjectedVertices.Num();
	if (ProjectedVertexCount > 0)
	{
		if (!ProjectedVertexBuffer || ProjectedVertexCount > ProjectedVertexCapacity)
		{
			ProjectedVertexCapacity = std::max({ ProjectedVertexCount, ProjectedVertexCapacity * 2, 64u });
			ProjectedVertexBuffer = RenderCommand::CreateDynamicVertexBuffer(
				sizeof(FDecalVertex) * ProjectedVertexCapacity, sizeof(FDecalVertex));
		}
		RenderCommand::UpdateBufferData(ProjectedVertexBuffer.get(), ProjectedVertices.GetData(),
			sizeof(FDecalVertex) * ProjectedVertexCount);
	}

	bProjectedMeshDirty = false;
}
