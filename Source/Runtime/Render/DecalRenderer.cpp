#include "EnginePCH.h"

#include "DecalRenderer.h"
#include "RenderCommand.h"
#include "RenderResourceManager.h"
#include "Renderer.h"
#include "Asset/AssetManager.h"
#include "Core/Stats/DecalStats.h"
#include "Render/DecalSettings.h"
#include "Math/Frustum.h"


bool FDecalRenderer::Init(FRenderer* InRenderer)
{

	Shader = FRenderResourceManager::GetShaderProgram("Resources/Shader/DecalShader.hlsl");
	if (!Shader)
	{
		HTR_LOG(Error, "[Decal] shader not found");
		return false;
	}


	ConstantBuffer = RenderCommand::CreateConstantBuffer(sizeof(FDecalConstants));

	PipelineState.Shader = Shader;
	PipelineState.Topology = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	PipelineState.BlendState = EBlendState::AlphaBlend;
	PipelineState.DepthStencilState = EDepthStencilState::Disabled;
	PipelineState.RasterizerState = ERasterizerState::SolidFront;

	AdditivePipelineState = PipelineState;
	AdditivePipelineState.BlendState = EBlendState::Additive;

	ProjectedShader = FRenderResourceManager::GetShaderProgram("Resources/Shader/ProjectedDecalShader.hlsl");
	if (!ProjectedShader)
	{
		return false;
	}

	// 투영 메시는 표면 바깥을 향해 감겨 있어 앞면만 그린다.
	// 깊이 비교는 셰이더에서 하므로 DBuffer와 크기가 다를 수 있는 DSV를 묶지 않는다.
	ProjectedPipelineState = PipelineState;
	ProjectedPipelineState.Shader = ProjectedShader;
	ProjectedPipelineState.RasterizerState = ERasterizerState::SolidBack;

	ProjectedAdditivePipelineState = ProjectedPipelineState;
	ProjectedAdditivePipelineState.BlendState = EBlendState::Additive;

	return true;
}

float FDecalRenderer::CalculateScreenFadeAlpha(
	const FDecalSceneProxy* Proxy,
	const FVector& CameraPosition,
	const FMatrix& Projection,
	int32 ViewWidth)
{
	const float FadeScreenSize = Proxy->GetFadeScreenSize();
	const bool bOrthographic = Projection.M[3][3] != 0.0f;
	if (FadeScreenSize == 0.0f || bOrthographic)
	{
		return (1.0f);
	}

	const FMatrix& DecalToWorld = Proxy->GetDecalToWorld();

	const FVector DecalOrigin = DecalToWorld.TransformPosition(FVector::ZeroVector);
	const float Distance = FVector::Distance(CameraPosition, DecalOrigin);

	// 박스 중심부터 각 면까지의 거리
	const float ScaleX = FVector(DecalToWorld.M[0][0], DecalToWorld.M[0][1], DecalToWorld.M[0][2]).Size();
	const float ScaleY = FVector(DecalToWorld.M[1][0], DecalToWorld.M[1][1], DecalToWorld.M[1][2]).Size();
	const float ScaleZ = FVector(DecalToWorld.M[2][0], DecalToWorld.M[2][1], DecalToWorld.M[2][2]).Size();
	const float Radius = std::max({ ScaleX, ScaleY, ScaleZ });

	const float CurrentScreenSize = Radius / Distance;

	const float FOVFactor = ((2.0f / Projection.M[1][0]) / ViewWidth) * 600.0f;
	const float FadeCoefficient = FadeScreenSize * FOVFactor;
	const float FadeRange = FadeCoefficient * 0.5f;

	return (std::clamp((CurrentScreenSize - FadeCoefficient) / FadeRange, 0.0f, 1.0f));
}

void FDecalRenderer::DrawDecals(
	const TArray<FDecalSceneProxy*>& SortDecals,
	FDecalConstants& DecalConstants,
	const FMatrix& ViewProjection,
	const FVector& CameraPosition,
	const FMatrix& Projection,
	int32 ViewWidth,
	bool bAdditive,
	EDecalMode Mode)
{
	const bool bProjected = Mode == EDecalMode::Projected;

	for (FDecalSceneProxy* DecalSceneProxy : SortDecals)
	{
		if (DecalSceneProxy->IsAdditive() != bAdditive)
		{
			continue;
		}

		// 닿는 표면이 없어 만든 정점이 없으면 그릴 것이 없다.
		if (bProjected && DecalSceneProxy->GetProjectedVertexCount() == 0)
		{
			continue;
		}

		const float ScreenAlpha = CalculateScreenFadeAlpha(DecalSceneProxy, CameraPosition, Projection, ViewWidth);
		if (DecalSceneProxy->GetFadeAlpha() * ScreenAlpha <= 0.0f)
		{
			continue;
		}


		// 투영 메시 정점은 이미 월드 좌표다.
		DecalConstants.DecalToClip = bProjected ? ViewProjection : DecalSceneProxy->GetDecalToWorld() * ViewProjection;
		DecalConstants.DecalInfo.WorldToDecal = DecalSceneProxy->GetWorldToDecal();
		DecalConstants.DecalInfo.DecalColor = DecalSceneProxy->GetDecalColor();
		DecalConstants.DecalInfo.DecalColor.A *= DecalSceneProxy->GetFadeAlpha() * ScreenAlpha;


		// Additive Decal은 빛만 더하므로 법선을 바꾸지 않는다.
		UTexture2D* NormalTexture = bAdditive ? nullptr : DecalSceneProxy->GetDecalNormalTexture();
		DecalConstants.DecalInfo.bHasNormalTexture = NormalTexture ? 1.0f : 0.0f;

		RenderCommand::BindShaderResource(1, DecalSceneProxy->GetDecalTexture(), EShaderBindFlagBits::Pixel);
		if (NormalTexture)
		{
			RenderCommand::BindShaderResource(2, NormalTexture, EShaderBindFlagBits::Pixel);
		}
		RenderCommand::UpdateBufferData(ConstantBuffer.get(), &DecalConstants);
		RenderCommand::BindConstantBuffer(0, ConstantBuffer.get(), EShaderBindFlagBits::Pixel | EShaderBindFlagBits::Vertex);
		if (bProjected)
		{
			RenderCommand::BindVertexBuffer(DecalSceneProxy->GetProjectedVertexBuffer());
			RenderCommand::Draw(DecalSceneProxy->GetProjectedVertexCount());
		}
		else
		{
			RenderCommand::DrawIndexed(36);
		}
		FStatRegistry::AddCounter(bAdditive ? DecalStats::STAT_AdditiveDrawCalls : DecalStats::STAT_DrawCalls);
	}
}

void FDecalRenderer::DrawScreenSpace(
	const TArray<FDecalSceneProxy*>& SortDecals,
	FDecalConstants& DecalConstants,
	FTexture2D* DepthTexture,
	const FMatrix& ViewProjection,
	const FVector& CameraPosition,
	const FMatrix& Projection,
	const FViewportSettings& Viewport,
	bool bAdditive)
{
	RenderCommand::BindPipelineState(bAdditive ? AdditivePipelineState : PipelineState);
	RenderCommand::BindSamplerState(0, ESamplerState::LinearClamp, EShaderBindFlagBits::Pixel);
	RenderCommand::BindShaderResource(0, DepthTexture, EShaderBindFlagBits::Pixel);
	UStaticMesh* Cube = UAssetManager::GetAssetByPath<UStaticMesh>("Cube");
	RenderCommand::BindMesh(Cube);

	DrawDecals(SortDecals, DecalConstants, ViewProjection, CameraPosition, Projection, Viewport.Width, bAdditive, EDecalMode::ScreenSpace);
}

void FDecalRenderer::DrawProjected(
	const TArray<FDecalSceneProxy*>& SortDecals,
	FDecalConstants& DecalConstants,
	FTexture2D* DepthTexture,
	const FMatrix& ViewProjection,
	const FVector& CameraPosition,
	const FMatrix& Projection,
	const FViewportSettings& Viewport,
	bool bAdditive)
{
	RenderCommand::BindPipelineState(bAdditive ? ProjectedAdditivePipelineState : ProjectedPipelineState);
	RenderCommand::BindSamplerState(0, ESamplerState::LinearClamp, EShaderBindFlagBits::Pixel);
	RenderCommand::BindShaderResource(0, DepthTexture, EShaderBindFlagBits::Pixel);

	DrawDecals(SortDecals, DecalConstants, ViewProjection, CameraPosition, Projection, Viewport.Width, bAdditive, EDecalMode::Projected);
}

void FDecalRenderer::OnRender(
	FRenderer* Renderer,
	const TArray<FDecalSceneProxy*>& Decals,
	FTexture2D* DepthTexture,
	const FMatrix& ViewProjection,
	const FVector& CameraPosition,
	const FMatrix& Projection,
	const FViewportSettings& Viewport,
	bool bShowDecals)
{
	SCOPE_CYCLE_COUNTER(DecalStats::STAT_RenderCPU);
	FStatRegistry::SetCounter(DecalStats::STAT_Total, Decals.Num());

	if (!Renderer->EnsureDBuffer(Viewport.Width, Viewport.Height) || !Renderer->EnsureAdditiveBuffer(Viewport.Width, Viewport.Height)
		|| !Renderer->EnsureNormalDBuffer(Viewport.Width, Viewport.Height))
	{
		return;
	}

	FRenderingInfo DecalPass{};
	DecalPass.ViewportSetting = Viewport;
	FRenderingDesc ColorDesc{};
	ColorDesc.Texture = Renderer->GetDBuffer();
	ColorDesc.ClearValue.ColorClearValue = FVector4(0, 0, 0, 0);
	DecalPass.ColorRenderTargets.Add(ColorDesc);

	// Additive 패스는 법선을 쓰지 않으므로 Normal DBuffer를 넣기 전에 복사한다.
	FRenderingInfo AdditivePass = DecalPass;
	AdditivePass.ColorRenderTargets[0].Texture = Renderer->GetAdditiveBuffer();

	// 같은 Decal 패스에서 MRT로 색과 법선을 함께 쓴다. 알파 0으로 비워 Decal이 없는 곳은 법선을 바꾸지 않는다.
	FRenderingDesc NormalDesc = ColorDesc;
	NormalDesc.Texture = Renderer->GetNormalDBuffer();
	DecalPass.ColorRenderTargets.Add(NormalDesc);

	//정렬 순서 SortOrder → 텍스처 포인터 → 프록시 주소

	// 화면 밖 Decal은 픽셀은 클리핑되어도 Draw 호출과 상수 버퍼 갱신 비용이 남으므로 미리 거른다.
	// Bounds는 Decal OBB를 감싸는 월드 AABB라 SSD와 투영 모두 같은 기준으로 쓸 수 있다.
	const FFrustumPlanes Frustum = ExtractFrustumPlanes(ViewProjection);
	TArray<FDecalSceneProxy*> SortDecals;
	SortDecals.Reserve(Decals.Num());
	for (FDecalSceneProxy* Proxy : Decals)
	{
		if (IsAABBInFrustum(Proxy->GetBounds(), Frustum))
		{
			SortDecals.Add(Proxy);
		}
	}
	FStatRegistry::AddCounter(DecalStats::STAT_Visible, SortDecals.Num());

	std::sort(SortDecals.begin(), SortDecals.end(), [](const FDecalSceneProxy* A, const FDecalSceneProxy* B) {
		if (A->GetSortOrder() != B->GetSortOrder())
		{
			return (A->GetSortOrder() < B->GetSortOrder());
		}
		if (A->GetDecalTexture() != B->GetDecalTexture())
		{
			return (A->GetDecalTexture() < B->GetDecalTexture());
		}
		return (A < B);
		});

	FDecalConstants DecalConstants = {};

	DecalConstants.InverseViewProjection = ViewProjection.Inverse();    // 루프 밖에서 한 번만 계산
	DecalConstants.DecalInfo.ViewSize = FVector2(static_cast<float>(Viewport.Width), static_cast<float>(Viewport.Height));
	DecalConstants.DecalInfo.NormalThreshold = FDecalSettings::GetNormalThreshold();

	const EDecalMode Mode = FDecalSettings::GetMode();

	if (Mode == EDecalMode::Projected)
	{
		uint64 ProjectedVertices = 0;
		for (const FDecalSceneProxy* Proxy : Decals)
		{
			ProjectedVertices += Proxy->GetProjectedVertexCount();
		}
		FStatRegistry::SetCounter(DecalStats::STAT_ProjectedVertices, ProjectedVertices);
	}

	// Show Flag가 꺼져도 패스는 실행해 버퍼를 비운다. 건너뛰면 UberLit이 지난 값을 읽는다.
	RenderCommand::BeginRenderPass(DecalPass);
	if (bShowDecals)
	{
		if (Mode == EDecalMode::ScreenSpace)
			DrawScreenSpace(SortDecals, DecalConstants, DepthTexture, ViewProjection, CameraPosition, Projection, Viewport, false);
		else
			DrawProjected(SortDecals, DecalConstants, DepthTexture, ViewProjection, CameraPosition, Projection, Viewport, false);
	}
	RenderCommand::EndRenderPass(DecalPass);

	// Additive Decal이 없어도 지난 프레임 값이 남지 않게 패스는 항상 실행해 비운다.
	RenderCommand::BeginRenderPass(AdditivePass);
	if (bShowDecals)
	{
		if (Mode == EDecalMode::ScreenSpace)
			DrawScreenSpace(SortDecals, DecalConstants, DepthTexture, ViewProjection, CameraPosition, Projection, Viewport, true);
		else
			DrawProjected(SortDecals, DecalConstants, DepthTexture, ViewProjection, CameraPosition, Projection, Viewport, true);
	}
	RenderCommand::EndRenderPass(AdditivePass);

	RenderCommand::UnbindShaderResource(0, EShaderBindFlagBits::Pixel);
	RenderCommand::UnbindShaderResource(1, EShaderBindFlagBits::Pixel);
	RenderCommand::UnbindShaderResource(2, EShaderBindFlagBits::Pixel);
}
