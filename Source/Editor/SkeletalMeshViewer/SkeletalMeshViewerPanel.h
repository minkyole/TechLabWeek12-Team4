#pragma once

#include "Editor/EditorUI/EditorPanel.h"
#include "Editor/Gizmo/Gizmo.h"
#include "Editor/Gizmo/GizmoRenderer.h"
#include "Render/RenderingInfo.h"
#include "Render/Renderer.h"
#include "Render/RenderCommand.h"
#include "Render/Mesh.h"
#include "Components/SkeletalMeshComponent.h"
#include "Core/EngineString.h"
#include "Containers/Array.h"
#include "Containers/Map.h"
#include "BoneWeightHeatmap.h"
#include <functional>
#include <utility>

#include "Render/ViewMode.h"
#include "Render/ToneMapRenderer.h"

class FGridRenderer;
class UAnimSequence;
class UAnimSingleNodeInstance;
class UAnimDataModel;
class UBlendSpace1D;

struct FAnimStateMachineAssetData;
class UAssetAnimInstance;
class UAnimGraphInstance;
struct FAnimGraphAssetData;
class FSkeletalMeshViewerPanel : public IEditorPanel
{
public:
	FSkeletalMeshViewerPanel();
	virtual ~FSkeletalMeshViewerPanel() override;

	void SetRenderer(FRenderer* InRenderer) { Renderer = InRenderer; }
	void SetGridRenderer(FGridRenderer* InGridRenderer) { GridRenderer = InGridRenderer; }
	void SetGizmoRenderer(FGizmoRenderer* InGizmoRenderer) { GizmoRenderer = InGizmoRenderer; }
	void SetToneMapRenderer(FToneMapRenderer* InToneMapRenderer) { ToneMapRenderer = InToneMapRenderer; }

	virtual bool Init() override;
	virtual void Tick(float DeltaTime) override;
	virtual void OnRender() override;
	virtual const char* GetPanelName() const override { return "Skeletal Mesh Viewer"; }

	// ImGui BeginRenderPass(메인 스왑체인) 시작 전에 호출되어 독립 렌더 타깃에 프리뷰 씬을 렌더링
    // Reusable isolated backend for animation document preview sessions.
    std::unique_ptr<FSkeletalMeshViewerPanel> CreateAnimationPreviewBackend() const;
    void DrawEmbeddedPreview();
    void HideEmbeddedPreview() { bViewportVisible=false; bViewportHovered=false; bViewportFocused=false; }
	void RenderPreview();
 bool PreviewAnimGraph(const FAnimGraphAssetData& Data, FString& Error);
 UAnimGraphInstance* GetAnimGraphPreview() const;
 void StopAnimGraphPreview();
 bool PreviewStateMachine(const FAnimStateMachineAssetData& Data, FString& Error);
 UAssetAnimInstance* GetStateMachinePreview() const;
 void StopStateMachinePreview();

	// 다중 애셋 탭 오픈 및 선택
	bool OpenAsset(const FString& Path);
	void OpenMesh(USkeletalMesh* Mesh);
	void ReleaseResources();
	void SetSelectedMesh(USkeletalMesh* Mesh);
	void FocusOnMesh();
	void ResetLighting();

	// 뷰어 패널이 마우스 조작 중이거나 호버 상태인지 확인 (메인 에디터 뷰포트 입력 간섭 차단용)
	bool IsHoveredOrInteracting() const { return bViewportHovered || bIsInteracting; }

	void RequestFocus() { bNeedFocus = true; }
	void SetOpenBlendSpaceEditorCallback(std::function<void(const FString&)> Callback) { OnOpenBlendSpaceEditor = std::move(Callback); }

	// 현재 프리뷰에서 수정한 본 Transform을 RefPose로 확정하고 .bin 파일에 저장
	bool SaveCurrentMesh();
	void ResetAllBonesToBindPose();

	// 변형된 .bin을 원본 FBX 초기 포즈로 즉시 복원
	bool RestoreToOriginalFBXPose();

	// 이름을 입력받아 새로운 .fbx.bin 에셋으로 별도 저장 및 에디터 등록
	bool SaveAsNewBin(const FString& NewAssetName);

private:
	void DrawSaveAsModal();
	void ResizeViewport(uint32 NewWidth, uint32 NewHeight);
	FBox CalculatePreviewBounds() const;
	FTransform GetBoneLocalTransform(int32 BoneIndex) const;
	UMaterial* GetPreviewMaterial(int32 SlotIndex) const;
	void SetPreviewSampler(int32 SlotIndex, ESamplerState Sampler);
	void AppendStaticPreviewPackets(FRenderQueue& Queue, const FMatrix& PreviewWorld) const;
	void UpdateCamera(float DeltaTime);
	void HandleInput(float DeltaTime);

	void DrawToolbarUI();
	void DrawAssetTabBar();

	void DrawViewportWindow(const struct ImGuiWindowClass& WindowClass);
	void DrawSkeletonTreeWindow(const struct ImGuiWindowClass& WindowClass);
	void DrawAssetDetailsWindow(const struct ImGuiWindowClass& WindowClass);
	void DrawPreviewSettingsWindow(const struct ImGuiWindowClass& WindowClass);
	void DrawBlendSpaceWindow(const struct ImGuiWindowClass& WindowClass);
	void DrawBlendSpaceGraph();
	void DrawAnimationWindow(const struct ImGuiWindowClass& WindowClass);
	void DrawPreviewTimeline(const UAnimDataModel* Model);
	void DrawMeshDetailsUI();
	void DrawBoneHierarchyUI();
	void DrawLightingSettingsUI();
	void DrawBoneNode(int32 BoneIndex, const TArray<struct FSkeletalBone>& Bones);

	// 1~3단계: 골격(Bone) 3D 메시 렌더링, 에디터 기즈모 연동, 실시간 스키닝 및 뷰포트 피킹
	void ComputeBoneWorldMatrices(TArray<FMatrix>& OutBoneWorldMatrices) const;
	void UpdateBoneInteraction();
	void Render3DBones(const FMatrix& ViewProj);
	void DrawSkeletonOverlay(const struct ImVec2& ViewportOrigin, const struct ImVec2& ViewportSize);
	bool IsBoneAncestorOf(int32 AncestorIndex, int32 TargetBoneIndex, const TArray<struct FSkeletalBone>& Bones) const;

private:
	FRenderer* Renderer = nullptr;
	FGridRenderer* GridRenderer = nullptr;
	FGizmoRenderer* GizmoRenderer = nullptr;
	FToneMapRenderer* ToneMapRenderer = nullptr;

	// 임시 프리뷰 객체는 에디터가 종료되기 전에 해제한다.
	USkeletalMeshComponent* PreviewComponent = nullptr;
	USceneComponent* BoneGizmoTarget = nullptr;
	FGizmo BoneGizmo;
	USkeletalMesh* SelectedMesh = nullptr;
	TArray<TUniquePtr<UMaterial>> PreviewMaterials;
	TArray<UStaticMesh*> VisibleStaticMeshes;
	TUniquePtr<FConstantBuffer> PreviewLighting;
	TUniquePtr<FConstantBuffer> PreviewLightGrid;
	FBox PreviewBounds{};
	float MeshRadius = 1.0f;

	// 다중 애셋 탭 목록 및 전환 대기 메시
	TArray<USkeletalMesh*> OpenedMeshes;
	USkeletalMesh* MeshToSelect = nullptr;
	FString OpenAssetMessage;

	// 렌더 타깃 및 뷰포트
	TUniquePtr<FTexture2D> ColorTarget;
	TUniquePtr<FTexture2D> HDRTarget;
	TUniquePtr<FTexture2D> DepthTarget;
	FRenderingInfo RenderingInfo{};
	uint32 ViewportWidth = 0;
	uint32 ViewportHeight = 0;
	bool bViewportVisible = false;
    bool bEmbeddedAnimationPreview = false;
	bool bFrameRequested = false;
	bool bViewportHovered = false;
	bool bIsInteracting = false;
	bool bNeedFocus = false;

	// 개별 도킹 서브 윈도우 표시 상태
	bool bViewportOpen = true;
	bool bSkeletonTreeOpen = true;
	bool bAssetDetailsOpen = true;
	bool bPreviewSettingsOpen = true;
	bool bAnimationOpen = true;
	bool bBlendSpaceOpen = true;
	bool bBlendSpaceDockInitialized = false;
	bool bViewportFocused = false;
	bool bBlendSpaceWindowFocused = false;
	bool bAnimationWasVisible = false;
	bool bBlendSpaceWasVisible = false;
	bool bResetLayout = false;

	// 레벨 에디터와 같은 공전 및 자유 이동 카메라 조작을 사용한다.
	FVector CameraLocation = FVector(-250.0f, -250.0f, 150.0f);
	FVector TargetPosition = FVector::ZeroVector;
	float CameraDistance = 250.0f;
	float CameraYaw = 45.0f;
	float CameraPitch = 20.0f;
	float CameraSpeed = 500.0f;
	float FovDegrees = 60.0f;
	FVector CachedCameraLocation = FVector::ZeroVector;
	FMatrix CachedViewMatrix = FMatrix::Identity;
	FMatrix CachedProjMatrix = FMatrix::Identity;

	// 미리보기의 방향광과 하늘광 설정이다.
	bool bEnableDirectionalLight = true;
	float LightYaw = 45.0f;
	float LightPitch = 45.0f;
	FVector LightColor = FVector(1.0f, 1.0f, 1.0f);
	float LightIntensity = 1.8f;

	bool bEnableSkyLight = true;
	FVector SkyColor = FVector(0.6f, 0.7f, 0.85f);
	FVector GroundColor = FVector(0.25f, 0.25f, 0.28f);
	float SkyLightIntensity = 1.0f;
	float PreviewExposure = 1.0f;

	// 뷰어 표시 모드. View Mode 메뉴의 라디오 그룹(하나만 선택)이며, 체크 표시는 이 값 하나로 계산한다.
	// 렌더러의 EViewMode에는 히트맵이 없으므로 GetRendererViewMode()로 변환해 넘긴다.
	enum class EViewerDisplayMode : uint8
	{
		LitBlinnPhong,
		LitLambert,
		Unlit,
		WorldNormal,
		BoneWeightHeatmap,
	};
	EViewMode GetRendererViewMode() const;
	EToneMapMode GetToneMapMode() const;
	bool IsBoneWeightHeatmapMode() const { return DisplayMode == EViewerDisplayMode::BoneWeightHeatmap; }

	// 뷰 모드 및 렌더 옵션
	EViewerDisplayMode DisplayMode = EViewerDisplayMode::LitBlinnPhong;
	bool bShowGrid = true;
	bool bWireframe = false;   // 표시 모드와 독립 토글 (히트맵 위에도 겹쳐 그릴 수 있다)
	FVector4 BackgroundClearColor = FVector4(0.12f, 0.12f, 0.12f, 1.0f); // 언리얼 기본 프리뷰 배경색

	// 골격(Bone) 표시 및 하이라이트 옵션
	bool bShowBones = true;
	bool bShowBoneGizmo = true;
	bool bDrawBoneCones = true;      // true: 입체 원뿔(Cone/Pyramid) 렌더링, false: 선(Line) 렌더링
	bool bShowAllBoneNames = false;  // 모든 본 이름 표시 여부 (선택된 본은 항상 표시)
	float BoneConeWidthRatio = 0.10f;
	float BoneJointRadius = 2.5f;

	// 본 트리 UI 및 피킹 상태
	int32 SelectedBoneIndex = -1;
	int32 HoveredBoneIndex = -1;
	bool bScrollToSelectedBone = false;
	char BoneSearchFilter[64] = "";
	char ClipSearchFilter[64] = "";

	// 저장 상태 알림 메시지 및 타이머
	FString SaveStatusMessage;
	float SaveStatusTimer = 0.0f;

	// 골격(Bone) 3D 메시 렌더링 자원
	UStaticMesh* BoneMesh = nullptr;
	UStaticMesh* BoneSphereMesh = nullptr;
	FShaderProgram* BoneShader = nullptr;
	FPipelineState BonePipelineState{};
	TUniquePtr<FConstantBuffer> BoneCB;

	ImVec2 CachedViewportOrigin{ 0.0f, 0.0f };
	ImVec2 CachedViewportSize{ 0.0f, 0.0f };

	// Save As New .bin 모달 팝업 상태
	bool bShowSaveAsModal = false;
	char SaveAsNameBuffer[128] = "";

	//BoneWeightHeatmap 관련 변수
	FBoneWeightHeatmap BoneWeightHeatmap;

	struct FPreviewClip
	{
		FString Name;
		UAnimSequence* Sequence = nullptr;
	};

	UAnimSingleNodeInstance* GetPreviewAnimInstance() const;
	void ClearPreviewClips();
	void BuildPreviewClips();
	void SelectPreviewClip(int32 ClipIndex);


	enum class EPreviewMode { None, Sequence, BlendSpace, StateMachine, AnimGraph };
	struct FPreviewPlaybackState
	{
		float NormalizedTime = 0.0f;
		float Rate = 1.0f;
		bool Playing = true;
		bool Looping = true;
	};
	EPreviewMode PreviewMode = EPreviewMode::None;
	FPreviewPlaybackState SequencePlayback, BlendSpacePlayback;
	UBlendSpace1D* SelectedBlendSpace = nullptr; // Registered asset, non-owning.
	float BlendSpaceInput = 0.0f;
	FString BlendSpaceMessage;
	std::function<void(const FString&)> OnOpenBlendSpaceEditor;
	void CapturePreviewPlayback();
	void ActivatePreviewMode(EPreviewMode Mode);
	void ResetPreviewModes();
	bool HasActivePreview() const;
	bool IsBlendSpacePlayable() const;
	void SelectPreviewBlendSpace(UBlendSpace1D* Asset);
	void ApplyBlendSpaceInput(float Value);
	void RefreshPreviewPose();
	bool HasPreviewClip() const;
	bool BlockEditDuringAnimationPreview();
	bool IsPreviewAtEnd() const;
	void UpdatePreviewPlaybackEnd();
	void TogglePreviewPlayback();
	int32 GetPreviewCurrentFrame() const;
	void ScrubPreviewToTime(float Seconds);
	void ApplyPreviewPlayRate();
	void PlayPreviewInDirection(int32 Direction);
	void StepPreviewFrame(int32 Delta);
	void SetPreviewPlaySpeed(float Speed);
	void HandlePreviewShortcuts();

	void DrawNotifyProperties(const UAnimDataModel* Model);
	void DrawNotifyObjectProperties(int32 Index, class UAnimNotify* NotifyObject);
	void SelectPreviewNotify(int32 Index);
	TArray<FString> CollectUsedNotifyNames() const;
	bool DrawAudioFilePicker(const char* Folder, FString& InOutPath);
	void RefreshAudioFileList(const char* Folder);
	TArray<FString> AudioFileList;

	UAnimSequence* GetSelectedPreviewSequence() const;
	int32 AddPreviewNotify(float Time, float Duration);
	void RemovePreviewNotify(int32 Index);
	void SetPreviewNotifyTime(int32 Index, float Time, bool bSnapToFrame);
	int32 SortPreviewNotifies(int32 KeepIndex);
	void SetPreviewNotifyDuration(int32 Index, float Duration);
	void RenamePreviewNotify(int32 Index, const FString& Name);
	void SetPreviewNotifyClass(int32 Index, class UClass* NotifyClass);
	void OnPreviewNotifyPropertyChanged(int32 Index, const struct FProperty& Property);
	bool SavePreviewNotifies();

	struct FFiredPreviewNotify
	{
		FAnimNotifyEvent Event;
		FString Label;
		double FiredAt = 0.0;
	};

	void HandlePreviewNotify(const FAnimNotifyEvent& Event, UAnimSequenceBase* Sequence);
	void HandlePreviewNotifyStateBegin(const FAnimNotifyEvent& Event, UAnimSequenceBase* Sequence);
	void HandlePreviewNotifyStateEnd(const FAnimNotifyEvent& Event, UAnimSequenceBase* Sequence);
	void AddFiredPreviewNotify(const FAnimNotifyEvent& Event, const FString& Label);
	bool IsPreviewNotifyFlashing(const FAnimNotifyEvent& Event) const;
	void DrawFiredNotifies();

	uint32 PreviewNotifyHandle = 0;
	uint32 PreviewNotifyStateBeginHandle = 0;
	uint32 PreviewNotifyStateEndHandle = 0;
	TArray<FFiredPreviewNotify> FiredNotifies;
	std::unique_ptr<class FCameraShakePattern> PreviewShake;
	float PreviewShakeScale = 1.0f;
	bool bPreviewCameraShake = true;

	int32 SelectedNotifyIndex = -1;
	bool bDraggingNotify = false;
	float NotifyDragOffset = 0.0f;
	int32 NotifyPopupIndex = -1;
	float NotifyPopupTime = 0.0f;
	char NotifyNameBuffer[64] = "";
	bool bNotifyDirty = false;

	TArray<FPreviewClip> PreviewClips;
	int32 SelectedClipIndex = -1; // -1 = None
	int32 PreviewPlayDirection = 1;
	float PreviewPlaySpeed = 1.0f;
	float PreviewCustomSpeed = 1.0f;
	bool bAnimationWindowFocused = false;
};
