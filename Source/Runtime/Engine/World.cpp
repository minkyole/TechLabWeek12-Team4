#include "EnginePCH.h"
#include "World.h"
#include "Level.h"
#include "Engine/Engine.h"

#include "Core/EngineStatics.h"
#include "Core/Misc/FileHelper.h"
#include "GameFramework/Actor/StaticMeshActor.h"

#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "Input/InputSystem.h"

#include "UObject/UObjectIterator.h"

#include "Collision/Ray.h"
#include "Components/BillboardComponent.h"
#include "Components/ExponentialHeightFogComponent.h"

#include "Components/StaticMeshComponent.h"
#include "Components/SkinnedMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Render/SkinningSettings.h"
#include "Components/ShapeComponent.h"
#include "Asset/LOD/StaticMeshLODSelector.h"

#include "Math/Frustum.h"
#include "ProjectedShadowInfo.h"
#include "Engine/DirectionalLightSceneProxy.h"
#include <utility>

#include "Core/Stats/LightweightStats.h"
#include "Core/Stats/EditorStats.h"
#include "Core/Async/TaskPool.h"

#include "UObject/UObjectGlobals.h"
#include "Core/Stats/ShadowStats.h"

#include "Render/ShadowSettings.h"
#include <algorithm>
#include <cmath>
#include <limits>

DECLARE_CYCLE_STAT("Actor Tick", STAT_ActorTick); // Actor 틱 측정
DECLARE_CYCLE_STAT("Update All Transforms", STAT_UpdateAllTransforms); // 각 Transform의 Update 시간 측정
DECLARE_CYCLE_STAT("Gather Render Packets", STAT_GatherRenderPackets);
DECLARE_CYCLE_STAT("Frustum Cull", STAT_FrustumCull);
DECLARE_CYCLE_STAT("Gather Elements", STAT_GatherElements);
DECLARE_CYCLE_STAT("Gather - LOD", STAT_GatherLOD);
DECLARE_CYCLE_STAT("Gather - Submit", STAT_GatherSubmit);

UWorld::~UWorld() = default;

void UWorld::SetVignette(float Intensity, float Radius, float Softness, const FVector& Color)
{
	VignetteSettings.Intensity = std::clamp(Intensity, 0.0f, 1.0f);
	VignetteSettings.Radius = std::max(Radius, 0.0f);
	VignetteSettings.Softness = std::max(Softness, 0.001f);
	VignetteSettings.Color = FVector{
		std::clamp(Color.X, 0.0f, 1.0f),
		std::clamp(Color.Y, 0.0f, 1.0f),
		std::clamp(Color.Z, 0.0f, 1.0f)
	};
}

void UWorld::ResetVignette()
{
	// 연출 파라미터만 초기화하고 Settings 패널의 마스터 스위치 상태는 유지한다.
	const bool bWasEnabled = VignetteSettings.bEnabled;
	VignetteSettings = FVignetteSettings{};
	VignetteSettings.bEnabled = bWasEnabled;
}

bool UWorld::Init()
{
	// Spawn Actor로 카메라 생성하고 세팅하기
	PersistentLevel = NewObject<ULevel>(this);

	if (!PersistentLevel)
	{
		HTR_LOG(Error, "Failed to create PersistentLevel");
		return false;
	}

	//레벨 연결
	PersistentLevel->SetWorld(this);
	Levels.Add(PersistentLevel);
	CurrentLevel = PersistentLevel;

	//카메라 생성
	CreateMainCamera();

	return true;
}

AActor* UWorld::SpawnActor(UClass* Class, FName InName, const FTransform* Transform)
{
	if (!Class) return nullptr;
	if (!Class->IsChildOf(AActor::StaticClass())) return nullptr;

	// 1. ObjectFactory로 Actor 생성
	AActor* NewActor = NewObject<AActor>(PersistentLevel, Class, InName);

	if (!NewActor)
	{
		HTR_LOG(Error, "SpawnActor : Failed to create Actor");
		return nullptr;
	}

	// 2. Actor에 World/Level 연결
	NewActor->World = this;
	NewActor->Level = PersistentLevel;

	// 3. Transform 적용. 호출자가 지정하지 않으면 생성자가 정한 Root Transform을 유지한다.
	if (!NewActor->GetRootComponent())
		NewActor->CreateDefaultSceneRoot();

	if (Transform && NewActor->GetRootComponent())
	{
		NewActor->GetRootComponent()->SetTransform(*Transform);
	}

	NewActor->RegisterAllComponents();

	// 4. Level->Actors에 등록
	PersistentLevel->AddActor(NewActor);

	// 5. PlayList에 추가
	BeginPlayList.Enqueue(NewActor);

	return NewActor;
}

void UWorld::Tick(float DeltaTime) {
	{
		SCOPE_CYCLE_COUNTER(STAT_UpdateAllTransforms);
		Scene.UpdateAllTransforms();
	}

	// 에디터 월드면 더이상 틱을 돌리지 않는다.
	if (WorldType != EWorldType::Editor)
	{
		// 입력받은 실제 DeltaTime으로 이번 프레임의 FGameTime을 만든 뒤,
		// 기존 Tick 경로에는 Time Dilation이 적용된 월드 DeltaTime을 전달한다.
		UpdateGameTime(DeltaTime);
		DeltaTime = GameTime.GetDeltaWorldTimeSeconds();

		bInGameTick = true;
		while (!BeginPlayList.IsEmpty())
		{
			BeginPlayList.Peek()->BeginPlay();
			BeginPlayList.Dequeue();
		}

		// 액터 수와 관계없이 게임 월드 프레임마다 한 번만 갱신한다.
		if (FLuaVM* LuaVM = GEngine ? GEngine->GetLuaVM() : nullptr)
			LuaVM->UpdateCoroutine(DeltaTime);

		{
			SCOPE_CYCLE_COUNTER(STAT_ActorTick);
			// 모든 Actor를 도는 대신 등록된 Tick 함수(메인 카메라 포함)만 실행한다.
			TickTaskManager.RunAllTickGroups(DeltaTime);
		}

		GatherOverlap(); // 게임 중에만 충돌처리
		bInGameTick = false;

		FlushPendingDestroyComponents();
		FlushPendingDestroyActors();

		if (WorldType == EWorldType::PIE || WorldType == EWorldType::Game) {
			APlayerCameraManager* CameraManager{ GetPlayerCameraManager() };
			if (CameraManager) {
				CameraManager->UpdateCamera(DeltaTime);
			}
		}
	}
	// 에디터 월드는 컴포넌트 Tick이 돌지 않는다. 스키닝 측정 모드(skinning force on)일 때만
	// 스켈레탈 메시를 매 프레임 다시 스키닝해 에디터 뷰포트에서도 CPU / GPU를 비교할 수 있게 한다.
	else if (FSkinningSettings::IsForceEveryFrame() && PersistentLevel)
	{
		for (AActor* Actor : PersistentLevel->GetActors())
		{
			if (!Actor) continue;
			for (UActorComponent* Component : Actor->GetComponents())
			{
				if (USkinnedMeshComponent* Skinned = Cast<USkinnedMeshComponent>(Component))
					Skinned->FlushPendingPose();
			}
		}
	}


}

void UWorld::SetTimeScale(float InTimeScale) {
	if (!std::isfinite(InTimeScale)) {
		HTR_LOG(Warning, "[Time] Ignored non-finite time scale");
		return;
	}

	TimeScale = std::clamp(InTimeScale, 0.0f, 10.0f);
}

void UWorld::ResetTimeEffects() {
	TimeScale = 1.0f;
}

void UWorld::UpdateGameTime(float DeltaTime) {
	const float DeltaRealTimeSeconds{ std::isfinite(DeltaTime) ? std::max(0.0f, DeltaTime) : 0.0f };

	const float DeltaWorldTimeSeconds{ DeltaRealTimeSeconds * TimeScale };

	GameTime.DeltaRealTimeSeconds = DeltaRealTimeSeconds;
	GameTime.DeltaWorldTimeSeconds = DeltaWorldTimeSeconds;
	GameTime.RealTimeSeconds += static_cast<double>(DeltaRealTimeSeconds);
	GameTime.WorldTimeSeconds += static_cast<double>(DeltaWorldTimeSeconds);
	GameTime.TimeDilation = TimeScale;
}

void UWorld::ClearWorld() {
	if (mPlayerCameraManager) {
		mPlayerCameraManager->RemoveAllCameraModifiers();
		mPlayerCameraManager->SetViewTarget(nullptr);
	}
	mPlayerCameraManager = nullptr;
	mPlayCameraComponent = nullptr;

	// BeginPlay 대기 중인 Actor 제거
	while (!BeginPlayList.IsEmpty())
	{
		BeginPlayList.Dequeue();
	}
	PendingDestroyComponents.Reset();
	PendingDestroyActors.Reset();


	for (ULevel* Level : Levels)
	{
		for (AActor* Actor : Level->Actors)
		{
			if (!Actor) continue;
			Actor->UnregisterAllComponents();
			Actor->RegisterAllActorTickFunctions(false);
		}
		Level->ClearActors();
	}
	// 액터를 지우기 전에 렌더 프록시와 틱 등록부터 푼다. ClearActors는 액터를 delete만 하므로,
	// 그대로 두면 지워진 컴포넌트를 가리키는 프록시가 FScene에 남아 다음 프레임에 터진다.
	Scene.RemoveAllLights();
	Scene.RemoveAllDecals();
	Scene.RemoveAllPrimitives();
	Scene.RemoveAllFogInfo();
	HTR_LOG(Info, "{} : ", PersistentLevel->GetActorNum());
}

// 월드 폐기 단계의 소유 자원 최종 해제
void UWorld::CleanupWorld()
{
	// 미초기화 월드와 반복 호출을 고려한 씬 내용 정리
	if (PersistentLevel)
	{
		ClearWorld();
	}

	// 레벨에 포함되지 않는 독립 기본 카메라 해제
	if (MainCamera)
	{
		MainCamera->UnregisterAllComponents();
		MainCamera->RegisterAllActorTickFunctions(false);
		delete MainCamera;
		MainCamera = nullptr;
	}

	// 소유 목록 기준의 레벨 해제와 별칭 초기화
	for (ULevel* Level : Levels)
	{
		delete Level;
	}
	Levels.Reset();
	PersistentLevel = nullptr;
	CurrentLevel = nullptr;
}

// Tick 이후의 Scene 상태를 반영하고 새 캡처 구간의 동적 데이터 준비를 예약한다.
void UWorld::BeginRenderViews()
{
	Scene.UpdateAllTransforms();
	for (FPrimitiveSceneProxy* Proxy : Scene.PrimitiveSceneProxies)
		Proxy->InvalidateDynamicData();
}

// Adapter와 World Gather가 같은 Proxy 수집을 사용하며 Queue에는 추가만 한다.
bool UWorld::GatherDynamicRenderPackets(FPrimitiveSceneProxy& Proxy, FRenderQueue& RenderQueue,
	const FVector& CameraLocation, FBillboardMatrixBuilder ResolveBillboard,
	const void* ViewContext, FRenderQueue* IconQueue)
{
	Proxy.PrepareDynamicData();
	return Proxy.GatherDynamicRenderPackets(RenderQueue, IconQueue, CameraLocation,
		ResolveBillboard, ViewContext, ParticleOrder, ParticleDistances);
}

// TODO : 렌더링 파이프라인 개편해서 이 함수의 책임을 덜어내야함.
// TODO : 언리얼처럼 FSceneRenderer,MeshPassProcessor등의 클래스를 추가해서 해당 클래스로 책임을 덜어낼수있을것으로 보임

// 기존 정적 수집을 유지하고 View 입력이 있으면 동적 Proxy 수집을 연결한다.
void UWorld::GatherRenderPackets(FRenderQueue& RenderQueue, const FLODViewContext* LODView,
	const FFrustumPlanes* Frustum, FRenderer* Renderer, FBillboardMatrixBuilder ResolveBillboard,
	const void* ViewContext, FRenderQueue* IconQueue, FRenderQueue* ArrowQueue)
{
	assert(!ResolveBillboard || LODView != nullptr);
	// 멤버로 두어 매 프레임 용량을 재사용한다.
	RenderStats.Reset();
	VisibleProxies.Reset();

	{
		SCOPE_CYCLE_COUNTER(STAT_FrustumCull);
		// BVH 방문은 호출 스레드에서 동기 실행하므로 에디터 Arrow도 여기서 제출한다.
		const auto Visit = [&](FPrimitiveSceneProxy* Proxy)
		{
			if (ArrowQueue && Proxy->GetMesh())
			{
				UStaticMeshComponent* StaticComponent = Cast<UStaticMeshComponent>(Proxy->GetComponent());
				if (StaticComponent && StaticComponent->IsEditorIcon())
				{
					// TODO: 모든 프리미티브를 공통 큐로 수집하고 렌더 패스로 분류하도록 통합한 뒤 임시 ArrowQueue 분기를 제거한다.
					if (Proxy->IsVisible())
						StaticComponent->SubmitToRenderQueue(*ArrowQueue, 0);
					return;
				}
			}
			// 일반 프리미티브는 기존 병렬·동적 수집 경로에서 처리한다.
			VisibleProxies.Add(Proxy);
		};

		if (Frustum)
		{
			Scene.BVH.QueryCull(
				FrustumAllPlanesMask,
				[Frustum](const FBox& Bounds, uint32& Mask)
				{
					return static_cast<EBVHCullResult>(ClassifyBoxInFrustum(Bounds, *Frustum, Mask));
				},
				Visit);
		}
		else
		{
			Scene.BVH.QueryCull(0, [](const FBox&, uint32&) { return EBVHCullResult::Inside; }, Visit);
		}
	}

	RenderStats.TotalPrimitives = Scene.PrimitiveSceneProxies.Num();

	// GPU 오클루전: 프러스텀을 통과한 물체를 GPU에서 가림 판정하고 결과를 이번 프레임에 받아 온다.
	// Cull이 켜져 있으면 가려진 물체를 목록에서 빼서 이후 Gather·정렬·드로우를 모두 건너뛴다.
	// 꺼져 있으면(검증 모드) 목록은 그대로 두고 패킷에 판정만 표시한다.
	const uint8* OccludedMask = nullptr;
	if (Renderer && LODView && Renderer->GetGPUOcclusion().GetSettings().bEnabled)
	{
		FGPUOcclusion& Occlusion = Renderer->GetGPUOcclusion();
		if (Occlusion.Run(VisibleProxies.GetData(), VisibleProxies.Num(), *LODView))
		{
			const std::vector<uint8>& Occluded = Occlusion.GetOccluded();
			if (Occlusion.GetSettings().bCull)
			{
				uint32 Kept = 0;
				for (uint32 i = 0; i < VisibleProxies.Num(); ++i)
					if (!Occluded[i])
						VisibleProxies[Kept++] = VisibleProxies[i];
				VisibleProxies.SetNum(Kept);
			}
			else
			{
				OccludedMask = Occluded.data();
			}
		}
	}

	RenderStats.VisiblePrimitives = VisibleProxies.Num();

	constexpr uint32 ExtraSlots = 4096;
	const uint32 VisibleCount = VisibleProxies.Num();
	const uint32 MaxSlots = VisibleCount + ExtraSlots;
	uint8* SlotDest = Renderer ? Renderer->BeginObjectConstants(MaxSlots) : nullptr;
	uint32 NextExtraSlot = VisibleCount;

	// 조각 수 = 스레드 수 × 4. 잘게 나눠야 먼저 끝난 스레드가 남은 조각을 가져가서 부하가 고르게 된다.
	FTaskPool& Pool = FTaskPool::Get();
	const uint32 ChunkCount = FMath::Clamp(VisibleCount, 1u, Pool.GetNumThreads() * 4);

	if (GatherChunks.Num() < ChunkCount)
		GatherChunks.SetNum(ChunkCount);     // 늘릴 때만. 줄이지 않아야 배열 용량이 계속 재사용된다.

	for (uint32 c = 0; c < GatherChunks.Num(); ++c)   // 이번에 안 쓰는 조각의 묶음도 비워 둔다 (Renderer에 넘기지 않도록)
	{
		FGatherChunk& Chunk = GatherChunks[c];
		Chunk.Packets.Reset();               // 용량은 유지, 개수만 0
		Chunk.SlowPathIndices.Reset();
		std::fill(std::begin(Chunk.LODCounts), std::end(Chunk.LODCounts), 0u);
		std::fill(std::begin(Chunk.LODTriangles), std::end(Chunk.LODTriangles), 0ull);
		for (FStaticDrawGroup& Group : Chunk.Groups)
			Group.Items.clear();             // 용량은 유지
		Chunk.StaticDrawCount = 0;
	}

	// Renderer가 있으면 불투명 스태틱 메시는 패킷 대신 조각별 묶음에 작은 항목으로 넣는다.
	// 동일 바인딩의 개별 드로우를 묶어 패킷 복사와 항목별 정렬을 줄인다.
	const bool bStaticGroups = Renderer != nullptr;
	if (Renderer)
		Renderer->ResetStaticDrawGroups();

	{
		SCOPE_CYCLE_COUNTER(STAT_GatherElements);

		LODInputs.Reset();
		if (LODView)
		{
			LODInputs.Reserve(VisibleProxies.Num());
			for (const FPrimitiveSceneProxy* Proxy : VisibleProxies)
				LODInputs.Add({ Proxy->GetLODSphere(), Proxy->GetRenderState() });
			SelectLODs(LODInputs, *LODView, SelectedLODs);
		}
		Pool.ParallelFor(VisibleCount, ChunkCount, [&](uint32 Begin, uint32 End, uint32 ChunkIndex)
			{
				FGatherChunk& Out = GatherChunks[ChunkIndex];      // 이 조각 전용. 다른 스레드는 절대 안 건드림
				// (머티리얼, 메시, LOD) 묶음 찾기. 조합이 몇 개뿐이라 선형 탐색이면 충분하고, 바로 전 묶음을 먼저 본다.
				const auto FindGroup = [&Out](UMaterial* Material, UStaticMesh* Mesh, uint8 LOD) -> FStaticDrawGroup&
					{
						if (Out.LastGroup < Out.Groups.size())
						{
							FStaticDrawGroup& Last = Out.Groups[Out.LastGroup];
							if (Last.Material == Material && Last.Mesh == Mesh && Last.LODIndex == LOD)
								return Last;
						}
						for (uint32 g = 0; g < Out.Groups.size(); ++g)
						{
							FStaticDrawGroup& Group = Out.Groups[g];
							if (Group.Material == Material && Group.Mesh == Mesh && Group.LODIndex == LOD)
							{
								Out.LastGroup = g;
								return Group;
							}
						}
						Out.LastGroup = static_cast<uint32>(Out.Groups.size());
						FStaticDrawGroup& Group = Out.Groups.emplace_back();
						Group.Material = Material;
						Group.Mesh = Mesh;
						Group.LODIndex = LOD;
						return Group;
					};

				for (uint32 VisibleIndex = Begin; VisibleIndex < End; ++VisibleIndex)
				{
					FPrimitiveSceneProxy* Proxy = VisibleProxies[VisibleIndex];   // 읽기만
					if (!Proxy->IsVisible()) continue;

					UStaticMesh* Mesh = Proxy->GetMesh();
					if (!Mesh)
					{
						Out.SlowPathIndices.Add(VisibleIndex);     // 컴포넌트를 건드리는 경로는 메인이 나중에
						continue;
					}

					const uint32 LOD = LODView ? SelectedLODs[VisibleIndex] : 0;
					const FCachedMeshLOD& CachedLOD = Proxy->GetLOD(LOD);
					++Out.LODCounts[LOD];                          // RenderStats 대신 조각 전용 통계

					uint32 Slot = InvalidObjectSlot;
					if (SlotDest)
					{
						// 칸 VisibleIndex는 이 반복만 쓴다 → 스레드끼리 겹치지 않음
						WritePerObjectConstants(SlotDest + size_t(VisibleIndex) * ObjectSlotBytes,
							Proxy->GetLocalToWorld(), Proxy->GetWorldToLocal(), Proxy->IsShadowReceiver(), Proxy->IsDecalReceiver());
						Slot = VisibleIndex;
					}

					const bool bOccludedByGpu = OccludedMask && OccludedMask[VisibleIndex];
					for (uint32 i = 0; i < CachedLOD.NumSections; ++i)
					{
						const FCachedMeshSection& Section = Proxy->GetSection(CachedLOD.FirstSection + i);
						Out.LODTriangles[LOD] += Section.IndexCount / 3;

						if (bStaticGroups && Section.Material && Section.Material->BlendState == EBlendState::Opaque)
						{
							FStaticDrawGroup& Group = FindGroup(Section.Material, Mesh, static_cast<uint8>(LOD));
							Group.Items.push_back({ Proxy, Slot, Section.StartIndex, Section.IndexCount, bOccludedByGpu ? 1u : 0u });
							++Out.StaticDrawCount;
							continue;
						}

						FRenderPacket& Packet = Out.Packets.AddDefaulted_GetRef();   // 조각 전용 배열에 추가
						Packet.Proxy = Proxy;
						Packet.Mesh = Mesh;
						Packet.Material = Section.Material;
						Packet.StartIndex = Section.StartIndex;
						Packet.IndexCount = Section.IndexCount;
						Packet.LODIndex = static_cast<uint8>(LOD);
						Packet.Slot = Slot;
						Packet.bOccludedByGpu = bOccludedByGpu;
					}
				}
			});

		// 묶음은 World 메모리 그대로 Renderer에 넘긴다 (복사 없음). 정렬은 Renderer가 묶음 단위로 한다.
		uint32 StaticDraws = 0;
		if (Renderer)
		{
			for (uint32 c = 0; c < ChunkCount; ++c)
			{
				StaticDraws += GatherChunks[c].StaticDrawCount;
				for (const FStaticDrawGroup& Group : GatherChunks[c].Groups)
					if (!Group.Items.empty())
						Renderer->AddStaticDrawGroup(&Group);
			}
		}

		uint32 TotalPackets = 0;
		for (uint32 c = 0; c < ChunkCount; ++c)
			TotalPackets += GatherChunks[c].Packets.Num();

		RenderQueue.Reserve(TotalPackets + 256);             // 느린 경로 몫 약간 여유
		for (uint32 c = 0; c < ChunkCount; ++c)
			if (GatherChunks[c].Packets.Num() > 0)
				RenderQueue.Append(GatherChunks[c].Packets);  // 조각 순서대로 이어 붙이기

		for (uint32 c = 0; c < ChunkCount; ++c)
		{
			for (uint32 VisibleIndex : GatherChunks[c].SlowPathIndices)
			{
				FPrimitiveSceneProxy* Proxy = VisibleProxies[VisibleIndex];
				UPrimitiveComponent* Primitive = Proxy->GetComponent();
				if (!Primitive || !Primitive->IsVisible())
					continue;

				const uint32 FirstNew = RenderQueue.Num();

				// 동적 수집은 병렬 정적 경로가 끝난 뒤 메인 스레드에서만 실행한다.
				const bool bHandled = ResolveBillboard && GatherDynamicRenderPackets(*Proxy,
					RenderQueue, LODView->CameraPosition, ResolveBillboard, ViewContext, IconQueue);
				if (!bHandled)
				{
					// 기본 호출과 미지원 Proxy는 기존 Component 제출 경로를 유지한다.
					UStaticMeshComponent* StaticMeshComponent = LODView ? Cast<UStaticMeshComponent>(Primitive) : nullptr;
					if (StaticMeshComponent && StaticMeshComponent->GetStaticMesh())
					{
						const uint32 LOD = SelectedLODs[VisibleIndex];
						StaticMeshComponent->SubmitToRenderQueue(RenderQueue, LOD);
					}
					else
					{
						Primitive->SubmitToRenderQueue(RenderQueue);
					}
				}

				// continue 없이 항상 여기까지 와서 새 패킷에 여유 칸을 배정한다.
				for (uint32 p = FirstNew; p < RenderQueue.Num(); ++p)
				{
					FRenderPacket& Packet = RenderQueue[p];

					// Sprite는 인스턴스 데이터와 뷰 상수만 사용한다.
					if (Packet.IsSpriteParticle())
					{
						continue;
					}

					if (!SlotDest || NextExtraSlot >= MaxSlots) break;
					const FMatrix& Model = Packet.Proxy ? Packet.Proxy->GetLocalToWorld() : Packet.Model ? *Packet.Model : FMatrix::Identity;
					const FMatrix WorldToLocal = Packet.Proxy ? Packet.Proxy->GetWorldToLocal() : Model.Inverse();
					WritePerObjectConstants(SlotDest + size_t(NextExtraSlot) * ObjectSlotBytes,
						Model, WorldToLocal, Packet.Proxy&& Packet.Proxy->IsShadowReceiver(),
						Packet.Proxy&& Packet.Proxy->IsDecalReceiver());
					Packet.Slot = NextExtraSlot++;
				}
			}
		}

		for (uint32 c = 0; c < ChunkCount; ++c)
			for (uint32 L = 0; L < 4; ++L)
			{
				RenderStats.LODCounts[L] += GatherChunks[c].LODCounts[L];
				RenderStats.LODTriangles[L] += GatherChunks[c].LODTriangles[L];
			}

		if (SlotDest) Renderer->EndObjectConstants();
		RenderStats.DrawCalls = RenderQueue.Num() + StaticDraws;
		for (uint64 T : RenderStats.LODTriangles) RenderStats.Triangles += T;
	}
}

// 지역광 그림자는 같은 World의 여러 View가 공유한다.
void UWorld::GatherLocalProjectedShadows(TArray<FProjectedShadowInfo>& OutShadows,
	const TArray<FFrustumPlanes>& ActiveViewFrustums,
	const TArray<FLODViewContext>& ActiveViews, float MinScreenRadiusSquared)
{
	for (const FLocalLightSceneProxy* LightProxy : Scene.LocalLightProxies)
	{
		const ELightType LightType = LightProxy->GetLightComponent()->GetLightType();
		if ((LightType != ELightType::Point && LightType != ELightType::Spot)||!LightProxy->ShouldCastShadow())
		{
			continue;
		}
		FLightRenderParameters LightParameters{};
		LightProxy->GetLightShaderParameters(LightParameters);

		// 같은 World의 어느 활성 그림자 View에라도 영향이 있으면 유지한다.
		bool bAffectsAnyView = false;
		for (const FFrustumPlanes& ViewFrustum : ActiveViewFrustums)
		{
			if (IsSphereInFrustum(LightParameters.Position, LightParameters.Radius, ViewFrustum))
			{
				bAffectsAnyView = true;
				break;
			}
		}
		if (!bAffectsAnyView)
		{
			FStatRegistry::AddCounter(ShadowStats::STAT_LocalLightsOutsideViews);
			continue;
		}
		const bool bSpotLight = LightType == ELightType::Spot;
		FShadowProjection ShadowProjection;
		FFrustumPlanes SpotFrustum{};
		const FFrustumPlanes* ReceiverFrustum = nullptr;

		if (bSpotLight)
		{
			// 유효성 검증은 기존 라이트별 투영 계산 함수가 담당한다.
			if (!LightProxy->GetShadowProjection(ShadowProjection))
				continue;

			// 같은 여섯 평면을 Receiver 검사와 Caster 수집에 재사용한다.
			SpotFrustum = ExtractFrustumPlanes(ShadowProjection.ViewProjections[0]);
			ReceiverFrustum = &SpotFrustum;
		}

		// Point는 Receiver가 없으면 여섯 VP 계산도 생략한다.
		if (!HasVisibleShadowReceiver(ActiveViewFrustums, &LightParameters, ReceiverFrustum))
		{
			FStatRegistry::AddCounter(ShadowStats::STAT_LocalLightsWithoutReceivers);
			continue;
		}

		if (!bSpotLight && !LightProxy->GetShadowProjection(ShadowProjection))
		{
			continue;
		}

		const float CasterSphereRadius = GetLocalShadowCasterRadius(*LightProxy->GetLightComponent(), LightParameters, ShadowProjection.TanHalfFOV);
		// 행렬의 평면 계산 오차만 흡수하며, 그림자 Bias나 필터 폭을 바꾸지 않는다.
		const float ReceiverGuard = std::max(0.001f, 8.0f * std::numeric_limits<float>::epsilon()
			* (LightParameters.Position.Size() + LightParameters.Radius));

		uint32 ProjectionIndex = 0;
		for (const FMatrix& ViewProjection : ShadowProjection.ViewProjections)
		{
			const FFrustumPlanes ProjectionFrustum = bSpotLight ? SpotFrustum : ExtractFrustumPlanes(ViewProjection);

			FProjectedShadowInfo& ShadowInfo = OutShadows.AddDefaulted_GetRef();
			ShadowInfo.SetupProjection(*LightProxy, ShadowProjection.ViewOrigin,
				ViewProjection, ProjectionFrustum, FrustumAllPlanesMask, ProjectionIndex);

			// Point의 면 선택은 Normal Offset 전 WorldPos의 최대 절댓값 축으로 결정된다.
			// 옆면 4개만 검사해, Offset 후 유효해지는 Near/Far 경계 Receiver도 보존한다.
			FFrustumPlanes FaceReceiverFrustum = ProjectionFrustum;
			for (int32 PlaneIndex = 0; PlaneIndex < 4; ++PlaneIndex)
				FaceReceiverFrustum.Planes[PlaneIndex].Distance += ReceiverGuard;
			const uint32 FaceReceiverMask = FrustumAllPlanesMask & ~((1u << 4) | (1u << 5));
			// Spot은 위의 동일 투영 검사 결과를 재사용하고, Point는 모든 활성 View를 확인한다.
			ShadowInfo.SetHasReceivers(bSpotLight || HasVisibleShadowReceiver(
				ActiveViewFrustums, &LightParameters, &FaceReceiverFrustum, FaceReceiverMask));
			if (ShadowInfo.HasReceivers())
			{
				GatherShadowCasters(ShadowInfo, ActiveViews.GetData(), ActiveViews.Num(),
					MinScreenRadiusSquared, &LightParameters, CasterSphereRadius);
			}
			// 생략한 면도 논리 슬롯을 남겨 ShadowIndex + FaceIndex 연결을 유지한다.
			++ProjectionIndex;
		}
	}
}

// 하나의 Receiver 구간마다 독립적인 Directional 작업을 추가한다.
void UWorld::GatherDirectionalShadow(TArray<FProjectedShadowInfo>& OutShadows,
	const TArray<FVector>& ReceiverCorners, const FLODViewContext& View,
	float ReceiverNearDistance, float ReceiverFarDistance,
	float MinScreenRadiusSquared, uint32 ProjectionIndex, uint32 ViewId,
	uint32 LogicalCascadeCount)
{
	// 화면 조명과 동일한 Directional 하나를 사용한다.
	const FDirectionalLightSceneProxy* Directional = Scene.GetPrimaryDirectionalLight();
	if (!Directional)
		return;

	FProjectedShadowInfo ShadowInfo;
	if (!Directional->SetupShadowProjection(
		ReceiverCorners, ShadowInfo, ProjectionIndex, ViewId))
		return;

	// 투영 초기화가 끝난 뒤 실제 View의 논리 분할 개수를 기록한다.
	ShadowInfo.SetLogicalCascadeCount(LogicalCascadeCount);

	// Caster의 열린 광원 Frustum과 분리해, 실제 카메라의 Receiver 구간을 검사한다.
	FFrustumPlanes ReceiverFrustum = ExtractFrustumPlanes(View.ViewProjection);
	const float CameraDepth = FVector::Dot(View.CameraPosition, View.CameraForward);
	const float ReceiverGuard = std::max(0.001f, 8.0f * std::numeric_limits<float>::epsilon()
		* (View.CameraPosition.Size() + std::max(std::abs(ReceiverNearDistance), std::abs(ReceiverFarDistance))));
	// 입력 Near는 이전 Cascade의 보간 구간까지 포함한 RenderNear다.
	ReceiverFrustum.Planes[4] = { View.CameraForward, -CameraDepth - ReceiverNearDistance + ReceiverGuard };
	ReceiverFrustum.Planes[5] = { -View.CameraForward, CameraDepth + ReceiverFarDistance + ReceiverGuard };
	TArray<FFrustumPlanes> ReceiverFrustums;
	ReceiverFrustums.Add(ReceiverFrustum);
	ShadowInfo.SetHasReceivers(HasVisibleShadowReceiver(ReceiverFrustums));

	// Receiver가 있어야 Caster를 수집하되, 생략된 Cascade의 번호도 그대로 보존한다.
	if (ShadowInfo.HasReceivers())
		GatherShadowCasters(ShadowInfo, &View, 1, MinScreenRadiusSquared);
	OutShadows.Add(std::move(ShadowInfo));
}

//void UWorld::GatherRenderPackets(TArray<FRenderPacket>& RenderArray, const FLODViewContext* LODView, const FFrustumPlanes* Frustum)
//{
//	for (TObjectIterator<UPrimitiveComponent> Itr; Itr; ++Itr)
//	{
//		if (!*Itr || !Itr->IsVisible())
//			continue;
//
//		if (Frustum)
//		{
//			const FBox Box = Itr->CalcBounds();
//			const FAABB Bounds{
//				(Box.Min + Box.Max) * 0.5f,
//				(Box.Max - Box.Min) * 0.5f
//			};
//			if (!IsAABBInFrustum(Bounds, *Frustum))
//				continue;
//		}
//
//		if (LODView)
//		{
//			if (auto* Component = Cast<UStaticMeshComponent>(*Itr))
//			{
//				if (UStaticMesh* Mesh =
//					Component->GetStaticMesh())
//				{
//					const uint32 LOD = SelectStaticMeshLOD(
//						*Mesh,
//						Component->GetWorldMatrix(),
//						*LODView);
//
//					Component->SubmitToRenderQueue(RenderQueue, LOD);
//					continue;
//				}
//			}
//		}
//
//		Itr->SubmitToRenderQueue(RenderQueue);
//	}
//}


// Bounds의 투영 반경을 근사하며 카메라 Frustum 가시성과는 별도로 판단한다.
bool UWorld::IsSmallShadowCaster(const FLODSphere& Sphere, const FLODViewContext* Views, int32 ViewCount, float MinScreenRadiusSquared)
{
	// 비활성 설정이나 사용할 수 없는 구 Bounds는 기존 제출 경로로 보낸다.
	if (MinScreenRadiusSquared <= 0.0f || Sphere.RadiusSquared <= 0.0f)
		return false;

	for (int32 ViewIndex = 0; ViewIndex < ViewCount; ++ViewIndex)
	{
		const FLODViewContext& View = Views[ViewIndex];

		// 기존 NDC 배율을 정규화된 화면 반경 제곱으로 변환한다.
		const float ProjectedRadiusSquared =
			0.25f * Sphere.RadiusSquared * View.ProjectionScaleSquared;

		float DistanceFactor = 1.0f;
		if (!View.bOrthographic)
		{
			DistanceFactor = (Sphere.Center - View.CameraPosition).SizeSquared();

			// 카메라가 Bounds 내부에 있으면 작은 물체로 생략하지 않는다.
			if (DistanceFactor <= Sphere.RadiusSquared)
				return false;
		}

		// 어느 View 하나라도 충분히 크면 공유 그림자에 남긴다.
		if (ProjectedRadiusSquared >= MinScreenRadiusSquared * DistanceFactor)
			return false;
	}

	// View가 없는 호출에서도 보수적으로 기존 제출을 유지한다.
	return ViewCount > 0;
}


float UWorld::GetLocalShadowCasterRadius(const ULightComponent& Light, const FLightRenderParameters& Parameters, float TanHalfFOV)
{
	const int32 Resolution = FShadowSettings::GetEffectiveResolution(Light.GetShadowMapResolution());
	if (Resolution <= 0)
		return 0.0f;

	const double Range = Parameters.Radius;
	const double Tan = TanHalfFOV;
	const double ProjectionRadius = Range * std::sqrt(1.0 + 2.0 * Tan * Tan);

	// 한 텍셀의 횡방향 여유와 작은 월드 공간 여유를 더한다.
	const double RasterGuard = 2.0 * 1.41421356 * Tan * Range / Resolution;
	const double Radius = ProjectionRadius + RasterGuard + 0.001;

	if (!std::isfinite(Radius) || Radius > std::numeric_limits<float>::max())
		return 0.0f;

	const float Result = std::nextafter(static_cast<float>(Radius), std::numeric_limits<float>::infinity());
	return std::isfinite(Result) ? Result : 0.0f;
}

// Light Frustum으로 후보를 찾고 기존 정적 메시 제출 경로를 재사용한다.
// 투영·타깃 상태는 유지하며 Caster 큐에 패킷만 추가한다.
void UWorld::GatherShadowCasters(FProjectedShadowInfo& ShadowInfo, const FLODViewContext* Views, int32 ViewCount, 
	float MinScreenRadiusSquared, const FLightRenderParameters* LocalLight, float CasterSphereRadius)
{
	// 한 Gather 안에서 여러 물체의 제출에 같은 임시 큐를 재사용한다.
	FRenderQueue MeshPackets;
	uint64 SmallCasterSkips = 0;
	uint64 SphereCasterSkips = 0;

	// QueryCull에서 처리할 함수
	const auto Visit = [&ShadowInfo, &MeshPackets, Views, ViewCount,
		MinScreenRadiusSquared, &SmallCasterSkips, LocalLight,
		CasterSphereRadius, &SphereCasterSkips](FPrimitiveSceneProxy* Proxy)
		{
			// 그림자 생성 여부만 확인한다. ReceiveShadow는 Caster 선별과 무관하다.
			if (!Proxy->IsVisible() || !Proxy->IsShadowCaster())
				return;

			UPrimitiveComponent* CasterComponent = Proxy->GetComponent();
			auto* StaticComponent = Cast<UStaticMeshComponent>(CasterComponent);
			auto* SkeletalComponent = StaticComponent ? nullptr : Cast<USkeletalMeshComponent>(CasterComponent);
			if (!StaticComponent && !SkeletalComponent)
				return;
			if (StaticComponent && StaticComponent->IsEditorIcon())
				return;

			// BVH가 Frustum 내부 가지를 일괄 방문해도 이 검사는 수행한다.
			if (LocalLight && CasterSphereRadius > 0.0f
				&& !IsAABBInSphere(Proxy->GetBounds(), LocalLight->Position, CasterSphereRadius))
			{
				++SphereCasterSkips;
				return;
			}

			// 월드 Sphere를 이용, 섹션마다 반복검사하지 않는다.
			if (IsSmallShadowCaster(Proxy->GetLODSphere(), Views, ViewCount, MinScreenRadiusSquared))
			{
				++SmallCasterSkips;
				return;
			}

			// 메시·Proxy·RenderState 검사는 기존 Submit 함수가 담당한다.
			// 기초 그림자는 카메라와 독립적으로 LOD0를 사용한다.
			MeshPackets.Reset();
			if (StaticComponent)
				StaticComponent->SubmitToRenderQueue(MeshPackets, 0);
			else
				SkeletalComponent->SubmitToRenderQueue(MeshPackets);

			bool bPacketAdded = false;
			for (const FRenderPacket& Packet : MeshPackets)
			{
				// 불투명 섹션만 그림자 작업에 등록한다.
				// IndexCount 0은 Renderer에서 전체 인덱스 버퍼를 뜻하므로 제외한다.
				if (!Packet.Material || Packet.Material->BlendState != EBlendState::Opaque || Packet.IndexCount == 0)
					continue;

				ShadowInfo.AddCasterPacket(Packet);
				bPacketAdded = true;
			}

			// 헤싱으로 Shadow Caching 키를 만든다
			if (bPacketAdded)
			{
				uint32 UUID = CasterComponent->GetUUID();
				uint64 Version = Proxy->GetShadowVersion();
				ShadowInfo.AddCasterKey(UUID, Version);
			}
		};


	// 전체 Scene BVH를 Light Frustum으로 탐색한다.
	const FFrustumPlanes& Frustum = ShadowInfo.GetCasterFrustum();
	Scene.BVH.QueryCull(
		ShadowInfo.GetCasterPlaneMask(),
		[&Frustum](const FBox& Bounds, uint32& Mask)
		{
			return static_cast<EBVHCullResult>(ClassifyBoxInFrustum(Bounds, Frustum, Mask));
		},
		Visit);
	if (SmallCasterSkips > 0)
		FStatRegistry::AddCounter(ShadowStats::STAT_SmallCasterSkips, SmallCasterSkips);
	if (SphereCasterSkips > 0)
		FStatRegistry::AddCounter(ShadowStats::STAT_CasterSphereSkips, SphereCasterSkips);
}

// 메인 카메라 생성
void UWorld::CreateMainCamera()
{
	if (MainCamera)
		return;

	MainCamera = NewObject<ACameraActor>();

	if (!MainCamera)
	{
		HTR_LOG(Error, "Failed to create MainCamera");
		return;
	}

	MainCamera->World = this;
	MainCamera->Level = nullptr;
	MainCamera->GetCameraComponent()->SetRelativeLocation(FVector(-5.0f, -5.0f, 5.0f));

	// 메인 카메라는 Level에 속하지 않아 BeginPlay를 거치지 않으므로 여기서 등록한다.
	MainCamera->RegisterAllActorTickFunctions(true);
}

void UWorld::SetMainCamera(ACameraActor* Camera)
{
	if (MainCamera == Camera)
		return;

	if (MainCamera)
		MainCamera->RegisterAllActorTickFunctions(false);

	MainCamera = Camera;

	if (MainCamera)
	{
		MainCamera->World = this;
		MainCamera->RegisterAllActorTickFunctions(true);
	}
}

UCameraComponent* UWorld::GetMainCameraComponent() {
	if (WorldType == EWorldType::PIE || WorldType == EWorldType::Game) {
		if (!mPlayCameraComponent) {
			InitializePlayCamera();
		}
		return mPlayCameraComponent;
	}

	return MainCamera ? MainCamera->GetCameraComponent() : nullptr;
}

void UWorld::ResetMainCameraComponent(UCameraComponent* Camera) {
	if (mPlayerCameraManager) {
		mPlayerCameraManager->ResetViewTarget(Camera);
	}
	if (mPlayCameraComponent == Camera) {
		mPlayCameraComponent = nullptr;
	}
}

APlayerCameraManager* UWorld::GetPlayerCameraManager() {
	if (WorldType != EWorldType::PIE && WorldType != EWorldType::Game) {
		return nullptr;
	}
	if (mPlayerCameraManager && mPlayerCameraManager->IsActorBeingDestroyed()) {
		mPlayerCameraManager = nullptr;
	}
	if (!mPlayerCameraManager) {
		for (AActor* Actor : PersistentLevel->GetActors()) {
			if (APlayerCameraManager* CameraManager{ Cast<APlayerCameraManager>(Actor) }; CameraManager && !CameraManager->IsActorBeingDestroyed()) {
				mPlayerCameraManager = CameraManager;
				break;
			}
		}
	}
	if (!mPlayerCameraManager) {
		mPlayerCameraManager = SpawnActor<APlayerCameraManager>();
		if (!mPlayerCameraManager) {
			return nullptr;
		}
		mPlayerCameraManager->SetFlags(EObjectFlags::RF_Transient);
	}
	if (!mPlayerCameraManager->GetViewTargetCamera()) {
		mPlayerCameraManager->SetViewTargetCamera(GetMainCameraComponent());
	}
	return mPlayerCameraManager;
}

void UWorld::InitializePlayCamera() {
	if (MainCamera) {
		MainCamera->GetCameraComponent()->SetExternalInputManaged(true);
	}

	for (ULevel* Level : Levels) {
		if (!Level) {
			continue;
		}

		for (AActor* Actor : Level->GetActors()) {
			if (!Actor) {
				continue;
			}

			for (UActorComponent* Component : Actor->GetComponents()) {
				UCameraComponent* Camera{ Cast<UCameraComponent>(Component) };
				if (!Camera || !Camera->IsRegistered()) {
					continue;
				}

				Camera->SetExternalInputManaged(true);
				if (!mPlayCameraComponent) {
					mPlayCameraComponent = Camera;
				}
			}
		}
	}

	if (!mPlayCameraComponent) {
		const FTransform CameraTransform{ MainCamera ? MainCamera->GetActorTransform() : FTransform::Identity };
		ACameraActor* CameraActor{ Cast<ACameraActor>(SpawnActor(ACameraActor::StaticClass(), NAME_None, &CameraTransform)) };
		if (CameraActor) {
			mPlayCameraComponent = CameraActor->GetCameraComponent();
			if (MainCamera) {
				const UCameraComponent* DefaultCamera{ MainCamera->GetCameraComponent() };
				mPlayCameraComponent->SetFieldOfView(DefaultCamera->GetFieldOfView());
				mPlayCameraComponent->SetAspectRatio(DefaultCamera->GetAspectRatio());
				mPlayCameraComponent->SetNearZ(DefaultCamera->GetNearZ());
				mPlayCameraComponent->SetFarZ(DefaultCamera->GetFarZ());
				mPlayCameraComponent->SetIsOrthogonal(DefaultCamera->GetIsOrthogonal());
				mPlayCameraComponent->SetOrthoWidth(DefaultCamera->GetOrthoWidth());
			}
			mPlayCameraComponent->SetExternalInputManaged(true);
		}
	}
}

int32 UWorld::GetActorNum()
{
	return PersistentLevel->GetActorNum();
}

bool UWorld::DestroyActor(AActor* Actor)
{
	if (!Actor || !Actor->GetLevel())
		return false;

	if (Actor->IsActorBeingDestroyed())
		return true;

	Actor->bActorIsBeingDestroyed = true;

	if (bInGameTick || bEndingPlay)
	{
		PendingDestroyActors.Add(Actor);
		return true;
	}

	return DestroyActorNow(Actor);
}

void UWorld::FlushPendingDestroyActors()
{
	const TArray<AActor*> Actors = PendingDestroyActors;
	PendingDestroyActors.Reset();

	for (AActor* Actor : Actors)
		DestroyActorNow(Actor);
}

void UWorld::RequestDestroyComponent(UActorComponent* Component)
{
	if (!Component || !Component->GetOwner())
		return;

	if (bInGameTick)
	{
		PendingDestroyComponents.Add(Component);
		return;
	}

	Component->GetOwner()->DestroyComponent(Component);
}

void UWorld::FlushPendingDestroyComponents()
{ 
	const TArray<UActorComponent*> Components = PendingDestroyComponents;
	PendingDestroyComponents.Reset();

	for (UActorComponent* ActorComponent : Components)
		ActorComponent->GetOwner()->DestroyComponent(ActorComponent);
}

bool UWorld::DestroyActorNow(AActor* Actor) {
	ULevel* Level{ Actor->GetLevel() };
	if (Actor == mPlayerCameraManager) {
		mPlayerCameraManager = nullptr;
	}

	// 게임 중이던 액터면 EndPlay부터 알린다. 이 시점엔 아직 월드와 컴포넌트에 접근할 수 있다.
	if (!bEndingPlay || Actor->HasActorBegunPlay())
	{
		Actor->EndPlay();
	}

	TArray<AActor*> AttachedActors;
	Actor->GetAttachedActors(AttachedActors);
	for (AActor* AttachedActor : AttachedActors)
		AttachedActor->DetachFromActor(EAttachmentRule::KeepWorld);

	// 1. BeginPlay 대기열에서 제거
	TQueue<AActor*> NewBeginPlayList;

	while (!BeginPlayList.IsEmpty())
	{
		AActor* PendingActor = BeginPlayList.Peek();
		BeginPlayList.Dequeue();

		if (PendingActor != Actor)
		{
			NewBeginPlayList.Enqueue(PendingActor);
		}
	}

	BeginPlayList = std::move(NewBeginPlayList);

	// 액터가 바로 지워지면 대기 중이던 소유 컴포넌트 포인터가 남으므로 함께 뺀다.
	for (int32 Index = PendingDestroyComponents.Num() - 1; Index >= 0; --Index)
	{
		if (PendingDestroyComponents[Index]->GetOwner() == Actor)
			PendingDestroyComponents.RemoveAtSwap(Index);
	}

	// 4. Level의 Actors에서 제거
	for (int32 i = Level->Actors.Num() - 1; i >= 0; --i)
	{
		if (Level->Actors[i] == Actor)
		{
			Level->Actors.RemoveAt(i, 1);
			break;
		}
	}

	FString ActorName = Actor->GetName();
	uint32 ActorUUID = Actor->GetUUID();

	// 5. 프록시 제거
	Actor->UnregisterAllComponents();

	Actor->RegisterAllActorTickFunctions(false);

	// 6. Actor 삭제
	delete Actor;

	HTR_LOG(Info, "Destroy Actor : {} UUID {}", ActorName, ActorUUID);

	return true;
}

// 다른 World의 객체를 제외하고 Component 교차 중 최근접 결과를 선택한다.
bool UWorld::LineTraceSingle(const FRay& WorldRay, FHitResult& OutHit, FBillboardTraceTransform ResolveBillboard, const void* ViewContext, const AActor* IgnoreActor) {
	SCOPE_CYCLE_COUNTER_ALWAYS(EditorStats::STAT_PickingTime_Name);
	OutHit = FHitResult();
	float NearestT = std::numeric_limits<float>::max();

	const auto TraceComponent = [&](FPrimitiveSceneProxy* Proxy, float& InOutNearestT)
		{
			const AActor* Owner{ Proxy && Proxy->GetComponent() ? Proxy->GetComponent()->GetOwner() : nullptr };
			while (Owner && IgnoreActor) {
				if (Owner == IgnoreActor) {
					return false;
				}
				Owner = Owner->GetAttachParentActor();
			}

			if (UStaticMesh* Mesh = Proxy ? Proxy->GetMesh() : nullptr)
			{
				if (!Proxy->IsVisible())
					return false;

				const FMatrix& WorldToLocal = Proxy->GetWorldToLocal();
				const FRay LocalRay{
					.Origin = WorldToLocal.TransformPosition(WorldRay.Origin),
					.Direction = WorldToLocal.TransformVector(WorldRay.Direction)
				};

				float T = InOutNearestT;
				if (!RayIntersectsMesh(LocalRay, Mesh->GetMeshData(), T))
					return false;

				OutHit.HitComponent = Proxy->GetComponent();
				OutHit.Distance = T;
				OutHit.ImpactPoint = WorldRay.Origin + WorldRay.Direction * T;
				InOutNearestT = T;
				return true;
			}

			UPrimitiveComponent* Component = Proxy ? Proxy->GetComponent() : nullptr;

			if (!Component || !Component->IsVisible())
				return false;

			if (UBillboardComponent* Billboard = Cast<UBillboardComponent>(Component))
			{
				if (!ResolveBillboard)
				{
					FHitResult Hit;
					if (!Billboard->LineTraceComponent(WorldRay, Hit) ||
						Hit.Distance >= InOutNearestT)
					{
						return false;
					}

					OutHit = Hit;
					InOutNearestT = Hit.Distance;
					return true;
				}

				const FMatrix BillboardToWorld = ResolveBillboard(*Billboard, ViewContext);

				const FRay LocalRay = ToLocalRay(WorldRay, BillboardToWorld);

				float T = InOutNearestT;
				if (!Billboard->LineTraceComponentLocal(LocalRay, T))
				{
					return false;
				}

				OutHit.HitComponent = Billboard;
				OutHit.Distance = T;
				OutHit.ImpactPoint = WorldRay.Origin + WorldRay.Direction * T;
				InOutNearestT = T;
				return true;
			}

			const FMatrix& WorldToLocal = Proxy->GetWorldToLocal();
			const FRay LocalRay{
				.Origin = WorldToLocal.TransformPosition(WorldRay.Origin),
				.Direction = WorldToLocal.TransformVector(WorldRay.Direction)
			};

			float T = InOutNearestT;
			if (!Component->LineTraceComponentLocal(LocalRay, T))
			{
				return false;
			}

			OutHit.HitComponent = Component;
			OutHit.Distance = T;
			OutHit.ImpactPoint = WorldRay.Origin + WorldRay.Direction * T;
			InOutNearestT = T;
			return true;
		};

	const FPreparedRay PreparedRay(WorldRay);

	Scene.BVH.TraceClosest(
		[&](const FBox& Bounds, float& OutEnterT) { return RayIntersectsAABB(PreparedRay, Bounds.Min, Bounds.Max, OutEnterT); },
		[&](FPrimitiveSceneProxy* Proxy, float& OutNearestT) { return TraceComponent(Proxy, OutNearestT); },
		NearestT);

	return OutHit.HitComponent != nullptr;
}

void UWorld::BeginPlay()
{
}

void UWorld::EndPlay() {
	if (bEndingPlay) return;
	bEndingPlay = true;
	for (ULevel* Level : Levels)
	{
		// EndPlay에서 액터가 지워질 수 있으므로 목록을 복사해서 돈다.
		const TArray<AActor*> Actors = Level->GetActors();
		for (AActor* Actor : Actors)
		{
			if (Actor)
				Actor->EndPlay();
		}
	}

	if (GEngine && GEngine->GetAudioSystem())
	{
		GEngine->GetAudioSystem()->StopAll();
	}
	// 현재 단일 게임 월드의 대기가 다음 Play까지 남지 않도록 정리한다.

	if (WorldType != EWorldType::Editor)
	{
		if (FLuaVM* LuaVM = GEngine ? GEngine->GetLuaVM() : nullptr)
			LuaVM->ResetCoroutines();
	}

	// 장면 전환이나 PIE 재시작에 이전 장면의 슬로모가 남지 않게 한다.
	ResetTimeEffects();
	ResetVignette();
	GameTime = FGameTime{};

	if (mPlayerCameraManager) {
		mPlayerCameraManager->RemoveAllCameraModifiers();
		mPlayerCameraManager->SetViewTarget(nullptr);
	}
	while (!PendingDestroyActors.IsEmpty())
	{
		FlushPendingDestroyActors();
	}
	bEndingPlay = false;
}

UWorld* UWorld::CreateWorld(const EWorldType InWorldType, bool bInformEngineOfWorld, FName WorldName)
{
	UWorld* NewWorld = NewObject<UWorld>(nullptr, WorldName);
	NewWorld->SetFlags(EObjectFlags::RF_Transactional);
	NewWorld->WorldType = InWorldType;
	NewWorld->Init();

	return NewWorld;
}

UWorld* UWorld::GetDuplicatedWorldForPIE(UWorld * InWorld)
{
	if (!InWorld)
	{
		return nullptr;
	}

	UWorld* PIEWorld = CreateWorld(EWorldType::PIE, false);

	PIEWorld->SetScenePath(InWorld->GetScenePath());

	FObjectDuplicationParameters Params(InWorld, nullptr);
	Params.DuplicationSeed.Add(InWorld, PIEWorld);
	Params.DuplicationSeed.Add(InWorld->GetPersistentLevel(), PIEWorld->GetPersistentLevel());
	Params.PortFlags = EPropertyPortFlags::PPF_DuplicateForPIE;

	StaticDuplicateObjectEx(Params);
	return PIEWorld;
}

void UWorld::PostDuplicate(bool bDuplicateForPIE)
{
	Super::PostDuplicate(bDuplicateForPIE);

	// 아직 필요 없는애들
	//TArray<UObject*> ObjectsToFixReferences;
	//TMap<UObject*, UObject*> ReplacementMap;

	if (!bDuplicateForPIE)
	{
		assert(PersistentLevel);

		// Update the persistent level's owning world. This is needed for some initialization
		if (!PersistentLevel->OwningWorld)
		{
			PersistentLevel->OwningWorld = this;
		}
	}

	for (AActor* Actor : PersistentLevel->GetActors())
	{
		if (!Actor)
			continue;

		for (UActorComponent* Component : Actor->GetComponents())
			if (USceneComponent* SceneComp = Cast<USceneComponent>(Component))
				SceneComp->MarkTransformDirty();

		Actor->RegisterAllComponents();

		BeginPlayList.Enqueue(Actor);   // Tick 등록은 BeginPlay에서 함
	}

	Scene.UpdateAllTransforms();
}

void UWorld::Serialize(FStructuredArchive::FRecord Record) {
	Super::Serialize(Record);   // "Properties"

	FArchive& Ar{ Record.GetUnderlyingArchive() };
	PersistentLevel->Serialize(Record.EnterRecord("PersistentLevel"));

	if (Ar.HasAnyPortFlags(EPropertyPortFlags::PPF_Duplicate)) {
		UObject* CameraManager{ mPlayerCameraManager };
		Record << SA_VALUE("PlayerCameraManager", CameraManager);
		if (Ar.IsLoading()) {
			mPlayerCameraManager = Cast<APlayerCameraManager>(CameraManager);
		}
		return;
	}

	if (Ar.IsLoading()) {
		for (AActor* Actor : PersistentLevel->GetActors()) {
			if (APlayerCameraManager* CameraManager{ Cast<APlayerCameraManager>(Actor) }) {
				CameraManager->ResolveViewTarget();
			}
		}
	}

	if (TOptional<FStructuredArchive::FSlot> CameraManagerSlot{ Record.TryEnterField("PlayerCameraManager", Ar.IsSaving() && mPlayerCameraManager && mPlayerCameraManager->HasAnyFlags(EObjectFlags::RF_Transient)) }) {
		FStructuredArchive::FRecord CameraManagerRecord{ CameraManagerSlot.GetValue().EnterRecord() };
		FString ClassName{ mPlayerCameraManager ? mPlayerCameraManager->GetClass()->GetName() : FString{} };
		FString ManagerName{ mPlayerCameraManager ? mPlayerCameraManager->GetName() : FString{} };
		CameraManagerRecord << SA_VALUE("Class", ClassName) << SA_VALUE("Name", ManagerName);
		if (Ar.IsLoading()) {
			UClass* ManagerClass{ FindClass(ClassName) };
			if (!ManagerClass || !ManagerClass->IsChildOf(APlayerCameraManager::StaticClass()) || !ManagerClass->Constructor) {
				Ar.SetError();
				return;
			}
			if (mPlayerCameraManager) {
				DestroyActor(mPlayerCameraManager);
			}
			mPlayerCameraManager = Cast<APlayerCameraManager>(SpawnActor(ManagerClass, FName{ ManagerName }));
			if (!mPlayerCameraManager) {
				Ar.SetError();
				return;
			}
			mPlayerCameraManager->SetFlags(EObjectFlags::RF_Transient);
		}
		mPlayerCameraManager->Serialize(CameraManagerRecord);
	}
}

// 광원 종류와 무관한 Receiver 후보 검사이며, GPU 가림 결과는 사용하지 않는다.
bool UWorld::HasVisibleShadowReceiver(const TArray<FFrustumPlanes>& ActiveViewFrustums,
	const FLightRenderParameters* LocalLight, const FFrustumPlanes* ReceiverFrustum, uint32 ReceiverPlaneMask) const
{
	bool bHasReceiver = false;

	const auto BoundsTest = [&](const FBox& Box)
		{
			if (bHasReceiver) return false;
			// 지역광의 기존 영향 구 판정을 유지하고, Directional은 이 제한을 사용하지 않는다.
			if (LocalLight)
			{
				const FVector& Position = LocalLight->Position;
				const FVector ClosestPoint(FMath::Clamp(Position.X, Box.Min.X, Box.Max.X),
					FMath::Clamp(Position.Y, Box.Min.Y, Box.Max.Y),
					FMath::Clamp(Position.Z, Box.Min.Z, Box.Max.Z));
				if ((ClosestPoint - Position).SizeSquared() > LocalLight->Radius * LocalLight->Radius)
					return false;
			}
			// 영향 영역과 겹치는 Bounds가 활성 View 중 하나에서도 보일 수 있어야 한다.
			const FAABB Bounds = MakeWorldBounds(Box);

			// Spot 또는 후속 투영별 Receiver 검사에서 지정한 범위를 사용한다.
			uint32 PlaneMask = ReceiverPlaneMask;
			if (ReceiverFrustum && ClassifyBoxInFrustum(Box, *ReceiverFrustum, PlaneMask) == EFrustumTest::Outside)
			{
				return false;
			}

			for (const FFrustumPlanes& Frustum : ActiveViewFrustums)
			{
				if (IsAABBInFrustum(Bounds, Frustum))
					return true;
			}

			// 어떤 활성 View와도 겹치지 않는 가지는 탐색하지 않는다.
			return false;
		};
	Scene.BVH.Query(BoundsTest, [&bHasReceiver](FPrimitiveSceneProxy* Proxy)
		{
			if (!Proxy->IsVisible() || !Proxy->IsShadowReceiver())
			{
				return;
			}
			if (const auto* StaticMeshComponent = Cast<UStaticMeshComponent>(Proxy->GetComponent()))
			{
				if (StaticMeshComponent->IsEditorIcon() || !Proxy->GetMesh() || !Proxy->GetRenderState())
				{
					return;
				}
			}
			const auto* Billboard = Cast<UBillboardComponent>(Proxy->GetComponent());
			if (Billboard && Billboard->IsEditorIcon())
			{
				return;
			}
			bHasReceiver = true;
		});
	return bHasReceiver;
}

void UWorld::GatherOverlap()
{
	LastFrameOverlapInfo = OverlapInfo;	// 이전 FrameOverlapInfo에 OverlapInfo 저장
	ResetOverlap(); // 현재 FrameOverlapInfo를 리셋
	TArray<UShapeComponent*> Shapes;
	for (ULevel* Level : Levels)
	{
		if (!Level) continue;
		for (AActor* Actor : Level->GetActors())
		{
			if (!Actor || Actor->IsActorBeingDestroyed()) continue;
			for (UActorComponent* Component : Actor->GetComponents())
			{
				if (UShapeComponent* Shape = Cast<UShapeComponent>(Component))
				{
					if (Shape->GetGenerateOverlapEvents())
						Shapes.Add(Shape);	// Shape 판정을 위해 월드에 존재하는 전체 Shape배열에 추가
				}
			}
		}
	}

	for (int i = 0; i < Shapes.Num(); ++i)
	{
		for (int j = i + 1; j < Shapes.Num(); ++j)
		{
			UShapeComponent* First = Shapes[i];
			UShapeComponent* Second = Shapes[j];
			// IsOverlapping 내부에서 AABB 검사 후 정밀 판정을 수행한다.
			if (First->IsOverlapping(*Second))
			{
				// 프레임마다 같은 쌍을 유지하도록 작은 UUID부터 저장, 전체 충돌 쌍 저장
				AddOverlap(std::min(First->GetUUID(), Second->GetUUID()),
				           std::max(First->GetUUID(), Second->GetUUID()));
			}
		}
	}


	for (int i = 0; i < OverlapInfo.Num();i++)
	{
		if (LastFrameOverlapInfo.Find(OverlapInfo[i]) == -1)
		{
			const FOverlapInfo Pair = OverlapInfo[i];
			UShapeComponent* First = FindShapeByUUID(Pair.OverlapFirst);
			UShapeComponent* Second = FindShapeByUUID(Pair.OverlapSecond);
			if (First) First->OnBeginOverlap(Second);
			// 첫 Lua 콜백에서 어느 쪽 Actor든 삭제할 수 있다.
			First = FindShapeByUUID(Pair.OverlapFirst);
			Second = FindShapeByUUID(Pair.OverlapSecond);
			if (Second) Second->OnBeginOverlap(First);
		}
	}

	for (int i = 0; i < LastFrameOverlapInfo.Num();i++)
	{
		if (OverlapInfo.Find(LastFrameOverlapInfo[i]) == -1)
		{
			const FOverlapInfo Pair = LastFrameOverlapInfo[i];
			UShapeComponent* First = FindShapeByUUID(Pair.OverlapFirst);
			UShapeComponent* Second = FindShapeByUUID(Pair.OverlapSecond);
			if (First) First->OnEndOverlap(Second);
			First = FindShapeByUUID(Pair.OverlapFirst);
			Second = FindShapeByUUID(Pair.OverlapSecond);
			if (Second) Second->OnEndOverlap(First);
		}
	}
}

void UWorld::AddOverlap(uint32 OverlapFirst, uint32 OverlapSecond)
{
	FOverlapInfo OverlapInf;
	OverlapInf.OverlapFirst = OverlapFirst;
	OverlapInf.OverlapSecond = OverlapSecond;
	OverlapInfo.Add(OverlapInf);
}

void UWorld::ResetOverlap()
{
	OverlapInfo.Reset();
}

UShapeComponent* UWorld::FindShapeByUUID(uint32 UUID)
{
	// 렌더 프록시가 없는 Shape도 찾고, 비활성화된 컴포넌트에도 EndOverlap을 전달한다.
	for (ULevel* Level : Levels)
	{
		if (!Level) continue;
		for (AActor* Actor : Level->GetActors())
		{
			if (!Actor) continue;
			for (UActorComponent* Component : Actor->GetComponents())
			{
				if (UShapeComponent* Shape = Cast<UShapeComponent>(Component))
				{
					if (Shape->GetUUID() == UUID)
						return Shape;
				}
			}
		}
	}
	return nullptr;
}

FString UWorld::GetSceneName() const
{
	if (ScenePath.empty())
	{
		return "Untitled";
	}

	const std::filesystem::path NativePath = FFileHelper::ToNativePath(ScenePath);
	return FFileHelper::ToUtf8Path(NativePath.stem());
}
