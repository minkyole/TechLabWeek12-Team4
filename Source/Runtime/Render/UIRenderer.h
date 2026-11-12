#pragma once

#include "EnginePCH.h"
#include "Text/TextRenderer.h"
#include "Text/Font.h"
#include "Render/MinimapRenderer.h"

struct UITextInfo {
	float mTextSize{};
	FString mText{};
	UFont* mAtlas{};
	// 화면의 비율 좌표(0~1), UI 자체의 정렬 기준점(0~1).
	FVector2 mAnchor{};
	FVector2 mAlignment{};
	FVector2 mOffset{}; // 앵커에서의 픽셀 여백
};

struct UIImageInfo {
	FVector2 mSize{};
	UTexture2D* mTexture{};
	FVector2 mAnchor{};
	FVector2 mAlignment{};
	FVector2 mOffset{};
	float mRatio{ 1.0f }; // 0~1, 왼쪽부터 표시할 비율
};

class FUIRenderer {
public:
	void Init(FTextRenderer& TextRender);
	void DrawUIText(const UITextInfo& TextInfo, float Width, float Height);
	// 그리기 패스 안에서 호출한다. 좌표 원점은 화면 왼쪽 위다.
	void RenderUIText(float Width, float Height);
	void DrawUIImage(const UIImageInfo& ImageInfo, float Width, float Height);
	void RenderUIImage(float Width, float Height);
	void AddUIText(UITextInfo TextInfo);
	void AddUIImage(UIImageInfo ImageInfo);
	void AddUIMinimap(const FUIMinimapInfo& Info);
	void ResetUIText();
	void ResetUIImage();

private:
	FTextRenderer* mTextRenderer{};
	FPipelineState mUIPipeline{};
	FPipelineState mUIImagePipeline{};
	TUniquePtr<FConstantBuffer> mUIImageConstants{};
	TArray<UITextInfo> mUITextArray{};
	TArray<UIImageInfo> mUIImageArray{};
	FMinimapRenderer mMinimapRenderer{};
};
