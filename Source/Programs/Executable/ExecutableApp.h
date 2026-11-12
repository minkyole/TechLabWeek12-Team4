#pragma once

#ifdef EXECUTABLE

#include "Engine/GameEngine.h"
#include "Render/FogRenderer.h"
#include "Render/DecalRenderer.h"
#include "Render/FShadowViewRenderer.h"
#include "Render/FXAARenderer.h"
#include "Render/SkyboxRenderer.h"
#include "Render/ToneMapRenderer.h"
#include "Render/UIRenderer.h"
#include "Text/TextRenderer.h"

struct FLODViewContext;
struct FMinimalViewInfo;

class UExecutableEngine : public UGameEngine {
	DECLARE_CLASS(UExecutableEngine, UGameEngine)

public:
	FEngineConfig GetConfig() const override;
	bool Init() override;
	void Tick(float DeltaTime) override;
	void OnResize(uint32 Width, uint32 Height) override;
	void PreExit() override;
	FUIRenderer* GetUIRenderer() override { return &mUIRenderer; }

private:
	void RenderFrame(const FMinimalViewInfo& Camera);
	void RenderShadows(const FMinimalViewInfo& Camera, const FLODViewContext& View, FViewConstants& ViewConstants);
	void RenderScene(const FMinimalViewInfo& Camera, const FViewConstants& ViewConstants);
	void RenderPostProcess(const FMinimalViewInfo& Camera);
	void RenderText(const FMatrix& ViewProjection);
	static FMatrix BuildBillboardMatrix(const FVector& Position, float Width, float Height, const void* ViewContext);
	static void AppendShadowFrustumCorners(const FMinimalViewInfo& Camera, float NearDistance, float FarDistance, TArray<FVector>& OutCorners);

private:
	UWorld* mWorld{};
	FSkyboxRenderer mSkyboxRenderer{};
	FDecalRenderer mDecalRenderer;
	FFogRenderer mFogRenderer{};
	FShadowViewRenderer mShadowViewRenderer{};
	FToneMapRenderer mToneMapRenderer{};
	FFXAARenderer mFXAARenderer{};
	FTextRenderer mTextRenderer{};
	FUIRenderer mUIRenderer{};
	TUniquePtr<FTexture2D> mColorTarget{};
	TUniquePtr<FTexture2D> mDepthTarget{};
	TUniquePtr<FTexture2D> mHDRTarget{};
	FRenderingInfo mRenderingInfo{};
	FRenderQueue mRenderQueue{};
	FRenderQueue mIconQueue{};
	FRenderQueue mArrowQueue{};
};

#endif
