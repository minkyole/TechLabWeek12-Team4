#pragma once
#include "Editor/EditorUI/EditorPanel.h"
#include "Animation/BlendSpace.h"
#include <memory>
#include <vector>

class FBlendSpaceEditorPanel : public IEditorPanel
{
public:
	FBlendSpaceEditorPanel();
	~FBlendSpaceEditorPanel() override;
	bool Init() override { SetOpen(false); return true; }
	void Tick(float) override {}
	void OnRender() override;
	const char* GetPanelName() const override { return "Blend Space Editor"; }
	void OpenAsset(const FString& Path);
	// Must run before the engine's global UObject teardown; safe to call again.
	void ReleaseResources();
	void SetOpen(bool Open) override;
	bool IsKeyboardFocused() const { return IsOpen() && KeyboardFocused; }
private:
	struct FSampleRow { int Id; FBlendSample Sample; };
	void LoadPending();
	bool Save();
	bool CommitRows(std::vector<FSampleRow> Rows);
	void Select(int Id);
	int SelectedIndex() const;
	void AddAnimation(UAnimSequence* Animation, float X);
	void DrawAssetDetails();
	void DrawGraph();
	void DrawSampleDetails();
	void DrawAssetBrowser();
	void DrawUnsavedDialog();
	std::unique_ptr<UBlendSpace1D> Working;
	std::vector<FSampleRow> Rows;
	FString Path, PendingPath, Error;
	char AxisName[256] = {};
	float AxisMin = 0, AxisMax = 600, EditX = 0;
	int NextId = 1, SelectedId = 0, DragId = 0;
	float DragX = 0;
	bool Dirty = false, PendingClose = false, GraphFocused = false;
	bool KeyboardFocused = false;
	ImGuiTextFilter Search;
	float DetailsHeight = 180;
};
