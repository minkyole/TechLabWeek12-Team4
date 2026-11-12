#pragma once

#include "UObject/Object.h"
#include "UObject/Class.h"
#include "GameFramework/Actor.h"
#include "Components/PrimitiveComponent.h"
#include "Components/MovementComponent.h"
#include "Components/TextRenderComponent.h"
#include "Math/Transform.h"
#include "Render/Renderer.h"
#include "Math/Frustum.h"
#include "Math/BVH.h"
#include "Engine/Scene.h"
#include "Camera/CameraActor.h"
#include "Asset/LOD/StaticMeshLODSelector.h"
#include "Engine/EngineTypes.h"
#include "Containers/Queue.h"
#include "GameTime.h"

//class ACameraActor;
class ULevel;
class APlayerCameraManager;
class UShapeComponent;
class UBillboardComponent;
class FProjectedShadowInfo;
struct FLODViewContext;

struct FRenderStats
{
	uint32 TotalPrimitives = 0;
	uint32 VisiblePrimitives = 0;
	uint32 DrawCalls = 0;
	uint64 Triangles = 0;
	uint32 LODCounts[4] = {};
	uint64 LODTriangles[4] = {};

	void Reset() { *this = FRenderStats(); }
};

struct FOverlapInfo
{
	uint32 OverlapFirst;
	uint32 OverlapSecond;

	bool operator==(const FOverlapInfo& other) const
	{
		if (this->OverlapFirst == other.OverlapFirst && this->OverlapSecond == other.OverlapSecond) return true;
		else return false;
	}
};

// Lua가 변경하고 각 View의 ToneMap pass가 읽는 월드 단위 후처리 설정이다.
struct FVignetteSettings
{
	// 렌더링 마스터 스위치다. Intensity가 0이면 활성 상태여도 화면에는 적용되지 않는다.
	bool bEnabled = true;
	float Intensity = 0.0f;
	float Radius = 0.65f;
	float Softness = 0.25f;
	FVector Color = FVector::ZeroVector;

	float GetEffectiveIntensity() const { return bEnabled ? Intensity : 0.0f; }
};

class UWorld : public UObject
{
	DECLARE_CLASS(UWorld, UObject)

public:
	UWorld() = default;
	virtual ~UWorld();

	bool Init();
	/*UPrimitiveComponent* SpawnPrimitive(FClass* Class);*/
	AActor* SpawnActor(UClass* Class, FName InName = NAME_None, const FTransform* Transform = nullptr);

	template <class T>
	T* SpawnActor(FName InName = NAME_None, const FTransform* Transform = nullptr)
	{
		AActor* Actor = SpawnActor(T::StaticClass(), InName, Transform);
		return Actor ? CastChecked<T>(Actor) : nullptr;
	}

	void Tick(float DeltaTime);

	void SetTimeScale(float InTimeScale);
	float GetTimeScale() const { return TimeScale; }

	void ResetTimeEffects();
	void SetVignette(float Intensity, float Radius, float Softness, const FVector& Color);
	void SetVignetteEnabled(bool bEnabled) { VignetteSettings.bEnabled = bEnabled; }
	bool IsVignetteEnabled() const { return VignetteSettings.bEnabled; }
	void ResetVignette();
	const FVignetteSettings& GetVignetteSettings() const { return VignetteSettings; }

	const FGameTime& GetGameTime() const { return GameTime; }
	float GetDeltaSeconds() const { return GameTime.GetDeltaWorldTimeSeconds(); }
	float GetUnscaledDeltaTime() const { return GameTime.GetDeltaRealTimeSeconds(); }

	void ClearWorld();
	void CleanupWorld();
	// 캡처 구간을 시작하고 첫 가시 View에서 동적 렌더 스냅샷을 준비하게 한다.
	void BeginRenderViews();
	// 메인 스레드에서 Proxy를 준비하고 World의 작업 버퍼로 동적 패킷을 추가한다.
	bool GatherDynamicRenderPackets(FPrimitiveSceneProxy& Proxy, FRenderQueue& RenderQueue,
		const FVector& CameraLocation, FBillboardMatrixBuilder ResolveBillboard,
		const void* ViewContext, FRenderQueue* IconQueue = nullptr);
	// Resolver가 있는 View 호출은 LODView와 사전 BeginRenderViews 호출을 전제로 한다.
	void GatherRenderPackets(FRenderQueue& RenderArray, const FLODViewContext* LODView = nullptr,
		const FFrustumPlanes* Frustum = nullptr, FRenderer* Renderer = nullptr,
		FBillboardMatrixBuilder ResolveBillboard = nullptr,
		const void* ViewContext = nullptr, FRenderQueue* IconQueue = nullptr,
		FRenderQueue* ArrowQueue = nullptr);


	// BeginRenderViews 이후 World 공유 지역광 작업을 한 번 추가한다.
	void GatherLocalProjectedShadows(TArray<FProjectedShadowInfo>& OutShadows,
		const TArray<FFrustumPlanes>& ActiveViewFrustums,
		const TArray<FLODViewContext>& ActiveViews, float MinScreenRadiusSquared);

	// 지정한 View·Cascade의 투영과 원래 논리 분할 개수를 보관하고 Caster를 수집한다.
	void GatherDirectionalShadow(TArray<FProjectedShadowInfo>& OutShadows,
		const TArray<FVector>& ReceiverCorners, const FLODViewContext& View,
		float ReceiverNearDistance, float ReceiverFarDistance,
		float MinScreenRadiusSquared, uint32 ProjectionIndex, uint32 ViewId,
		uint32 LogicalCascadeCount = 1);

	// BeginRenderViews와 SetupProjection 이후, 해당 작업에 한 번 호출한다.
	// Light Frustum과 CastShadow 조건으로 정적 메시 Caster를 수집한다.
	void GatherShadowCasters(FProjectedShadowInfo& ShadowInfo,const FLODViewContext* Views, 
		int32 ViewCount, float MinScreenRadiusSquared, const FLightRenderParameters* LocalLight = nullptr,
		float CasterSphereRadius = 0.0f);

	void CreateMainCamera();

	// 카메라 Get/Set
	void SetMainCamera(ACameraActor* Camera);
	ACameraActor* GetMainCamera() const { return MainCamera; }
	UCameraComponent* GetMainCameraComponent();
	void ResetMainCameraComponent(UCameraComponent* Camera);
	APlayerCameraManager* GetPlayerCameraManager();

	// Level
	ULevel* GetPersistentLevel() const { return PersistentLevel; }
	void SetPersistentLevel(ULevel* InLevel) { PersistentLevel = InLevel; }

	ULevel* GetCurrentLevel() const { return CurrentLevel; }
	void SetCurrentLevel(ULevel* InLevel) { CurrentLevel = InLevel; }


	int32 GetActorNum();

	bool DestroyActor(AActor* Actor);
	void FlushPendingDestroyActors();

	void RequestDestroyComponent(UActorComponent* Component);
	void FlushPendingDestroyComponents();
	

	// View별 Billboard 행렬 공급자는 이 동기 호출 동안만 사용하며 저장하지 않는다.
	using FBillboardTraceTransform = FMatrix(*)(const UBillboardComponent&, const void*);
	// 현재 World의 Component에 Ray를 전달하고 가장 가까운 유효 교차를 반환한다.
	bool LineTraceSingle(const FRay& WorldRay, FHitResult& OutHit, FBillboardTraceTransform ResolveBillboard = nullptr, const void* ViewContext = nullptr, const AActor* IgnoreActor = nullptr);

	void BeginPlay();
	void EndPlay();

	FScene& GetScene() { return Scene; }
	FTickTaskManager& GetTickTaskManager() { return TickTaskManager; }

	const FRenderStats& GetRenderStats() const { return RenderStats; }

	void SetWorldType(EWorldType InType) { WorldType = InType; }
	EWorldType GetWorldType() const { return WorldType; }

	static UWorld* CreateWorld(const EWorldType InWorldType, bool bInformEngineOfWorld, FName WorldName = NAME_None); /*, UPackage* InWorldPackage = NULL, bool bAddToRoot = true, ERHIFeatureLevel::Type InFeatureLevel = ERHIFeatureLevel::Num, const InitializationValues* InIVS = nullptr, bool bInSkipInitWorld = false);*/

	bool IsPlayInEditor() const { return WorldType == EWorldType::PIE; }

	static UWorld* GetDuplicatedWorldForPIE(UWorld* InWorld);

	virtual void PostDuplicate(bool bDuplicateForPIE) override;


	using Super::Serialize;
	virtual void Serialize(FStructuredArchive::FRecord Record) override;

	void SetScenePath(const FString& InScenePath)
	{
		ScenePath = InScenePath;
	}

	const FString& GetScenePath() const
	{
		return ScenePath;
	}

	FString GetSceneName() const;

	void GatherOverlap();
	void AddOverlap(uint32 OverlapFirst, uint32 OverlapSecond);
	void ResetOverlap();
	UShapeComponent* FindShapeByUUID(uint32 UUID);
private:
	void InitializePlayCamera();
	void UpdateGameTime(float DeltaTime);

	// 활성 Lit View 중 하나에서 Receiver 후보가 보이면 유지한다.
	// LocalLight와 ReceiverFrustum은 지정한 경우에만 영향 구와 투영 범위를 제한한다.
	bool HasVisibleShadowReceiver(const TArray<FFrustumPlanes>& ActiveViewFrustums,
		const FLightRenderParameters* LocalLight = nullptr, const FFrustumPlanes* ReceiverFrustum = nullptr,
		uint32 ReceiverPlaneMask = FrustumAllPlanesMask) const;

	// 모든 전달 View에서 Bounds가 작을 때만 해당 투영의 제출을 생략한다.
	static bool IsSmallShadowCaster(const FLODSphere& Sphere, const FLODViewContext* Views, int32 ViewCount, float MinScreenRadiusSquared);

	EWorldType WorldType = EWorldType::None;

	FGameTime GameTime;
	float TimeScale = 1.0f;
	FVignetteSettings VignetteSettings;

	struct alignas(64) FGatherChunk
	{
		TArray<FRenderPacket> Packets;              // 스태틱 묶음에 못 들어가는 것 (반투명 섹션, Renderer 없는 호출)
		TArray<uint32> SlowPathIndices;
		std::vector<FStaticDrawGroup> Groups;       // 불투명 스태틱 메시: (머티리얼, 메시, LOD)별 묶음. 항목만 매 프레임 비운다
		uint32 LastGroup = 0;                       // 바로 전 물체가 들어간 묶음 (연속한 물체는 대개 같은 묶음)
		uint32 StaticDrawCount = 0;
		uint32 LODCounts[4] = {};
		uint64 LODTriangles[4] = {};
	};


	// 실제 사각 Projection 전체와 래스터 경계 여유를 포함한다.
	// 0을 반환하면 추가 Sphere 검사를 사용하지 않는다.
	static float GetLocalShadowCasterRadius(const ULightComponent& Light, const FLightRenderParameters& Parameters, float TanHalfFOV);

	TArray<FGatherChunk> GatherChunks;

	// 등록된 Tick 함수만 실행한다. Actor보다 먼저 사라져도 남은 함수와의 연결을 스스로 끊는다.
	FTickTaskManager TickTaskManager;

	TQueue<AActor*> BeginPlayList;

	bool DestroyActorNow(AActor* Actor);
	TArray<AActor*> PendingDestroyActors;
	TArray<UActorComponent*> PendingDestroyComponents;
	bool bInGameTick = false;
	bool bEndingPlay = false;
	//메인 카메라 
	ACameraActor* MainCamera = nullptr;
	UCameraComponent* mPlayCameraComponent{};
	APlayerCameraManager* mPlayerCameraManager{};


	ULevel* PersistentLevel = nullptr;
	ULevel* CurrentLevel = nullptr;
	TArray<ULevel*> Levels;

	FScene Scene;

	// GatherRenderPackets가 매 프레임 채우는 컬링 결과. 용량을 재사용한다.
	TArray<FPrimitiveSceneProxy*> VisibleProxies;
	TArray<FLODSelectionInput> LODInputs;
	TArray<uint8> SelectedLODs;
	// 메인 스레드의 동적 수집에서 View마다 내용을 바꾸고 용량은 재사용한다.
	TArray<int32> ParticleOrder;
	TArray<float> ParticleDistances;
	TArray<FOverlapInfo> OverlapInfo;
	TArray<FOverlapInfo> LastFrameOverlapInfo;

	FRenderStats RenderStats;

	FString ScenePath;
};
