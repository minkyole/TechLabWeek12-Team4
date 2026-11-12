#include "EnginePCH.h"
#include "Scene.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/SpotLightComponent.h"
#include "Components/SkyLightComponent.h"
#include "Components/RectLightComponent.h"
#include "Render/DecalSettings.h"
#include "Core/Stats/DecalStats.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SkinnedMeshComponent.h"

// 투영 Decal이 찍힐 수 있는 Primitive인지 본다. 에디터 아이콘이나 Billboard가 움직여도 Decal을 다시 만들지 않는다.
// 스켈레탈은 자세가 바뀌면 UpdateSkinning이 OnTransformDirty를 불러 여기를 거쳐 다시 잘린다.
static bool CanReceiveProjectedDecal(const FPrimitiveSceneProxy* Proxy)
{
	if (Cast<USkinnedMeshComponent>(Proxy->GetComponent()))
		return true;
	const auto* StaticMeshComponent = Cast<UStaticMeshComponent>(Proxy->GetComponent());
	return StaticMeshComponent && !StaticMeshComponent->IsEditorIcon();
}

void FScene::AddPrimitive(UPrimitiveComponent* Component)
{
	if (!Component || Component->SceneProxy) return;

	FPrimitiveSceneProxy* Proxy = Component->CreateSceneProxy();
	Proxy->Scene = this;
	Proxy->PackedIndex = PrimitiveSceneProxies.Num();
	Component->SceneProxy = Proxy;
	MarkDirty(Proxy);
	MarkRenderStateDirty(Proxy);

	PrimitiveSceneProxies.Add(Proxy);
	PrimitiveBounds.Add(FAABB{});
	PrimitiveFlags.Add(0);

	bElementListChanged = true;
}

void FScene::RemovePrimitive(UPrimitiveComponent* Component)
{
	if (!Component || !Component->SceneProxy) return;

	FPrimitiveSceneProxy* Proxy = Component->SceneProxy;
	if (!Proxy) return;

	const uint32 Index = static_cast<uint32>(Proxy->PackedIndex);

	// 지워진 메시 위에 찍혀 있던 투영 Decal을 다시 만들 수 있게 영역을 남긴다.
	if (CanReceiveProjectedDecal(Proxy))
		ChangedReceiverBounds.Add(PrimitiveBounds[Index]);

	PrimitiveSceneProxies.RemoveAtSwap(Index);
	PrimitiveBounds.RemoveAtSwap(Index);
	PrimitiveFlags.RemoveAtSwap(Index);

	if (Index < static_cast<uint32>(PrimitiveSceneProxies.Num()))
		PrimitiveSceneProxies[Index]->PackedIndex = static_cast<int32>(Index);

	if (Proxy->bQueuedForUpdate)
	{
		const int32 Found = DirtyProxies.Find(Proxy);   // 프로젝트 TArray의 Find 이름에 맞게
		if (Found != INDEX_NONE) DirtyProxies.RemoveAtSwap(Found);
	}

	if (Proxy->bRenderStateQueued)
	{
		const int32 Found = RenderStateDirtyProxies.Find(Proxy);
		if (Found != INDEX_NONE) RenderStateDirtyProxies.RemoveAtSwap(Found);
	}

	Component->SceneProxy = nullptr;
	delete Proxy;

	bElementListChanged = true;
}

void FScene::RemoveAllPrimitives()
{
	// 하나씩 RemovePrimitive하면 대기열 검색(Find)이 물체 수만큼 반복되므로 통째로 비운다.
	for (FPrimitiveSceneProxy* Proxy : PrimitiveSceneProxies)
	{
		if (UPrimitiveComponent* Component = Proxy->GetComponent())
			Component->SceneProxy = nullptr;
		delete Proxy;
	}
	PrimitiveSceneProxies.Reset();
	PrimitiveBounds.Reset();
	PrimitiveFlags.Reset();
	DirtyProxies.Reset();
	RenderStateDirtyProxies.Reset();
	BVH.Clear();
	bElementListChanged = true;

	ChangedReceiverBounds.Reset();
	for (FDecalSceneProxy* Proxy : DecalSceneProxies)
	{
		Proxy->bProjectedMeshDirty = true;
	}
}

void FScene::AddLight(ULightComponent* Component)
{
	if (!Component || Component->SceneProxy) return;

	FLightSceneProxy* Proxy = Component->CreateSceneProxy();
	if (!Proxy)
	{
		if (USkyLightComponent* Sky = Cast<USkyLightComponent>(Component))
			if (SkyLights.Find(Sky) == INDEX_NONE) SkyLights.Add(Sky);
		return;
	}

	Proxy->Scene = this;
	Component->SceneProxy = Proxy;

	if (Component->GetLightType() == ELightType::Directional)
	{
		Proxy->PackedIndex = DirectionalLightProxies.Num();
		DirectionalLightProxies.Add(static_cast<FDirectionalLightSceneProxy*>(Proxy));
	}
	else
	{
		Proxy->PackedIndex = LocalLightProxies.Num();
		LocalLightProxies.Add(static_cast<FLocalLightSceneProxy*>(Proxy));
		LocalLightParameters.Add(FLightRenderParameters{});
	}
}

void FScene::RemoveLight(ULightComponent* Component)
{
	if (!Component) return;

	FLightSceneProxy* Proxy = Component->SceneProxy;
	if (!Proxy)
	{
		if (USkyLightComponent* Sky = Cast<USkyLightComponent>(Component))
		{
			const int32 Found = SkyLights.Find(Sky);
			if (Found != INDEX_NONE) SkyLights.RemoveAtSwap(Found);
		}
		return;
	}

	const int32 Index = Proxy->PackedIndex;
	if (Component->GetLightType() == ELightType::Directional)
	{
		DirectionalLightProxies.RemoveAtSwap(Index);
		if (Index < DirectionalLightProxies.Num())
			DirectionalLightProxies[Index]->PackedIndex = Index;
	}
	else
	{
		LocalLightProxies.RemoveAtSwap(Index);
		LocalLightParameters.RemoveAtSwap(Index);      // 두 배열을 같이 움직여야 순서가 맞음
		if (Index < LocalLightProxies.Num())
			LocalLightProxies[Index]->PackedIndex = Index;
	}

	Component->SceneProxy = nullptr;
	delete Proxy;
}

void FScene::RemoveAllLights()
{
	for (FLightSceneProxy* Proxy : DirectionalLightProxies) delete Proxy;
	for (FLightSceneProxy* Proxy : LocalLightProxies)       delete Proxy;
	DirectionalLightProxies.Reset();
	LocalLightProxies.Reset();
	LocalLightParameters.Reset();
	SkyLights.Reset();
}

void FScene::AddDecal(UDecalComponent* Component)
{
	if (!Component || Component->SceneProxy) return;

	FDecalSceneProxy* Proxy = Component->CreateSceneProxy();
	if (!Proxy)
	{
		return;
	}

	Component->SceneProxy = Proxy;
	Proxy->Scene = this;
	Proxy->PackedIndex = DecalSceneProxies.Num();
	MarkDecalDirty(Proxy);
	MarkDecalRenderStateDirty(Proxy);

	DecalSceneProxies.Add(Proxy);
}

void FScene::RemoveDecal(UDecalComponent* Component)
{
	if (!Component) return;

	FDecalSceneProxy* Proxy = Component->SceneProxy;
	if (!Proxy)
	{
		return;
	}

	const int32 Index = Proxy->PackedIndex;

	DecalSceneProxies.RemoveAtSwap(Index);
	if (Index < DecalSceneProxies.Num())
		DecalSceneProxies[Index]->PackedIndex = Index;

	if (Proxy->bQueuedForUpdate)
	{
		const int32 Found = DirtyDecalProxies.Find(Proxy);
		if (Found != INDEX_NONE) DirtyDecalProxies.RemoveAtSwap(Found);
	}

	if (Proxy->bRenderStateQueued)
	{
		const int32 Found = RenderStateDirtyDecalProxies.Find(Proxy);
		if (Found != INDEX_NONE) RenderStateDirtyDecalProxies.RemoveAtSwap(Found);
	}

	Component->SceneProxy = nullptr;
	delete Proxy;
}

void FScene::RemoveAllDecals()
{
	for (FDecalSceneProxy* Proxy : DecalSceneProxies)
	{
		delete Proxy;
	}

	DecalSceneProxies.Reset();
	DirtyDecalProxies.Reset();
	RenderStateDirtyDecalProxies.Reset();
}

// 등록된 라이트를 셰이더용 상수로 모아 Renderer에 올린다. 컴포넌트 값을 매 프레임 직접 읽으므로 별도 갱신 경로가 필요 없다.
void FScene::UpdateLights(FRenderer* Renderer)
{
	if (!Renderer) return;

	FLightingConstants Constants{};
	Constants.Ambient.Color = FVector(1, 1, 1);
	Constants.Ambient.Intensity = 0.0f;

	for (USkyLightComponent* Sky : SkyLights)            // 기존 Sky 코드 그대로
	{
		if (!Sky->IsVisible()) continue;
		const FLinearColor Color(Sky->GetLightColor());
		Constants.Ambient.Color = FVector(Color.R, Color.G, Color.B);
		Constants.Ambient.Intensity = Sky->GetIntensity();
		break;
	}

	if (const FDirectionalLightSceneProxy* Proxy = GetPrimaryDirectionalLight())
	{
		FLightRenderParameters P;
		Proxy->GetLightShaderParameters(P);
		Constants.Directional.Direction = P.Direction;
		Constants.Directional.Color = P.Color;
		Constants.Directional.Intensity = P.Intensity;
		Constants.Directional.SourceAngle = P.SourceAngle;
	}

	for (FLocalLightSceneProxy* Proxy : LocalLightProxies)
	{
		FLightRenderParameters& P = LocalLightParameters[Proxy->PackedIndex];
		Proxy->GetLightShaderParameters(P);
		P.ShadowIndex = Renderer->GetShadowIndex(*Proxy);
		if (!Proxy->IsVisible())
			P.Radius = 0.0f;                              // 칸은 유지하고 영향만 없앤다
			/*FTransform Transform;
			Transform.Rotation = Spot->GetWorldRotation();
			const FLinearColor Color(Spot->GetLightColor());

			Constants.SpotLights[Constants.NumSpotLights].Color = FVector(Color.R, Color.G, Color.B);
			Constants.SpotLights[Constants.NumSpotLights].Direction = Transform.GetForward();
			Constants.SpotLights[Constants.NumSpotLights].FalloffExponent = Spot->GetFalloff();
			Constants.SpotLights[Constants.NumSpotLights].Intensity = Spot->GetIntensity();
			Constants.SpotLights[Constants.NumSpotLights].Position = Spot->GetWorldLocation();
			Constants.SpotLights[Constants.NumSpotLights].Radius = Spot->GetAttenuationRadius();
			const float SafeOuterAngle = FMath::Clamp(Spot->GetOuterConeAngle(), 0.0f, 89.0f);
			const float SafeInnerAngle = FMath::Clamp(Spot->GetInnerConeAngle(), 0.0f, SafeOuterAngle);
			Constants.SpotLights[Constants.NumSpotLights].InnerConeCos = std::cos(FMath::DegreesToRadians(SafeInnerAngle));
			Constants.SpotLights[Constants.NumSpotLights].OuterConeCos = std::cos(FMath::DegreesToRadians(SafeOuterAngle));
			Constants.NumSpotLights++;
		}
		else if (UPointLightComponent* Point = Cast<UPointLightComponent>(Light))
		{
			const FLinearColor Color(Point->GetLightColor());
			FPointLightInfo Info{};
			Info.Position = Point->GetWorldLocation();
			Info.Radius = Point->GetAttenuationRadius();
			Info.Color = FVector(Color.R, Color.G, Color.B);
			Info.Intensity = Point->GetIntensity();
			Info.FalloffExponent = Point->GetFalloff();
			PointLights.Add(Info);
		}
		else if (URectLightComponent* Rect = Cast<URectLightComponent>(Light))
		{
			if (Constants.NumRectLights >= MaxRectLights)
				continue;

			FTransform Transform;
			Transform.Rotation = Rect->GetWorldRotation();
			const FLinearColor Color(Rect->GetLightColor());

			FRectLightInfo& Info = Constants.RectLights[Constants.NumRectLights++];
			Info.Position   = Rect->GetWorldLocation();
			Info.Radius     = Rect->GetAttenuationRadius();
			Info.Color      = FVector(Color.R, Color.G, Color.B);
			Info.Intensity  = Rect->GetIntensity();
			Info.Direction  = Transform.GetForward();
			Info.Right      = Transform.GetRight();
			Info.HalfWidth  = Rect->GetWidth() * 0.5f;
			Info.HalfHeight = Rect->GetHeight() * 0.5f;

			const float AngleRad = FMath::DegreesToRadians(FMath::Clamp(Rect->GetBarnDoorAngle(), 0.0f, 90.0f));
			Info.BarnDoorCos    = std::cos(AngleRad);
			Info.BarnDoorSin    = std::sin(AngleRad);
			Info.BarnDoorLength = std::max(Rect->GetBarnDoorLength(), 0.0f);
		}*/
	}

	Renderer->SetLighting(Constants, LocalLightParameters);
}

const FDirectionalLightSceneProxy* FScene::GetPrimaryDirectionalLight() const
{
	for (const FDirectionalLightSceneProxy* Proxy : DirectionalLightProxies)
	{
		if (Proxy->IsVisible())
			return Proxy;
	}
	return nullptr;
}
void FScene::AddFogInfo(uint32 Id, const FFogInfo& FogInfo)
{
	FExponentialHeightFogSceneInfo sceneFogInfo;
	sceneFogInfo.Id = Id;
	sceneFogInfo.FogInfo = FogInfo;


	FogInfos.Add(sceneFogInfo);
}

void FScene::RemoveFogInfo(uint32 Id)
{
	// 렌더러는 FogInfos[0]만 쓰므로, 순서를 유지하려고 RemoveAtSwap 대신 RemoveAt을 쓴다.
	for (int32 i = 0; i < FogInfos.Num(); ++i)
	{
		if (FogInfos[i].Id == Id)
		{
			FogInfos.RemoveAt(i);
			return;
		}
	}
}

void FScene::RemoveAllFogInfo()
{
	FogInfos.Reset();
	ResetFogOverride();

}

void FScene::SetFogOverride(const FFogInfo& FogInfo) {
	mFogOverride = FogInfo;
	mHasFogOverride = true;
}

void FScene::ResetFogOverride() {
	mFogOverride = {};
	mHasFogOverride = false;
}

bool FScene::HasFogOverride() const {
	return mHasFogOverride;
}

const FFogInfo* FScene::GetFogInfo() const {
	if (mHasFogOverride) {
		return &mFogOverride;
	}
	return FogInfos.IsEmpty() ? nullptr : &FogInfos[0].FogInfo;
}

void FScene::UpdateFogInfo(uint32 Id, const FFogInfo& FogInfo)
{
	for (FExponentialHeightFogSceneInfo& SceneFogInfo : FogInfos)
	{
		if (SceneFogInfo.Id == Id)
		{
			SceneFogInfo.FogInfo = FogInfo;
			return;
		}


	}
}

//void FScene::RemoveFogInfo(uint32 Id)
//{
//	for (int32 i = 0; i < FogInfos.Num(); ++i)
//	{
//		if (FogInfos[i].Id == Id)
//		{
//			FogInfos.RemoveAtSwap(i);
//			return;
//		}
//	}
//}


void FScene::UpdateAllTransforms()
{
	for (FDecalSceneProxy* Proxy : RenderStateDirtyDecalProxies)
	{
		Proxy->UpdateRenderState();
		Proxy->bRenderStateQueued = false;
	}
	RenderStateDirtyDecalProxies.Reset();

	for (FDecalSceneProxy* Proxy : DirtyDecalProxies)
	{
		Proxy->UpdateTransform();
		Proxy->bQueuedForUpdate = false;
	}
	DirtyDecalProxies.Reset();

	for (FPrimitiveSceneProxy* Proxy : RenderStateDirtyProxies)
	{
		Proxy->UpdateRenderState();
		Proxy->bRenderStateQueued = false;
		// 보이기, Receive Decal, 메시가 바뀌었을 수 있다.
		if (CanReceiveProjectedDecal(Proxy))
			ChangedReceiverBounds.Add(Proxy->GetBounds());
	}
	RenderStateDirtyProxies.Reset();

	for (FPrimitiveSceneProxy* Proxy : DirtyProxies)
	{
		// 옮기기 전 자리의 Decal은 표면이 사라지고, 옮긴 자리의 Decal은 표면이 새로 생긴다.
		const bool bReceiver = CanReceiveProjectedDecal(Proxy);
		if (bReceiver)
			ChangedReceiverBounds.Add(PrimitiveBounds[Proxy->PackedIndex]);
		Proxy->UpdateTransform();
		PrimitiveBounds[Proxy->PackedIndex] = Proxy->GetBounds();
		if (bReceiver)
			ChangedReceiverBounds.Add(Proxy->GetBounds());
		PrimitiveFlags[Proxy->PackedIndex] = Proxy->GetComponent()->IsVisible() ? 1 : 0;
		Proxy->bQueuedForUpdate = false;
	}

	//const int32 Count = Proxies.Num();
	//for (int32 i = 0; i < Count; ++i)
	//{
	//	FPrimitiveSceneProxy* Proxy = Proxies[i];
	//	Proxy->UpdateTransform();

	//	const FAABB& Bounds = Proxy->GetBounds();
	//	if (PrimitiveBounds[i].Center != Bounds.Center || PrimitiveBounds[i].Extent != Bounds.Extent)
	//	{
	//		bBoundsChanged = true;
	//	}

	//	PrimitiveBounds[i] = Proxy->GetBounds();
	//	PrimitiveFlags[i] = Proxy->GetComponent()->IsVisible() ? 1 : 0;
	//}

	const bool bAnyMoved = DirtyProxies.Num() > 0;
	DirtyProxies.Reset();


	if (bElementListChanged)
	{
		BuildBVH();
		bElementListChanged = false;
	}
	else if (bAnyMoved)
	{
		BVH.Refit();
	}

	// SSD 모드에서도 표시는 남겨 두어, Projected로 바꿀 때 낡은 메시를 다시 만들게 한다.
	MarkProjectedDecalsDirty();

	if (FDecalSettings::GetMode() == EDecalMode::Projected)
	{
		SCOPE_CYCLE_COUNTER(DecalStats::STAT_ProjectedBuildCPU);
		for (FDecalSceneProxy* Proxy : DecalSceneProxies)
		{
			if (Proxy->bProjectedMeshDirty)
			{
				Proxy->BuildProjectedMesh(*this);
				FStatRegistry::AddCounter(DecalStats::STAT_ProjectedBuilds);
			}
		}
	}
}

static bool AABBOverlaps(const FAABB& A, const FAABB& B)
{
	return std::abs(A.Center.X - B.Center.X) <= A.Extent.X + B.Extent.X &&
		   std::abs(A.Center.Y - B.Center.Y) <= A.Extent.Y + B.Extent.Y &&
		   std::abs(A.Center.Z - B.Center.Z) <= A.Extent.Z + B.Extent.Z;
}

void FScene::MarkProjectedDecalsDirty()
{
	if (AppliedDecalSettingsVersion != FDecalSettings::GetVersion())
	{
		AppliedDecalSettingsVersion = FDecalSettings::GetVersion();
		for (FDecalSceneProxy* Proxy : DecalSceneProxies)
		{
			Proxy->bProjectedMeshDirty = true;
		}
		ChangedReceiverBounds.Reset();
		return;
	}

	if (ChangedReceiverBounds.IsEmpty())
	{
		return;
	}

	for (FDecalSceneProxy* Proxy : DecalSceneProxies)
	{
		if (Proxy->bProjectedMeshDirty)
		{
			continue;
		}

		for (const FAABB& Changed : ChangedReceiverBounds)
		{
			if (AABBOverlaps(Proxy->GetBounds(), Changed))
			{
				Proxy->bProjectedMeshDirty = true;
				break;
			}
		}
	}

	ChangedReceiverBounds.Reset();
}

void FScene::BuildBVH()
{
	BVH.Clear();
	// BillboardComponents.Reset();

	TArray<FPrimitiveSceneProxy*> Elements;
	Elements.Reserve(PrimitiveSceneProxies.Num());
	for (FPrimitiveSceneProxy* Proxy : PrimitiveSceneProxies)
	{
		if (Proxy && Proxy->GetComponent())
			Elements.Add(Proxy);
	}

	BVH.Build(std::span(Elements.GetData(), Elements.Num()));
}

void FScene::MarkDirty(FPrimitiveSceneProxy* Proxy)
{
	if (Proxy->bQueuedForUpdate) return;            // 중복 추가 방지
	Proxy->bQueuedForUpdate = true;
	DirtyProxies.Add(Proxy);
}

void FScene::MarkRenderStateDirty(FPrimitiveSceneProxy* Proxy)
{
	if (!Proxy || Proxy->bRenderStateQueued) return;
	Proxy->bRenderStateQueued = true;
	RenderStateDirtyProxies.Add(Proxy);
}

void FScene::MarkDecalDirty(FDecalSceneProxy* Proxy)
{
	if (!Proxy || Proxy->bQueuedForUpdate) return;
	Proxy->bQueuedForUpdate = true;
	DirtyDecalProxies.Add(Proxy);
}

void FScene::MarkDecalRenderStateDirty(FDecalSceneProxy* Proxy)
{
	if (!Proxy || Proxy->bRenderStateQueued) return;
	Proxy->bRenderStateQueued = true;
	RenderStateDirtyDecalProxies.Add(Proxy);
}
