#pragma once

#include "PrimitiveSceneProxy.h"
#include "LightSceneProxy.h"
#include "LocalLightSceneProxy.h"
#include "DecalSceneProxy.h"
#include "DirectionalLightSceneProxy.h"
#include "Components/BillboardComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Components/LightComponent.h"
#include "Components/SkyLightComponent.h"
#include "Math/Frustum.h"
#include "Math/BVH.h"
#include "Render/FogInfo.h"
#include "Render/Renderer.h"
 
struct FExponentialHeightFogSceneInfo
{
	uint32 Id;
	FFogInfo FogInfo;
};

class FScene
{
public:
	void AddPrimitive(UPrimitiveComponent* Component);
	void RemovePrimitive(UPrimitiveComponent* Component);
	// 모든 프록시를 한 번에 지운다. 액터를 통째로 지우기 전(ClearWorld)에 불러야 지워진 컴포넌트를 가리키는 프록시가 남지 않는다.
	void RemoveAllPrimitives();

	void AddLight(ULightComponent* Component);
	void RemoveLight(ULightComponent* Component);

	void RemoveAllLights();
	void UpdateLights(FRenderer* Renderer);
	const FDirectionalLightSceneProxy* GetPrimaryDirectionalLight() const;
	void AddFogInfo(uint32 Id, const FFogInfo& FogInfo);
	void RemoveFogInfo(uint32 Id);
	void RemoveAllFogInfo();
	void UpdateFogInfo(uint32 Id, const FFogInfo& FogInfo);
	void SetFogOverride(const FFogInfo& FogInfo);
	void ResetFogOverride();
	bool HasFogOverride() const;
	const FFogInfo* GetFogInfo() const;

	void AddDecal(UDecalComponent* Component);
	void RemoveDecal(UDecalComponent* Component);
	void RemoveAllDecals();

	void UpdateAllTransforms();

	void BuildBVH();

	void MarkDirty(FPrimitiveSceneProxy* Proxy);
	void MarkRenderStateDirty(FPrimitiveSceneProxy* Proxy);
	void MarkDecalDirty(FDecalSceneProxy* Proxy);
	void MarkDecalRenderStateDirty(FDecalSceneProxy* Proxy);

	TArray<FPrimitiveSceneProxy*> PrimitiveSceneProxies;
	TArray<FPrimitiveSceneProxy*> DirtyProxies;
	TArray<FPrimitiveSceneProxy*> RenderStateDirtyProxies;
	TArray<FAABB> PrimitiveBounds;
	TArray<uint8> PrimitiveFlags;
	
	TArray<FExponentialHeightFogSceneInfo> FogInfos;

	// 컬링 결과로 프록시를 바로 내보내 컴포넌트를 역참조하지 않는다. 경계는 Build/Refit 때만 계산한다.
	TBVH<FPrimitiveSceneProxy*> BVH{
		[](const FPrimitiveSceneProxy* Proxy) -> FBox
		{
			const FAABB& B = Proxy->GetBounds();
			return FBox{ B.Center - B.Extent, B.Center + B.Extent };
		}
	};
	bool bElementListChanged = false;

	TArray<FLocalLightSceneProxy*>        LocalLightProxies;      // ↔ PackedIndex ↔
	TArray<FLightRenderParameters>        LocalLightParameters;   // GPU 업로드용, 같은 순서
	TArray<FDecalSceneProxy*>			  DecalSceneProxies;
	TArray<FDecalSceneProxy*>			  DirtyDecalProxies;
	TArray<FDecalSceneProxy*>			  RenderStateDirtyDecalProxies;
	TArray<FDirectionalLightSceneProxy*>  DirectionalLightProxies;
	TArray<USkyLightComponent*>           SkyLights;               // 프록시 없음

private:
	// 바뀐 Receiver 영역과 겹치는 Decal에만 투영 메시 재생성 표시를 한다.
	void MarkProjectedDecalsDirty();

	FFogInfo mFogOverride{};
	bool mHasFogOverride{};

	// 이번 프레임에 추가, 삭제, 이동, 상태 변경된 Primitive가 차지하던 영역과 새 영역
	TArray<FAABB> ChangedReceiverBounds;
	// 마지막으로 반영한 FDecalSettings 버전. 각도 같은 설정이 바뀌면 모든 투영 메시를 다시 만든다.
	uint32 AppliedDecalSettingsVersion = 0;
};
