#include "EnginePCH.h"
#include "ExecutableApp.h"

#ifdef EXECUTABLE

#include "Asset/LOD/StaticMeshLODSelector.h"
#include "Render/DecalRenderer.h"
#include "Camera/CameraComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "Sequence/SequenceActor.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/TextRenderComponent.h"
#include "Core/ResourcePaths.h"
#include "Core/Stats/LightweightStats.h"
#include "Engine/DirectionalLightSceneProxy.h"
#include "Engine/SceneTransitionManager.h"
#include "Launch/LaunchEngineLoop.h"
#include "Render/RenderCommand.h"
#include "Render/Swapchain.h"
#include "Serialization/JsonArchive.h"
#include "UObject/UObjectIterator.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace {
	constexpr const char* MainScenePath{ "Assets/Scenes/Demo/05_TheBasement.scene" };
	constexpr uint32 MainViewId{ 1 };
}

FEngineConfig UExecutableEngine::GetConfig() const {
	FEngineConfig Config{ Super::GetConfig() };
	Config.Title = L"Hitori - Executable";
	Config.SyncInterval = 1;
	Config.bCreateDepthBuffer = false;
	Config.SplashImage = ResourcePaths::SplashImage;
	return Config;
}

bool UExecutableEngine::Init() {
	if (!Super::Init()) {
		return false;
	}

	FRenderer* Renderer{ GetEngineLoop().GetRenderer() };
	if (!mSkyboxRenderer.Init(ResourcePaths::DefaultSkyPanorama) || !mFogRenderer.Init(Renderer) || !mShadowViewRenderer.Init() || !mToneMapRenderer.Init() || !mFXAARenderer.Init() || !mDecalRenderer.Init(Renderer)) {
		HTR_LOG(Error, "[Executable] Failed to initialize rendering");
		return false;
	}
	mTextRenderer.Init();
	mUIRenderer.Init(mTextRenderer);
	Renderer->SetViewMode(EViewMode::Lit_BlinnPhong);

	mWorld = UWorld::CreateWorld(EWorldType::Game, false);
	if (!mWorld || !mWorld->GetPersistentLevel() || !mWorld->GetMainCamera()) {
		HTR_LOG(Error, "[Executable] Failed to create game world");
		return false;
	}

	mWorld->SetScenePath(MainScenePath);
	if (!FJsonArchive::LoadWorld(mWorld, MainScenePath)) {
		HTR_LOG(Error, "[Executable] Failed to load scene: {}", MainScenePath);
		return false;
	}

	FWorldContext& WorldContext{ CreateNewWorldContext(EWorldType::Game) };
	WorldContext.SetCurrentWorld(mWorld);
	OnResize(GetEngineLoop().GetViewportWidth(), GetEngineLoop().GetViewportHeight());
	mWorld->BeginPlay();
	HTR_LOG(Info, "[Executable] Playing scene: {}", MainScenePath);
	return true;
}

void UExecutableEngine::Tick(float DeltaTime) {
	mWorld->Tick(DeltaTime);
	if (FSceneTransitionManager* SceneTransitions = GetSceneTransitionManager()) {
		SceneTransitions->ProcessPendingTransition();
	}

	APlayerCameraManager* CameraManager{ mWorld->GetPlayerCameraManager() };
	const uint32 Width{ GetEngineLoop().GetViewportWidth() };
	const uint32 Height{ GetEngineLoop().GetViewportHeight() };
	if (CameraManager && CameraManager->HasValidCameraView() && Width > 0 && Height > 0) {
		FMinimalViewInfo Camera{ CameraManager->GetCameraView() };
		Camera.SetAspectRatio(static_cast<float>(Width) / static_cast<float>(Height));
		CameraManager->GetViewTargetCamera()->SetAspectRatio(Camera.GetAspectRatio());
		RenderFrame(Camera);
	}
	FStatRegistry::EndFrame();
}

void UExecutableEngine::OnResize(uint32 Width, uint32 Height) {
	if (Width == 0 || Height == 0) {
		return;
	}

	D3D11_TEXTURE2D_DESC Desc{};
	Desc.Width = Width;
	Desc.Height = Height;
	Desc.MipLevels = 1;
	Desc.ArraySize = 1;
	Desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	Desc.SampleDesc.Count = 1;
	Desc.Usage = D3D11_USAGE_DEFAULT;
	Desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
	mColorTarget = RenderCommand::CreateTexture2D(Desc);

	Desc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
	mHDRTarget = RenderCommand::CreateTexture2D(Desc);

	Desc.Format = DXGI_FORMAT_R24G8_TYPELESS;
	Desc.BindFlags = D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE;
	mDepthTarget = RenderCommand::CreateTexture2D(Desc);

	mRenderingInfo = {};
	mRenderingInfo.ViewportSetting = { 0, 0, Width, Height, 0.0f, 1.0f };
	FRenderingDesc ColorDesc{};
	ColorDesc.Texture = mHDRTarget.get();
	mRenderingInfo.ColorRenderTargets.Add(ColorDesc);
	mRenderingInfo.DepthStencil.Texture = mDepthTarget.get();
}

void UExecutableEngine::PreExit() {
	if (mWorld) {
		if (FSceneTransitionManager* SceneTransitions = GetSceneTransitionManager()) {
			SceneTransitions->CancelForWorld(mWorld);
		}
		mWorld->EndPlay();
		mWorld->ClearWorld();
		DestroyWorldContext(mWorld);
		mWorld = nullptr;
	}

	mRenderingInfo = {};
	mColorTarget.reset();
	mDepthTarget.reset();
	mHDRTarget.reset();
	Super::PreExit();
}

void UExecutableEngine::RenderFrame(const FMinimalViewInfo& Camera) {
	const FMatrix ViewProjection{ Camera.GetViewProjectionMatrix() };
	const FMatrix Projection{ Camera.GetProjectionMatrix() };
	const FFrustumPlanes Frustum{ ExtractFrustumPlanes(ViewProjection) };
	FRenderer* Renderer{ GetEngineLoop().GetRenderer() };
	FLODViewContext View{ GetEngineLoop().GetViewportWidth(), GetEngineLoop().GetViewportHeight() };
	View.ViewProjection = ViewProjection;
	View.CameraPosition = Camera.GetLocation();
	View.CameraForward = Camera.GetForwardVector();
	View.ProjectionScaleSquared = std::max(Projection.M[1][0] * Projection.M[1][0], Projection.M[2][1] * Projection.M[2][1]);
	View.NearZ = Camera.GetNearZ();
	View.bOrthographic = Camera.GetIsOrthogonal();
	View.Prepare();

	mWorld->BeginRenderViews();
	FViewConstants ViewConstants{};
	ViewConstants.ViewProjection = ViewProjection;
	ViewConstants.CameraWorldPos = View.CameraPosition;
	ViewConstants.CameraForward = View.CameraForward;
	RenderShadows(Camera, View, ViewConstants);
	mWorld->GetScene().UpdateLights(Renderer);

	mRenderQueue.Reset();
	mIconQueue.Reset();
	mArrowQueue.Reset();
	mWorld->GatherRenderPackets(mRenderQueue, &View, &Frustum, Renderer, &BuildBillboardMatrix, &Camera, &mIconQueue, &mArrowQueue);
	RenderScene(Camera, ViewConstants);
	RenderPostProcess(Camera);
	RenderText(ViewProjection);
}

void UExecutableEngine::RenderShadows(const FMinimalViewInfo& Camera, const FLODViewContext& View, FViewConstants& ViewConstants) {
	FRenderer* Renderer{ GetEngineLoop().GetRenderer() };
	TArray<FProjectedShadowInfo> Shadows{};
	TArray<FFrustumPlanes> Frustums{};
	TArray<FLODViewContext> Views{};
	Frustums.Add(ExtractFrustumPlanes(View.ViewProjection));
	Views.Add(View);
	const float MinScreenRadius{ std::max(0.0f, GMinScreenRadiusForShadowCaster) };
	const float MinScreenRadiusSquared{ MinScreenRadius * MinScreenRadius };
	mWorld->GatherLocalProjectedShadows(Shadows, Frustums, Views, MinScreenRadiusSquared);

	const FDirectionalLightSceneProxy* Directional{ mWorld->GetScene().GetPrimaryDirectionalLight() };
	TArray<float> SplitDistances{};
	if (Directional && Directional->ShouldCastShadow()) {
		const UDirectionalLightComponent* Light{ static_cast<const UDirectionalLightComponent*>(Directional->GetLightComponent()) };
		if (Camera.GetIsOrthogonal()) {
			SplitDistances.Add(-Light->GetShadowDistance());
			SplitDistances.Add(Light->GetShadowDistance());
		} else {
			Directional->CalculateCascadeSplitDistances(Camera.GetNearZ(), Camera.GetFarZ(), SplitDistances);
		}

		if (SplitDistances.Num() >= 2) {
			ViewConstants.DirectionalCascadeCount = static_cast<uint32>(SplitDistances.Num() - 1);
			ViewConstants.DirectionalShadowNearDistance = SplitDistances[0];
			ViewConstants.DirectionalCascadeTransitionFraction = ViewConstants.DirectionalCascadeCount > 1 ? Light->GetCascadeTransitionFraction() : 0.0f;
			TArray<FVector> Corners{};
			Corners.Reserve(8);
			for (uint32 CascadeIndex{}; CascadeIndex < ViewConstants.DirectionalCascadeCount; ++CascadeIndex) {
				float RenderNear{ SplitDistances[CascadeIndex] };
				if (CascadeIndex > 0) {
					RenderNear -= (SplitDistances[CascadeIndex] - SplitDistances[CascadeIndex - 1]) * ViewConstants.DirectionalCascadeTransitionFraction;
				}
				Corners.Reset();
				AppendShadowFrustumCorners(Camera, RenderNear, SplitDistances[CascadeIndex + 1], Corners);
				mWorld->GatherDirectionalShadow(Shadows, Corners, View, RenderNear, SplitDistances[CascadeIndex + 1], MinScreenRadiusSquared, CascadeIndex, MainViewId, ViewConstants.DirectionalCascadeCount);
				ViewConstants.DirectionalCascadeFarDistances[CascadeIndex] = SplitDistances[CascadeIndex + 1];
			}
		}
	}

	Renderer->RenderShadowMaps(std::move(Shadows), mShadowViewRenderer);
	for (uint32 CascadeIndex{}; CascadeIndex < ViewConstants.DirectionalCascadeCount; ++CascadeIndex) {
		ViewConstants.DirectionalShadowIndices[CascadeIndex] = Renderer->GetShadowIndex(*Directional, CascadeIndex, MainViewId);
	}
}

void UExecutableEngine::RenderScene(const FMinimalViewInfo& Camera, const FViewConstants& ViewConstants) {
	FRenderer* Renderer{ GetEngineLoop().GetRenderer() };
	RenderCommand::BeginRenderPass(mRenderingInfo);
	if (!Camera.GetIsOrthogonal()) {
		mSkyboxRenderer.OnRender(ViewConstants.ViewProjection, ViewConstants.CameraWorldPos);
	}

	RenderCommand::SetRasterizerState(ERasterizerState::SolidBack);
	RenderCommand::SetBlendState(EBlendState::Opaque);
	RenderCommand::SetDepthStencilState(EDepthStencilState::Default);
	RenderCommand::SetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

	Renderer->RenderQueueSorting(mRenderQueue, ViewConstants);
	Renderer->RenderDepth(ViewConstants.ViewProjection);
	RenderCommand::EndRenderPass(mRenderingInfo);
	Renderer->CullLights(Camera.GetViewMatrix(), Camera.GetProjectionMatrix(), mRenderingInfo.ViewportSetting.Width, mRenderingInfo.ViewportSetting.Height);

	mDecalRenderer.OnRender(
		Renderer,
		mWorld->GetScene().DecalSceneProxies,
		mDepthTarget.get(),
		ViewConstants.ViewProjection,
		ViewConstants.CameraWorldPos,
		Camera.GetProjectionMatrix(),
		mRenderingInfo.ViewportSetting,
		true);
	RenderCommand::SetRasterizerState(ERasterizerState::SolidBack);

	FRenderingInfo ResumeInfo{ mRenderingInfo };
	for (FRenderingDesc& Color : ResumeInfo.ColorRenderTargets)
		Color.LoadOp = ERenderTargetLoadOp::Load;
	ResumeInfo.DepthStencil.LoadOp = ERenderTargetLoadOp::Load;
	RenderCommand::BeginRenderPass(ResumeInfo);

	Renderer->RenderOpaque(ViewConstants.ViewProjection);
	Renderer->RenderTranslucent(ViewConstants.ViewProjection);
	RenderCommand::EndRenderPass(mRenderingInfo);
	
}

void UExecutableEngine::RenderPostProcess(const FMinimalViewInfo& Camera) {
	const FFogInfo* FogInfo{ mWorld->GetScene().GetFogInfo() };
	if (!Camera.GetIsOrthogonal() && FogInfo) {
		FRenderingInfo FogPass{ mRenderingInfo };
		FogPass.ColorRenderTargets[0].LoadOp = ERenderTargetLoadOp::Load;
		FogPass.DepthStencil.Texture = nullptr;
		RenderCommand::BeginRenderPass(FogPass);
		mFogRenderer.OnRender(mDepthTarget.get(), Camera.GetViewProjectionMatrix(), Camera.GetLocation(), *FogInfo);
		RenderCommand::EndRenderPass(FogPass);
	}

	FRenderingInfo ToneMapPass{};
	ToneMapPass.ViewportSetting = mRenderingInfo.ViewportSetting;
	FRenderingDesc ColorDesc{};
	ColorDesc.Texture = mColorTarget.get();
	ColorDesc.LoadOp = ERenderTargetLoadOp::DontCare;
	ToneMapPass.ColorRenderTargets.Add(ColorDesc);
	RenderCommand::BeginRenderPass(ToneMapPass);
	const FVignetteSettings& Vignette = mWorld->GetVignetteSettings();
	mToneMapRenderer.SetVignette(
		Vignette.GetEffectiveIntensity(),
		Vignette.Radius,
		Vignette.Softness,
		Vignette.Color);
	mToneMapRenderer.OnRender(mHDRTarget.get(), EToneMapMode::ACES, ASequenceActor::GetWorldFadeAlpha(mWorld));
	RenderCommand::EndRenderPass(ToneMapPass);
	RenderCommand::UnbindShaderResource(0, EShaderBindFlagBits::Pixel);

	const FRenderingInfo& BackbufferInfo{ GetEngineLoop().GetSwapchain()->GetRenderingInfo() };
	RenderCommand::BeginRenderPass(BackbufferInfo);
	mFXAARenderer.OnRender(mColorTarget.get());
	RenderCommand::EndRenderPass(BackbufferInfo);
	RenderCommand::UnbindShaderResource(0, EShaderBindFlagBits::Pixel);
}

void UExecutableEngine::RenderText(const FMatrix& ViewProjection) {
	FRenderingInfo TextPass{ GetEngineLoop().GetSwapchain()->GetRenderingInfo() };
	TextPass.ColorRenderTargets[0].LoadOp = ERenderTargetLoadOp::Load;
	TextPass.DepthStencil.Texture = mDepthTarget.get();
	TextPass.DepthStencil.LoadOp = ERenderTargetLoadOp::Load;
	RenderCommand::BeginRenderPass(TextPass);
	for (TObjectIterator<UTextRenderComponent> TextComponent{}; TextComponent; ++TextComponent) {
		if (!TextComponent->GetFont() || !TextComponent->IsVisible() || TextComponent->GetWorld() != mWorld) {
			continue;
		}
		mTextRenderer.OnRender(TextComponent->GetText(), TextComponent->GetWorldMatrix(), TextComponent->GetTextSize(), *TextComponent->GetFont(), ViewProjection);
	}
	mUIRenderer.RenderUIImage(TextPass.ViewportSetting.Width, TextPass.ViewportSetting.Height);
	mUIRenderer.RenderUIText(TextPass.ViewportSetting.Width, TextPass.ViewportSetting.Height);
	RenderCommand::EndRenderPass(TextPass);
}

FMatrix UExecutableEngine::BuildBillboardMatrix(const FVector& Position, float Width, float Height, const void* ViewContext) {
	const FMinimalViewInfo& Camera{ *static_cast<const FMinimalViewInfo*>(ViewContext) };
	const FQuat CameraRotation{ Camera.GetRotation().Quaternion() };
	FVector Forward{ CameraRotation.RotateVector(FVector{ -1.0f, 0.0f, 0.0f }) };
	FVector Right{ CameraRotation.RotateVector(FVector{ 0.0f, 1.0f, 0.0f }) };
	FVector Up{ CameraRotation.RotateVector(FVector{ 0.0f, 0.0f, 1.0f }) };
	if (!Camera.GetIsOrthogonal()) {
		const FVector ToCamera{ Camera.GetLocation() - Position };
		if (ToCamera.SizeSquared() > 1.0e-12f) {
			Forward = ToCamera.Normalized();
		}
		const FVector ReferenceUp{ std::fabs(Forward.Z) > 0.999f ? FVector{ 0.0f, 1.0f, 0.0f } : FVector{ 0.0f, 0.0f, 1.0f } };
		Right = FVector::Cross(ReferenceUp, Forward).Normalized();
		Up = FVector::Cross(Forward, Right).Normalized();
		Right *= -1.0f;
	}
	Right *= Width;
	Up *= Height;
	return FMatrix{
		Forward.X, Forward.Y, Forward.Z, 0.0f,
		Right.X, Right.Y, Right.Z, 0.0f,
		Up.X, Up.Y, Up.Z, 0.0f,
		Position.X, Position.Y, Position.Z, 1.0f
	};
}

void UExecutableEngine::AppendShadowFrustumCorners(const FMinimalViewInfo& Camera, float NearDistance, float FarDistance, TArray<FVector>& OutCorners) {
	const FQuat CameraRotation{ Camera.GetRotation().Quaternion() };
	const FVector Forward{ CameraRotation.RotateVector(FVector{ 1.0f, 0.0f, 0.0f }).Normalized() };
	const FVector Right{ CameraRotation.RotateVector(FVector{ 0.0f, 1.0f, 0.0f }).Normalized() };
	const FVector Up{ CameraRotation.RotateVector(FVector{ 0.0f, 0.0f, 1.0f }).Normalized() };
	const float TanHalfFov{ std::tan(FMath::DegreesToRadians(Camera.GetFieldOfView()) * 0.5f) };
	for (const float Distance : { NearDistance, FarDistance }) {
		const FVector Center{ Camera.GetLocation() + Forward * Distance };
		const float HalfWidth{ Camera.GetIsOrthogonal() ? Camera.GetOrthoWidth() * 0.5f : Distance * TanHalfFov * Camera.GetAspectRatio() };
		const float HalfHeight{ HalfWidth / Camera.GetAspectRatio() };
		for (const float Horizontal : { -1.0f, 1.0f }) {
			for (const float Vertical : { -1.0f, 1.0f }) {
				OutCorners.Add(Center + Right * (HalfWidth * Horizontal) + Up * (HalfHeight * Vertical));
			}
		}
	}
}

#endif
