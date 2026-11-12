#include "EnginePCH.h"
#include "MinimapRenderer.h"

#include "Render/RenderCommand.h"
#include "Render/RenderResourceManager.h"

#include <algorithm>
#include <cmath>

namespace {
	struct FMinimapConstants {
		FVector2 mClipOrigin{};
		FVector2 mClipSize{};
		FVector2 mPlayerCell{};
		float mRadius{};
		float mCellSize{};
		FVector2 mViewDirection{};
		float mCosHalfFieldOfView{};
		float mPixelDiameter{};
		uint32 mColumnCount{};
		uint32 mRowCount{};
		float mPadding[2]{};
		uint32 mBlockedRows[32]{};
	};
	static_assert(sizeof(FMinimapConstants) == 192);
}

void FMinimapRenderer::Init() {
	mPipeline.Shader = FRenderResourceManager::GetShaderProgram("Resources/Shader/MinimapShader.hlsl");
	mPipeline.RasterizerState = ERasterizerState::SolidNone;
	mPipeline.BlendState = EBlendState::AlphaBlend;
	mPipeline.DepthStencilState = EDepthStencilState::Disabled;
	mConstants = RenderCommand::CreateConstantBuffer(sizeof(FMinimapConstants));
}

void FMinimapRenderer::AddMinimap(const FUIMinimapInfo& Info) {
	if (!std::isfinite(Info.mCellSize) || !std::isfinite(Info.mRadius) || !std::isfinite(Info.mSize) || !std::isfinite(Info.mViewYaw) || !std::isfinite(Info.mVerticalFieldOfView) || !std::isfinite(Info.mPlayerCell.X) || !std::isfinite(Info.mPlayerCell.Y) || !std::isfinite(Info.mOffset.X) || !std::isfinite(Info.mOffset.Y) || Info.mCellSize <= 0.0f || Info.mRadius <= 0.0f || Info.mSize <= 0.0f || Info.mColumnCount == 0 || Info.mColumnCount > 32 || Info.mRowCount == 0 || Info.mRowCount > 32) {
		return;
	}
	mMinimaps.Add(Info);
}

void FMinimapRenderer::Render(float Width, float Height) {
	if (Width > 0.0f && Height > 0.0f && mPipeline.Shader && mConstants) {
		for (const FUIMinimapInfo& Info : mMinimaps) {
			Draw(Info, Width, Height);
		}
	}
	Reset();
}

void FMinimapRenderer::Reset() {
	mMinimaps.Reset();
}

void FMinimapRenderer::Draw(const FUIMinimapInfo& Info, float Width, float Height) {
	const float Size{ std::min({ Info.mSize, Width, Height }) };
	const FVector2 Offset{ std::clamp(Info.mOffset.X, 0.0f, Width - Size), std::clamp(Info.mOffset.Y, 0.0f, Height - Size) };
	const float HalfVerticalFieldOfView{ FMath::DegreesToRadians(std::clamp(Info.mVerticalFieldOfView, 1.0f, 179.0f)) * 0.5f };
	const float HalfHorizontalFieldOfView{ std::atan(std::tan(HalfVerticalFieldOfView) * Width / Height) };
	FMinimapConstants Constants{};
	Constants.mClipOrigin = FVector2{ 2.0f * Offset.X / Width - 1.0f, 1.0f - 2.0f * Offset.Y / Height };
	Constants.mClipSize = FVector2{ 2.0f * Size / Width, -2.0f * Size / Height };
	Constants.mPlayerCell = Info.mPlayerCell;
	Constants.mRadius = Info.mRadius;
	Constants.mCellSize = Info.mCellSize;
	Constants.mViewDirection = FVector2{ std::sin(Info.mViewYaw), -std::cos(Info.mViewYaw) };
	Constants.mCosHalfFieldOfView = std::cos(HalfHorizontalFieldOfView);
	Constants.mPixelDiameter = Size;
	Constants.mColumnCount = Info.mColumnCount;
	Constants.mRowCount = Info.mRowCount;
	std::copy(std::begin(Info.mBlockedRows), std::end(Info.mBlockedRows), std::begin(Constants.mBlockedRows));

	RenderCommand::BindPipelineState(mPipeline);
	RenderCommand::UpdateBufferData(mConstants.get(), &Constants);
	RenderCommand::BindConstantBuffer(0, mConstants.get(), EShaderBindFlagBits::Vertex | EShaderBindFlagBits::Pixel);
	RenderCommand::BindVertexBuffer(nullptr);
	RenderCommand::BindIndexBuffer(nullptr);
	RenderCommand::Draw(6);
}
