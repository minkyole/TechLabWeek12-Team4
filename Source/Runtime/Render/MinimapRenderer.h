#pragma once

#include "EnginePCH.h"
#include "Render/Buffer.h"
#include "Render/PipelineState.h"

struct FUIMinimapInfo {
	FVector2 mPlayerCell{};
	FVector2 mOffset{};
	float mCellSize{ 1.0f };
	float mRadius{ 5.0f };
	float mSize{ 184.0f };
	float mViewYaw{};
	float mVerticalFieldOfView{ 60.0f };
	uint32 mColumnCount{};
	uint32 mRowCount{};
	uint32 mBlockedRows[32]{};
};

class FMinimapRenderer {
public:
	void Init();
	void AddMinimap(const FUIMinimapInfo& Info);
	void Render(float Width, float Height);
	void Reset();

private:
	void Draw(const FUIMinimapInfo& Info, float Width, float Height);

private:
	FPipelineState mPipeline{};
	TUniquePtr<FConstantBuffer> mConstants{};
	TArray<FUIMinimapInfo> mMinimaps{};
};
