#include "EnginePCH.h"
#include "Animation/AssetAnimInstance.h"
#include "Animation/AnimGraph.h"
#include "SkeletalMeshViewerPanel.h"
#include "Asset/AssetManager.h"
#include "Camera/MinimalViewInfo.h"
#include "Collision/Ray.h"
#include "Engine/SceneManagement.h"
#include "Editor/Rendering/GridRenderer.h"
#include "Input/InputSystem.h"
#include "Render/RenderCommand.h"
#include "Math/EngineMath.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/UObjectIterator.h"

#include "imgui.h"
#include "imgui_internal.h"
#include "Render/Material.h"
#include "Render/RenderResourceManager.h"
#include "Components/SkinnedMeshComponent.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimSequence.h"
#include "Animation/BlendSpace.h"
#include "Core/Misc/FileHelper.h"
#include "Animation/AnimNotifyFile.h"
#include "Animation/AnimNotifies/AnimNotify.h"
#include "Animation/AnimNotifies/AnimNotify_CameraShake.h"
#include "Camera/CameraModifier_CameraShake.h"
#include "Engine/Engine.h"
#include "Audio/AudioSystem.h"
#include "Camera/Shake/CameraShakePattern.h"
#include "UObject/UObjectHash.h"
#include "UObject/UObjectGlobals.h"
#include "Core/Misc/FileHelper.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <functional>

namespace
{
	float SRGBToLinear(float C)
	{
		C = std::clamp(C, 0.0f, 1.0f);
		return C <= 0.04045f ? C / 12.92f : std::pow((C + 0.055f) / 1.055f, 2.4f);
	}

	FVector SRGBToLinear(const FVector& C)
	{
		return FVector(SRGBToLinear(C.X), SRGBToLinear(C.Y), SRGBToLinear(C.Z));
	}

	FAnimNotifyEvent* FindPreviewNotify(UAnimSequence* Sequence, int32 Index)
	{
		if (!Sequence || Index < 0 || Index >= Sequence->Notifies.Num())
		{
			return nullptr;
		}
		return &Sequence->Notifies[Index];
	}

	FString GetNotifyLabel(const FAnimNotifyEvent& Notify)
	{
		return Notify.Notify ? Notify.Notify->GetNotifyName() : Notify.NotifyName.ToString();
	}

	bool IsNotifyStateEvent(const FAnimNotifyEvent& Notify)
	{
		return Notify.IsStateEvent();
	}

	bool IsNotifyStateClass(const UClass* Class)
	{
		return Class && Class->IsChildOf(UAnimNotifyState::StaticClass());
	}

	bool CanHaveNotifyDuration(const FAnimNotifyEvent& Notify)
	{
		return Notify.Notify == nullptr || IsNotifyStateClass(Notify.Notify->GetClass());
	}

	float SnapNotifyTimeToFrame(const UAnimSequence& Sequence, float Time)
	{
		const UAnimDataModel* Model = Sequence.GetDataModel();
		const float FPS = Model ? Model->GetFrameRate().AsDecimal() : 0.0f;
		if (FPS <= 0.0f)
		{
			return Time;
		}
		return std::round(Time * FPS) / FPS;
	}
}

FSkeletalMeshViewerPanel::FSkeletalMeshViewerPanel()
{
	ResetLighting();
}

FSkeletalMeshViewerPanel::~FSkeletalMeshViewerPanel()
{
	ReleaseResources();
}

bool FSkeletalMeshViewerPanel::Init()
{
	PreviewComponent = NewObject<USkeletalMeshComponent>();
	PreviewComponent->SetAnimationMode(EAnimationMode::AnimationSingleNode);
	PreviewNotifyHandle = PreviewComponent->OnAnimNotify.AddDynamic(this, &FSkeletalMeshViewerPanel::HandlePreviewNotify);
	PreviewNotifyStateBeginHandle = PreviewComponent->OnAnimNotifyStateBegin.AddDynamic(this, &FSkeletalMeshViewerPanel::HandlePreviewNotifyStateBegin);
	PreviewNotifyStateEndHandle = PreviewComponent->OnAnimNotifyStateEnd.AddDynamic(this, &FSkeletalMeshViewerPanel::HandlePreviewNotifyStateEnd);
	BoneGizmoTarget = NewObject<USceneComponent>();
	PreviewComponent->SetFlags(EObjectFlags::RF_Transient);
	BoneGizmoTarget->SetFlags(EObjectFlags::RF_Transient);
	BoneGizmo.SetMode(EGizmoMode::Rotation);
	BoneGizmo.SetSpace(EGizmoSpace::Local);

	// 기본 뷰포트 크기 초기화
	ResizeViewport(800, 600);

	// 골격(Bone) 3D 메시 렌더링 자원 준비
	BoneMesh = UAssetManager::GetAssetByPath<UStaticMesh>("OctahedralBone");
	BoneSphereMesh = UAssetManager::GetAssetByPath<UStaticMesh>("GizmoSphere");
	BoneCB = RenderCommand::CreateConstantBuffer(sizeof(FGizmoData));
	BoneShader = FRenderResourceManager::GetShaderProgram("Resources/Shader/GizmoShader.hlsl");
	BonePipelineState.Shader = BoneShader;
	BonePipelineState.Topology = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;

	// 기본 상태: 닫혀 있음 (Window 메뉴 또는 Details 패널의 Edit 버튼으로 오픈)
	SetOpen(false);

	return true;
}

bool FSkeletalMeshViewerPanel::OpenAsset(const FString& Path)
{
	const FFbxAssetSet* Assets = UAssetManager::Get().LoadFbxAssets(Path);
	if (!Assets)
	{
		OpenAssetMessage = "Could not open FBX asset: " + Path;
		SetOpen(true);
		RequestFocus();
		return false;
	}
	if (Assets->SkeletalMesh)
	{
		OpenMesh(Assets->SkeletalMesh);
		return true;
	}

	OpenAssetMessage = "Imported " + std::to_string(Assets->StaticMeshes.Num()) +
		" static mesh assets. This FBX has no skeletal mesh to preview.";
	SetOpen(true);
	RequestFocus();
	return true;
}

void FSkeletalMeshViewerPanel::ReleaseResources()
{
	ResetPreviewModes();
	BoneGizmo.SetTarget(nullptr, false);
	ClearPreviewClips();
	SelectedClipIndex = -1;
	if (PreviewComponent)
	{
		PreviewComponent->OnAnimNotify.RemoveHandler(PreviewNotifyHandle);
		PreviewComponent->OnAnimNotifyStateBegin.RemoveHandler(PreviewNotifyStateBeginHandle);
		PreviewComponent->OnAnimNotifyStateEnd.RemoveHandler(PreviewNotifyStateEndHandle);
	}
	PreviewNotifyHandle = 0;
	PreviewNotifyStateBeginHandle = 0;
	PreviewNotifyStateEndHandle = 0;
	FiredNotifies.Empty();
	PreviewShake.reset();
	delete PreviewComponent;
	PreviewComponent = nullptr;
	delete BoneGizmoTarget;
	BoneGizmoTarget = nullptr;
	PreviewMaterials.Reset();
	VisibleStaticMeshes.Reset();
	PreviewLighting.reset();
	PreviewLightGrid.reset();
	BoneCB.reset();
	BoneMesh = nullptr;
	BoneSphereMesh = nullptr;
	BoneShader = nullptr;
	OpenedMeshes.Reset();
	SelectedMesh = nullptr;
	MeshToSelect = nullptr;
	RenderingInfo = {};
	ColorTarget.reset();
	HDRTarget.reset();
	DepthTarget.reset();
}

void FSkeletalMeshViewerPanel::OpenMesh(USkeletalMesh* Mesh)
{
	if (!Mesh)
		return;

	OpenAssetMessage.clear();
	SetOpen(true);
	RequestFocus();

	// 이미 열려 있는지 검사
	bool bFound = false;
	for (USkeletalMesh* Opened : OpenedMeshes)
	{
		if (Opened == Mesh)
		{
			bFound = true;
			break;
		}
	}

	if (!bFound)
	{
		OpenedMeshes.Add(Mesh);
	}

	MeshToSelect = Mesh;
	SetSelectedMesh(Mesh);
}

void FSkeletalMeshViewerPanel::SetSelectedMesh(USkeletalMesh* Mesh)
{
	ResetPreviewModes();
	if (SelectedMesh != Mesh)
	{
		PreviewMaterials.Reset();
		if (Mesh) PreviewMaterials.SetNum(Mesh->Materials.Num());
		SelectedMesh = Mesh;
		PreviewBounds = CalculatePreviewBounds();
	}

	if (PreviewComponent)
	{
		PreviewComponent->SetSkeletalMesh(Mesh);
		PreviewComponent->SetRelativeLocation(FVector::ZeroVector);
		PreviewComponent->SetRelativeRotation(FRotator(0.0f, 0.0f, 0.0f));
	}

	SelectedBoneIndex = -1;
	HoveredBoneIndex = -1;
	BoneGizmo.SetTarget(nullptr, false);
	bFrameRequested = Mesh != nullptr;
	FocusOnMesh();

	// 메시가 바뀌면 AnimInstance가 새로 만들어지므로 클립과 선택을 다시 맞춘다.
	BuildPreviewClips();
	SelectPreviewClip(-1);
}

FBox FSkeletalMeshViewerPanel::CalculatePreviewBounds() const
{
	FBox Bounds{FVector(FLT_MAX, FLT_MAX, FLT_MAX), FVector(-FLT_MAX, -FLT_MAX, -FLT_MAX)};
	if (!SelectedMesh) return Bounds;
	for (const FSkeletalMesh& Part : SelectedMesh->MeshData.Meshes)
	{
		for (const FSkeletalVertex& Vertex : Part.Vertices)
		{
			const FVector Position = Part.MeshTransform.TransformPosition(Vertex.Vertex.Position);
			Bounds.Expand(FBox{Position, Position});
		}
	}
	return Bounds;
}

FTransform FSkeletalMeshViewerPanel::GetBoneLocalTransform(int32 BoneIndex) const
{
	if (PreviewComponent)
	{
		FTransform Transform;
		if (PreviewComponent->GetBoneLocalTransform(BoneIndex, Transform))
			return Transform;
	}
	return FTransform{};
}

UMaterial* FSkeletalMeshViewerPanel::GetPreviewMaterial(int32 SlotIndex) const
{
	if (PreviewMaterials.IsValidIndex(SlotIndex) && PreviewMaterials[SlotIndex])
		return PreviewMaterials[SlotIndex].get();
	return SelectedMesh && SelectedMesh->Materials.IsValidIndex(SlotIndex)
		? SelectedMesh->GetMaterial(SlotIndex) : nullptr;
}

void FSkeletalMeshViewerPanel::SetPreviewSampler(int32 SlotIndex, ESamplerState Sampler)
{
	if (!SelectedMesh || !PreviewMaterials.IsValidIndex(SlotIndex)) return;
	UMaterial* Source = SelectedMesh->GetMaterial(SlotIndex);
	if (!Source) return;
	auto& Material = PreviewMaterials[SlotIndex];
	if (Source->SamplerState == Sampler)
	{
		Material.reset();
		return;
	}
	if (!Material)
	{
		Material.reset(UMaterial::CreateInstance(Source));
		if (!Material || (Source->ParamBuffer && !Material->ParamBuffer))
		{
			Material.reset();
			return;
		}
		Material->SetFlags(EObjectFlags::RF_Transient);
	}
	Material->SamplerState = Sampler;
}

bool FSkeletalMeshViewerPanel::SaveCurrentMesh()
{
	if (BlockEditDuringAnimationPreview())
	{
		return false;
	}

	if (!SelectedMesh)
	{
		SaveStatusMessage = "No skeletal mesh selected.";
		SaveStatusTimer = 3.0f;
		return false;
	}

	// 1. PreviewComponent에서 수정한 본의 현재 LocalTransform들을 SelectedMesh->MeshData.Bones에 반영
	// 주의: Mesh.Vertices는 원본 FBX 바인드 공간 정점이므로 InverseBindPoseMat은 원본을 유지해야
	// ResetPoseToBindPose() -> UpdateSkinning() 시 정점들이 수정된 본 위치를 따라 스키닝된다.
	if (PreviewComponent)
	{
		const int32 NumBones = SelectedMesh->MeshData.Bones.Num();
		for (int32 BoneIdx = 0; BoneIdx < NumBones; ++BoneIdx)
		{
			FTransform CurTf;
			if (PreviewComponent->GetBoneLocalTransform(BoneIdx, CurTf))
			{
				SelectedMesh->MeshData.Bones[BoneIdx].RefPoseTransform = CurTf;
			}
		}
	}

	// 2. AssetManager를 통해 .bin 파일 저장 및 씬 컴포넌트(레벨 에디터 및 프리뷰) 즉시 갱신
	if (UAssetManager::Get().SaveSkeletalMesh(SelectedMesh))
	{
		if (PreviewComponent)
		{
			PreviewComponent->ResetPoseToBindPose();
		}
		PreviewBounds = CalculatePreviewBounds();
		SaveStatusMessage = "Saved to .bin successfully!";
		SaveStatusTimer = 3.5f;
		return true;
	}
	else
	{
		SaveStatusMessage = "Failed to save to .bin.";
		SaveStatusTimer = 3.5f;
		return false;
	}
}

void FSkeletalMeshViewerPanel::ResetAllBonesToBindPose()
{
	if (BlockEditDuringAnimationPreview())
	{
		return;
	}

	if (PreviewComponent)
	{
		PreviewComponent->ResetPoseToBindPose();
		TArray<FMatrix> BoneMatrices;
		ComputeBoneWorldMatrices(BoneMatrices);
		if (SelectedBoneIndex >= 0 && SelectedBoneIndex < BoneMatrices.Num() && BoneGizmoTarget)
		{
			const FMatrix& BoneMat = BoneMatrices[SelectedBoneIndex];
			const FVector BoneWorldPos(BoneMat.M[3][0], BoneMat.M[3][1], BoneMat.M[3][2]);
			FMatrix BoneRotMat = FMatrix::Identity;
			BoneRotMat.SetAxis(0, FVector(BoneMat.M[0][0], BoneMat.M[0][1], BoneMat.M[0][2]).GetSafeNormal());
			BoneRotMat.SetAxis(1, FVector(BoneMat.M[1][0], BoneMat.M[1][1], BoneMat.M[1][2]).GetSafeNormal());
			BoneRotMat.SetAxis(2, FVector(BoneMat.M[2][0], BoneMat.M[2][1], BoneMat.M[2][2]).GetSafeNormal());

			BoneGizmoTarget->SetRelativeLocation(BoneWorldPos);
			BoneGizmoTarget->SetRelativeRotation(MatrixToRotator(BoneRotMat));
			BoneGizmoTarget->SetRelativeScale3D(GetBoneLocalTransform(SelectedBoneIndex).Scale);
		}
		SaveStatusMessage = "Reset all bones to bind pose.";
		SaveStatusTimer = 2.5f;
	}
}

bool FSkeletalMeshViewerPanel::RestoreToOriginalFBXPose()
{
	if (BlockEditDuringAnimationPreview())
	{
		return false;
	}

	if (!SelectedMesh)
	{
		SaveStatusMessage = "No skeletal mesh selected.";
		SaveStatusTimer = 3.0f;
		return false;
	}

	if (UAssetManager::Get().RestoreSkeletalMeshToOriginalFBX(SelectedMesh))
	{
		if (PreviewComponent)
		{
			PreviewComponent->ResetPoseToBindPose();
		}
		PreviewBounds = CalculatePreviewBounds();
		SaveStatusMessage = "Restored to original FBX pose successfully!";
		SaveStatusTimer = 3.5f;
		return true;
	}
	else
	{
		SaveStatusMessage = "Cannot restore: Original .fbx file does not exist.";
		SaveStatusTimer = 3.5f;
		return false;
	}
}

bool FSkeletalMeshViewerPanel::SaveAsNewBin(const FString& NewAssetName)
{
	if (BlockEditDuringAnimationPreview())
	{
		return false;
	}

	if (!SelectedMesh || NewAssetName.empty())
	{
		SaveStatusMessage = "Asset name cannot be empty.";
		SaveStatusTimer = 3.0f;
		return false;
	}

	// 1. 현재 프리뷰에서 변형된 본 로컬 트랜스폼을 적용한 복사본 MeshData 준비
	FSkeletalMeshData NewMeshData = SelectedMesh->MeshData;
	if (PreviewComponent)
	{
		const int32 NumBones = NewMeshData.Bones.Num();
		for (int32 BoneIdx = 0; BoneIdx < NumBones; ++BoneIdx)
		{
			FTransform CurTf;
			if (PreviewComponent->GetBoneLocalTransform(BoneIdx, CurTf))
			{
				NewMeshData.Bones[BoneIdx].RefPoseTransform = CurTf;
			}
		}
	}

	// 2. AssetManager를 통해 새 .fbx.bin 파일 저장 및 에셋 등록
	USkeletalMesh* NewMesh = UAssetManager::Get().SaveSkeletalMeshAsNewBin(SelectedMesh, NewAssetName, NewMeshData);
	if (NewMesh)
	{
		OpenMesh(NewMesh);
		SaveStatusMessage = "Saved and loaded new asset: " + NewMesh->GetPath();
		SaveStatusTimer = 4.0f;
		return true;
	}
	else
	{
		SaveStatusMessage = "Failed to save new .fbx.bin asset.";
		SaveStatusTimer = 3.5f;
		return false;
	}
}

void FSkeletalMeshViewerPanel::DrawSaveAsModal()
{
	if (bShowSaveAsModal)
	{
		ImGui::OpenPopup("Save As New .bin##Modal");
		bShowSaveAsModal = false;

		if (SelectedMesh)
		{
			std::filesystem::path Path(SelectedMesh->GetPath());
			FString Stem = Path.stem().string();
			if (Stem.ends_with(".fbx")) Stem = Stem.substr(0, Stem.length() - 4);
			Stem += "_Modified";
			strcpy_s(SaveAsNameBuffer, sizeof(SaveAsNameBuffer), Stem.c_str());
		}
		else
		{
			strcpy_s(SaveAsNameBuffer, sizeof(SaveAsNameBuffer), "NewSkeletalMesh");
		}
	}

	if (ImGui::BeginPopupModal("Save As New .bin##Modal", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
	{
		ImGui::Text("Enter new asset name:");
		ImGui::Spacing();
		ImGui::SetNextItemWidth(350.0f);
		if (ImGui::InputText("##NewBinNameInput", SaveAsNameBuffer, sizeof(SaveAsNameBuffer), ImGuiInputTextFlags_EnterReturnsTrue))
		{
			if (strlen(SaveAsNameBuffer) > 0)
			{
				SaveAsNewBin(SaveAsNameBuffer);
				ImGui::CloseCurrentPopup();
			}
		}
		ImGui::TextDisabled("Will be saved as: %s.fbx.bin", SaveAsNameBuffer);
		ImGui::Spacing();
		ImGui::Separator();
		ImGui::Spacing();

		if (ImGui::Button("Save", ImVec2(120.0f, 0.0f)))
		{
			if (strlen(SaveAsNameBuffer) > 0)
			{
				SaveAsNewBin(SaveAsNameBuffer);
				ImGui::CloseCurrentPopup();
			}
		}
		ImGui::SameLine();
		if (ImGui::Button("Cancel", ImVec2(120.0f, 0.0f)))
		{
			ImGui::CloseCurrentPopup();
		}
		ImGui::EndPopup();
	}
}

void FSkeletalMeshViewerPanel::FocusOnMesh()
{
	if (!SelectedMesh)
	{
		TargetPosition = FVector::ZeroVector;
		CameraDistance = 250.0f;
		CameraSpeed = 500.0f;
		CameraYaw = 45.0f;
		CameraPitch = 20.0f;
		const FRotator Rot(CameraPitch, CameraYaw, 0.0f);
		CameraLocation = TargetPosition - Rot.Quaternion().GetForwardVector() * CameraDistance;
		return;
	}

	FBox Bounds = PreviewBounds;
	if (!VisibleStaticMeshes.IsEmpty())
	{
		if (const FFbxAssetSet* Assets = UAssetManager::Get().FindFbxAssets(SelectedMesh->GetPath()))
		{
			for (const FFbxStaticAsset& Part : Assets->StaticMeshes)
			{
				if (Part.Mesh && VisibleStaticMeshes.Find(Part.Mesh) != INDEX_NONE)
					Bounds.Expand(Part.Mesh->GetRenderBounds().GetWorldAABB(Part.NodeTransform));
			}
		}
	}
	if (Bounds.Min.X <= Bounds.Max.X)
	{
		TargetPosition = (Bounds.Min + Bounds.Max) * 0.5f;
		const FVector Extent = (Bounds.Max - Bounds.Min) * 0.5f;
		MeshRadius = std::max(Extent.Length(), 0.001f);
		const float Aspect = ViewportHeight > 0 ? static_cast<float>(ViewportWidth) / ViewportHeight : 1.0f;
		const float HalfFov = FMath::DegreesToRadians(FovDegrees) * 0.5f;
		const float HalfHorizontalFov = std::atan(std::tan(HalfFov) * Aspect);
		CameraDistance = MeshRadius / std::sin(std::min(HalfFov, HalfHorizontalFov)) * 1.1f;
		CameraSpeed = MeshRadius * 2.5f;
	}
	else
	{
		TargetPosition = FVector::ZeroVector;
		CameraDistance = 250.0f;
		CameraSpeed = 500.0f;
	}

	const FRotator Rot(CameraPitch, CameraYaw, 0.0f);
	CameraLocation = TargetPosition - Rot.Quaternion().GetForwardVector() * CameraDistance;
}

void FSkeletalMeshViewerPanel::ResetLighting()
{
	bEnableDirectionalLight = true;
	LightYaw = 45.0f;
	LightPitch = 45.0f;
	LightColor = FVector(1.0f, 1.0f, 1.0f);
	LightIntensity = 1.8f;

	bEnableSkyLight = true;
	SkyColor = FVector(0.6f, 0.7f, 0.85f);
	GroundColor = FVector(0.25f, 0.25f, 0.28f);
	SkyLightIntensity = 1.0f;

	BackgroundClearColor = FVector4(0.12f, 0.12f, 0.12f, 1.0f);
	bShowGrid = true;
	bWireframe = false;
	DisplayMode = EViewerDisplayMode::LitBlinnPhong;
}

EViewMode FSkeletalMeshViewerPanel::GetRendererViewMode() const
{
	switch (DisplayMode)
	{
	case EViewerDisplayMode::LitLambert:        return EViewMode::Lit_Lambert;
	case EViewerDisplayMode::Unlit:             return EViewMode::Unlit;
	case EViewerDisplayMode::WorldNormal:       return EViewMode::WorldNormal;
	// 히트맵은 PS 앞부분에서 바로 반환하므로 조명 계산이 가장 가벼운 Unlit 변형을 쓴다.
	case EViewerDisplayMode::BoneWeightHeatmap: return EViewMode::Unlit;
	case EViewerDisplayMode::LitBlinnPhong:
	default:                                    return EViewMode::Lit_BlinnPhong;
	}
}

EToneMapMode FSkeletalMeshViewerPanel::GetToneMapMode() const
{
	switch (DisplayMode)
	{
	case EViewerDisplayMode::Unlit:             return EToneMapMode::EncodeOnly;
	case EViewerDisplayMode::BoneWeightHeatmap: return EToneMapMode::EncodeOnly;
	case EViewerDisplayMode::WorldNormal:       return EToneMapMode::Passthrough;
	case EViewerDisplayMode::LitLambert:		return EToneMapMode::ACES;
	case EViewerDisplayMode::LitBlinnPhong:		return EToneMapMode::ACES;
	default:                                    return EToneMapMode::ACES;
	}
}

void FSkeletalMeshViewerPanel::ResizeViewport(uint32 NewWidth, uint32 NewHeight)
{
	NewWidth = std::max(1u, NewWidth);
	NewHeight = std::max(1u, NewHeight);

	if (ColorTarget && DepthTarget && HDRTarget &&
		std::abs(static_cast<int32>(ViewportWidth) - static_cast<int32>(NewWidth)) <= 2 &&
		std::abs(static_cast<int32>(ViewportHeight) - static_cast<int32>(NewHeight)) <= 2)
	{
		return;
	}

	ViewportWidth = NewWidth;
	ViewportHeight = NewHeight;

	D3D11_TEXTURE2D_DESC Desc{};
	Desc.Width = ViewportWidth;
	Desc.Height = ViewportHeight;
	Desc.MipLevels = 1;
	Desc.ArraySize = 1;
	Desc.SampleDesc.Count = 1;
	Desc.Usage = D3D11_USAGE_DEFAULT;

	// 색상 렌더 타깃
	Desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	Desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
	ColorTarget = RenderCommand::CreateTexture2D(Desc);

	Desc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
	Desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
	HDRTarget = RenderCommand::CreateTexture2D(Desc);
	
	// 깊이 렌더 타깃
	Desc.Format = DXGI_FORMAT_R24G8_TYPELESS;
	Desc.BindFlags = D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE;
	DepthTarget = RenderCommand::CreateTexture2D(Desc);


	RenderingInfo.ColorRenderTargets.Reset();
	RenderingInfo.ViewportSetting.StartX = 0;
	RenderingInfo.ViewportSetting.StartY = 0;
	RenderingInfo.ViewportSetting.Width = ViewportWidth;
	RenderingInfo.ViewportSetting.Height = ViewportHeight;
	RenderingInfo.ViewportSetting.MinDepth = 0.0f;
	RenderingInfo.ViewportSetting.MaxDepth = 1.0f;

	FRenderingDesc ColorDesc{};
	ColorDesc.Texture = ColorTarget.get();
	ColorDesc.LoadOp = ERenderTargetLoadOp::Clear;
	ColorDesc.StoreOp = ERenderTargetStoreOp::Store;
	ColorDesc.ClearValue.ColorClearValue = BackgroundClearColor;
	RenderingInfo.ColorRenderTargets.Add(ColorDesc);

	RenderingInfo.DepthStencil.Texture = DepthTarget.get();
	RenderingInfo.DepthStencil.LoadOp = ERenderTargetLoadOp::Clear;
	RenderingInfo.DepthStencil.StoreOp = ERenderTargetStoreOp::Store;
	RenderingInfo.DepthStencil.ClearValue.DepthClearValue = 1.0f;
	RenderingInfo.DepthStencil.ClearValue.StencilClearValue = 0;
}

void FSkeletalMeshViewerPanel::Tick(float DeltaTime)
{
	if (!IsOpen())
		return;

	if (SaveStatusTimer > 0.0f)
	{
		SaveStatusTimer -= DeltaTime;
		if (SaveStatusTimer <= 0.0f)
		{
			SaveStatusMessage.clear();
		}
	}

	if (PreviewMode == EPreviewMode::BlendSpace)
	{
		if (!IsBlendSpacePlayable())
		{
			BlendSpaceMessage = "BlendSpace is empty or incompatible. Select a valid asset to preview.";
			ActivatePreviewMode(EPreviewMode::None);
			BlendSpacePlayback.Playing = false;
		}
		else if (auto* Instance = GetPreviewAnimInstance())
		{
			const auto& Axis = *SelectedBlendSpace->GetBlendParameter(0);
			BlendSpaceInput = std::clamp(BlendSpaceInput, Axis.Min, Axis.Max);
			Instance->SetBlendSpaceInput(BlendSpaceInput);
		}
	}
	if (PreviewComponent && HasActivePreview() && bViewportOpen && bViewportVisible)
	{
		PreviewComponent->TickComponent(std::min(DeltaTime, 0.1f));
		UpdatePreviewPlaybackEnd();
	}

	if (!bEmbeddedAnimationPreview) HandlePreviewShortcuts();
	HandleInput(DeltaTime);
	UpdateCamera(DeltaTime);
	if (!bEmbeddedAnimationPreview) UpdateBoneInteraction();
}

void FSkeletalMeshViewerPanel::HandleInput(float DeltaTime)
{
	ImGuiIO& IO = ImGui::GetIO();

	const bool bRightMouseDown = ImGui::IsMouseDown(ImGuiMouseButton_Right);
	const bool bMiddleMouseDown = ImGui::IsMouseDown(ImGuiMouseButton_Middle);
	const bool bLeftMouseDown = ImGui::IsMouseDown(ImGuiMouseButton_Left);

	if (bViewportHovered && (bRightMouseDown || bMiddleMouseDown || bLeftMouseDown))
	{
		bIsInteracting = true;
	}
	else if (!bRightMouseDown && !bMiddleMouseDown && !bLeftMouseDown)
	{
		bIsInteracting = false;
	}

	if (!bViewportHovered && !bIsInteracting)
		return;

	// 1. 단축키 'F': 포커스 맞추기, W/E/R/Space: 기즈모 모드 전환, Ctrl+S: 저장 (우클릭 카메라 조작 중이 아닐 때)
	if (!IO.WantTextInput && !bRightMouseDown && !IO.KeyAlt)
	{
		if (!bEmbeddedAnimationPreview && IO.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_S, false))
		{
			SaveCurrentMesh();
		}
		if (ImGui::IsKeyPressed(ImGuiKey_F))
		{
			FocusOnMesh();
		}
		if (ImGui::IsKeyPressed(ImGuiKey_W))
		{
			BoneGizmo.SetMode(EGizmoMode::Location);
		}
		if (ImGui::IsKeyPressed(ImGuiKey_E))
		{
			BoneGizmo.SetMode(EGizmoMode::Rotation);
		}
		if (ImGui::IsKeyPressed(ImGuiKey_R))
		{
			BoneGizmo.SetMode(EGizmoMode::Scale);
		}
		if (!HasActivePreview() && ImGui::IsKeyPressed(ImGuiKey_Space))
		{
			const EGizmoMode Cur = BoneGizmo.GetMode();
			if (Cur == EGizmoMode::Location) BoneGizmo.SetMode(EGizmoMode::Rotation);
			else if (Cur == EGizmoMode::Rotation) BoneGizmo.SetMode(EGizmoMode::Scale);
			else BoneGizmo.SetMode(EGizmoMode::Location);
		}
	}

	// 2. 단축키 'L' + 좌클릭 드래그: 실시간 라이트 회전
	const bool bHoldingL = ImGui::IsKeyDown(ImGuiKey_L);
	if (bHoldingL && bLeftMouseDown)
	{
		LightYaw += IO.MouseDelta.x * 0.5f;
		LightPitch = std::clamp(LightPitch + IO.MouseDelta.y * 0.5f, -89.0f, 89.0f);
		return;
	}

	// 3. 우클릭으로 1인칭 카메라를 이동하고 시점을 회전한다.
	if (bRightMouseDown && !IO.KeyAlt)
	{
		CameraYaw += IO.MouseDelta.x * 0.35f;
		CameraPitch = std::clamp(CameraPitch + IO.MouseDelta.y * 0.35f, -89.0f, 89.0f);

		const FRotator CurrentRot(CameraPitch, CameraYaw, 0.0f);
		const FQuat CurrentQuat(CurrentRot);
		const FVector CurrentForward = CurrentQuat.GetForwardVector();
		const FVector CurrentRight = CurrentQuat.GetRightVector();
		const FVector CurrentUp = FVector(0.0f, 0.0f, 1.0f);

		FVector MoveDir = FVector::ZeroVector;
		if (ImGui::IsKeyDown(ImGuiKey_W)) MoveDir += CurrentForward;
		if (ImGui::IsKeyDown(ImGuiKey_S)) MoveDir -= CurrentForward;
		if (ImGui::IsKeyDown(ImGuiKey_D)) MoveDir += CurrentRight;
		if (ImGui::IsKeyDown(ImGuiKey_A)) MoveDir -= CurrentRight;
		if (ImGui::IsKeyDown(ImGuiKey_E)) MoveDir += CurrentUp;
		if (ImGui::IsKeyDown(ImGuiKey_Q)) MoveDir -= CurrentUp;

		if (MoveDir.SizeSquared() > 0.0001f)
		{
			const float SpeedMultiplier = IO.KeyShift ? 2.5f : 1.0f;
			const FVector DeltaPos = MoveDir.GetSafeNormal() * (CameraSpeed * SpeedMultiplier * DeltaTime);
			CameraLocation += DeltaPos;
		}

		TargetPosition = CameraLocation + CurrentForward * CameraDistance;
	}
	// 4. Alt + 마우스 조작
	else if (IO.KeyAlt)
	{
		if (bLeftMouseDown)
		{
			CameraYaw += IO.MouseDelta.x * 0.35f;
			CameraPitch = std::remainder(CameraPitch + IO.MouseDelta.y * 0.35f, 360.0f);

			const FRotator CurrentRot(CameraPitch, CameraYaw, 0.0f);
			const FQuat CurrentQuat(CurrentRot);
			CameraLocation = TargetPosition - CurrentQuat.GetForwardVector() * CameraDistance;
		}
		else if (bRightMouseDown)
		{
			const float ZoomDelta = (IO.MouseDelta.x - IO.MouseDelta.y) * std::max(CameraDistance * 0.005f, MeshRadius * 0.0001f);
			CameraDistance = std::clamp(CameraDistance - ZoomDelta, MeshRadius * 0.05f, MeshRadius * 1000.0f);

			const FRotator CurrentRot(CameraPitch, CameraYaw, 0.0f);
			const FQuat CurrentQuat(CurrentRot);
			CameraLocation = TargetPosition - CurrentQuat.GetForwardVector() * CameraDistance;
		}
	}

	// 5. 휠 클릭 드래그(미들 버튼): 카메라 팬
	if (bMiddleMouseDown)
	{
		const FRotator CurrentRot(CameraPitch, CameraYaw, 0.0f);
		const FQuat CurrentQuat(CurrentRot);
		const FVector CurrentRight = CurrentQuat.GetRightVector();
		const FVector CurrentUp = FVector(0.0f, 0.0f, 1.0f);

		const float PanSpeed = std::max(CameraDistance * 0.0015f, 0.005f);
		const FVector PanDelta = (-CurrentRight * (IO.MouseDelta.x * PanSpeed)) + (-CurrentUp * (IO.MouseDelta.y * PanSpeed));
		CameraLocation += PanDelta;
		TargetPosition += PanDelta;
	}

	// 6. 마우스 휠로 카메라를 앞뒤로 이동해 확대하거나 축소한다.
	if (std::abs(IO.MouseWheel) > 0.01f)
	{
		const float Step = std::max(CameraDistance * 0.15f, MeshRadius * 0.005f);
		CameraDistance -= IO.MouseWheel * Step;

		const FRotator CurrentRot(CameraPitch, CameraYaw, 0.0f);
		const FQuat CurrentQuat(CurrentRot);
		const FVector CurrentForward = CurrentQuat.GetForwardVector();

		CameraDistance = std::clamp(CameraDistance, MeshRadius * 0.05f, MeshRadius * 1000.0f);
		CameraLocation = TargetPosition - CurrentForward * CameraDistance;
	}
}

void FSkeletalMeshViewerPanel::UpdateCamera(float DeltaTime)
{
	CachedCameraLocation = CameraLocation;

	FMinimalViewInfo ViewInfo;
	ViewInfo.SetLocation(CachedCameraLocation);
	ViewInfo.SetRotation(FRotator(CameraPitch, CameraYaw, 0.0f));
	ViewInfo.SetFieldOfView(FovDegrees);
	ViewInfo.SetAspectRatio((ViewportHeight > 0) ? (static_cast<float>(ViewportWidth) / static_cast<float>(ViewportHeight)) : (16.0f / 9.0f));
	ViewInfo.SetNearZ(std::max(MeshRadius * 0.001f, 0.00001f));
	ViewInfo.SetFarZ(std::max(CameraDistance, (TargetPosition - CameraLocation).Length()) + MeshRadius * 100.0f);

	if (PreviewShake)
	{
		FCameraShakeUpdateParams Params;
		Params.DeltaTime = DeltaTime;
		Params.POV = ViewInfo;
		FCameraShakeUpdateResult Result;
		PreviewShake->UpdateShakePattern(Params, Result);
		UCameraModifier_CameraShake::ApplyResult(Result, PreviewShakeScale, ECameraShakePlaySpace::CameraLocal, FRotator{}, ViewInfo);
		if (PreviewShake->IsFinished())
		{
			PreviewShake.reset();
		}
	}

	CachedViewMatrix = ViewInfo.GetViewMatrix();
	CachedProjMatrix = ViewInfo.GetPerspectiveMatrix();
}

void FSkeletalMeshViewerPanel::AppendStaticPreviewPackets(FRenderQueue& Queue, const FMatrix& PreviewWorld) const
{
	if (VisibleStaticMeshes.IsEmpty()) return;
	const FFbxAssetSet* Assets = UAssetManager::Get().FindFbxAssets(SelectedMesh->GetPath());
	if (!Assets) return;

	for (const FFbxStaticAsset& Part : Assets->StaticMeshes)
	{
		UStaticMesh* Mesh = Part.Mesh;
		if (!Mesh || VisibleStaticMeshes.Find(Mesh) == INDEX_NONE) continue;
		const FIndexBuffer* IndexBuffer = Mesh->GetIndexBuffer(0);
		if (!Mesh->GetVertexBuffer(0) || !IndexBuffer) continue;

		// FBX 노드의 원래 배치에 프리뷰 컴포넌트의 월드 변환을 적용한다.
		const FMatrix* Model = Queue.StoreWorldMatrix(Part.NodeTransform * PreviewWorld);
		const float CameraDistance = (Model->TransformPosition(FVector::ZeroVector) - CachedCameraLocation).Length();
		const uint32 TotalIndices = IndexBuffer->GetIndexCount();
		for (const FStaticMeshSection& Section : Mesh->GetMeshData().Sections)
		{
			if (Section.IndexCount == 0 || Section.StartIndex > TotalIndices ||
				Section.IndexCount > TotalIndices - Section.StartIndex)
				continue;
			UMaterial* Material = Mesh->GetMaterial(Section.MaterialSlotIndex);
			if (!Material) continue;

			FRenderPacket& Packet = Queue.AddDefaulted_GetRef();
			Packet.Model = Model;
			Packet.Mesh = Mesh;
			Packet.Material = Material;
			Packet.StartIndex = Section.StartIndex;
			Packet.IndexCount = Section.IndexCount;
			Packet.CameraToParticleDistance = CameraDistance;
		}
	}
}

void FSkeletalMeshViewerPanel::RenderPreview()
{
	if (!IsOpen() || !bViewportVisible || !SelectedMesh)
		return;

	if (!Renderer || !ToneMapRenderer || !ColorTarget || !DepthTarget || !HDRTarget || ViewportWidth == 0 || ViewportHeight == 0)
		return;

	UpdateCamera(0.0f);

	const FMatrix ViewProj = CachedViewMatrix * CachedProjMatrix;

	RenderingInfo.ColorRenderTargets[0].ClearValue.ColorClearValue = BackgroundClearColor;

	FRenderingInfo SceneInfo = RenderingInfo;
	SceneInfo.ColorRenderTargets[0].Texture = HDRTarget.get();
	RenderCommand::BeginRenderPass(SceneInfo);

	FLightingConstants LightingConstants{};
	if (bEnableSkyLight)
	{
		LightingConstants.Ambient.Color = SRGBToLinear(SkyColor);
		LightingConstants.Ambient.Intensity = SkyLightIntensity;
	}
	else
	{
		LightingConstants.Ambient.Color = FVector::ZeroVector;
		LightingConstants.Ambient.Intensity = 0.0f;
	}

	if (bEnableDirectionalLight)
	{
		const FRotator LightRot(LightPitch, LightYaw, 0.0f);
		const FQuat LightQuat(LightRot);
		LightingConstants.Directional.Direction = LightQuat.GetForwardVector().GetSafeNormal();
		LightingConstants.Directional.Color = SRGBToLinear(LightColor);
		LightingConstants.Directional.Intensity = LightIntensity;
		LightingConstants.Directional.SourceAngle = 0.0f;
	}
	else
	{
		LightingConstants.Directional.Direction = FVector(0, 0, -1);
		LightingConstants.Directional.Color = FVector::ZeroVector;
		LightingConstants.Directional.Intensity = 0.0f;
	}
	LightingConstants.NumLocalLights = 0;

	if (!PreviewLighting) PreviewLighting = RenderCommand::CreateConstantBuffer(sizeof(FLightingConstants));
	if (!PreviewLightGrid) PreviewLightGrid = RenderCommand::CreateConstantBuffer(sizeof(FLightGridConstants));
	const EViewMode PreviousViewMode = Renderer->GetViewMode();
	Renderer->SetViewMode(GetRendererViewMode());
	RenderCommand::SetRasterizerState(bWireframe ? ERasterizerState::Wireframe : ERasterizerState::SolidBack);

	if (PreviewComponent)
	{
		FRenderQueue RenderQueue;
		const FMatrix PreviewWorld = PreviewComponent->GetWorldMatrix();
		PreviewComponent->SyncSkinningMode();
		const bool bGPUSkinned = PreviewComponent->UsesGPUSkinning();
		uint32 BoneWeightHeatmapStatus = FBoneWeightHeatmap::StatusOff;
		if (IsBoneWeightHeatmapMode())
		{
			BoneWeightHeatmapStatus = BoneWeightHeatmap.Update(SelectedMesh, SelectedBoneIndex)
				? FBoneWeightHeatmap::StatusOn : FBoneWeightHeatmap::StatusNoSelection;
		}

		FVertexBuffer* VertexBuffer = bGPUSkinned ? SelectedMesh->VertexBuffer.get() : PreviewComponent->GetSkinnedVertexBuffer();
		FIndexBuffer* IndexBuffer = SelectedMesh->IndexBuffer.get();
		if (VertexBuffer && IndexBuffer)
		{
			const FMatrix* Model = RenderQueue.StoreWorldMatrix(PreviewWorld);
			uint32 BaseIndex = 0;
			uint32 BaseMaterial = 0;
			for (const FSkeletalMesh& Part : SelectedMesh->MeshData.Meshes)
			{
				const uint32 PartStartIndex = BaseIndex;
				const uint32 PartStartMaterial = BaseMaterial;
				const uint32 PartIndexCount = static_cast<uint32>(Part.Indices.Num());
				BaseIndex += PartIndexCount;
				BaseMaterial += static_cast<uint32>(Part.MaterialSlots.Num());

				const FVector PartWorldOrigin = Model->TransformPosition(Part.MeshTransform.TransformPosition(FVector::ZeroVector));
				for (const FSkeletalMeshSection& Section : Part.Sections)
				{
					if (Section.IndexCount == 0 || Section.StartIndex > PartIndexCount ||
						Section.IndexCount > PartIndexCount - Section.StartIndex)
						continue;
					UMaterial* Material = GetPreviewMaterial(static_cast<int32>(PartStartMaterial + Section.MaterialSlotIndex));
					if (!Material) continue;

					FRenderPacket& Packet = RenderQueue.AddDefaulted_GetRef();
					Packet.Model = Model;
					Packet.VertexBuffer = VertexBuffer;
					Packet.IndexBuffer = IndexBuffer;
					if (bGPUSkinned)
					{
						Packet.SkinMatrices = PreviewComponent->GetSkinMatrixBuffer();
						Packet.NormalMatrices = PreviewComponent->GetNormalMatrixBuffer();
					}
					Packet.Material = Material;
					Packet.StartIndex = PartStartIndex + Section.StartIndex;
					Packet.IndexCount = Section.IndexCount;
					Packet.CameraToParticleDistance = (PartWorldOrigin - CachedCameraLocation).Length();
				}
			}
		}
		if (BoneWeightHeatmapStatus == FBoneWeightHeatmap::StatusOff)
		{
			AppendStaticPreviewPackets(RenderQueue, PreviewWorld);
		}

		if (!RenderQueue.IsEmpty() && PreviewLighting && PreviewLightGrid)
		{
			const FLightGridConstants LightGrid{};
			RenderCommand::UpdateBufferData(PreviewLighting.get(), &LightingConstants);
			RenderCommand::UpdateBufferData(PreviewLightGrid.get(), &LightGrid);
			FViewConstants ViewConstants{};
			ViewConstants.ViewProjection = ViewProj;
			ViewConstants.CameraWorldPos = CachedCameraLocation;
			ViewConstants.CameraForward = FRotator(CameraPitch, CameraYaw, 0.0f).Quaternion().GetForwardVector();
			ViewConstants.BoneWeightHeatmapStatus = BoneWeightHeatmapStatus;
			if (BoneWeightHeatmapStatus == FBoneWeightHeatmap::StatusOn)
			{
				BoneWeightHeatmap.Bind();
			}
			Renderer->RenderPreview(RenderQueue, ViewConstants, *PreviewLighting, *PreviewLightGrid);
			BoneWeightHeatmap.Unbind();
		}
	}

	RenderCommand::EndRenderPass(SceneInfo);

	{
		FRenderingInfo ToneMapInfo;
		ToneMapInfo.ViewportSetting = RenderingInfo.ViewportSetting;
		FRenderingDesc ToneMapColor{};
		ToneMapColor.Texture = ColorTarget.get();
		ToneMapColor.LoadOp = ERenderTargetLoadOp::DontCare;
		ToneMapInfo.ColorRenderTargets.Add(ToneMapColor);
		ToneMapInfo.DepthStencil.Texture = nullptr;

		RenderCommand::BeginRenderPass(ToneMapInfo);
		ToneMapRenderer->SetExposure(PreviewExposure);
		ToneMapRenderer->SetVignette(0.0f, 0.65f, 0.25f, FVector::ZeroVector);
		
		ToneMapRenderer->OnRender(HDRTarget.get(), GetToneMapMode());
		RenderCommand::EndRenderPass(ToneMapInfo);
		RenderCommand::UnbindShaderResource(0, EShaderBindFlagBits::Pixel);
	}

	FRenderingInfo OverlayInfo = RenderingInfo;
	for (FRenderingDesc& Color : OverlayInfo.ColorRenderTargets)
		Color.LoadOp = ERenderTargetLoadOp::Load;
	OverlayInfo.DepthStencil.LoadOp = ERenderTargetLoadOp::Load;
	RenderCommand::BeginRenderPass(OverlayInfo);
	RenderCommand::SetRasterizerState(ERasterizerState::SolidBack);

	if (bShowGrid && GridRenderer)
	{
		const FRotator Rot(CameraPitch, CameraYaw, 0.0f);
		const FQuat Quat(Rot);
		const FVector CameraForward = Quat.GetForwardVector();

		GridRenderer->OnRenderBatchGrid(
			ViewProj,
			CachedCameraLocation,
			CameraForward,
			EGridPlane::XY,
			50.0f,
			true,
			OverlayInfo.ViewportSetting);
	}

	if (PreviewComponent)
	{
		// 본 3D 메시 및 관절 렌더링
		Render3DBones(ViewProj);

		// 3단계: 에디터와 100% 동일한 3D 기즈모(FGizmoRenderer)를 선택된 본 위치에 렌더링
		if (bShowBoneGizmo && !HasActivePreview() && GizmoRenderer && BoneGizmoTarget && SelectedBoneIndex >= 0 && SelectedBoneIndex < static_cast<int32>(SelectedMesh->MeshData.Bones.Num()))
		{
			if (!BoneGizmo.IsUsing())
			{
				TArray<FMatrix> BoneMatrices;
				ComputeBoneWorldMatrices(BoneMatrices);
				if (SelectedBoneIndex < BoneMatrices.Num())
				{
					const FMatrix& BoneMat = BoneMatrices[SelectedBoneIndex];
					const FVector BoneWorldPos(BoneMat.M[3][0], BoneMat.M[3][1], BoneMat.M[3][2]);
					FMatrix BoneRotMat = FMatrix::Identity;
					BoneRotMat.SetAxis(0, FVector(BoneMat.M[0][0], BoneMat.M[0][1], BoneMat.M[0][2]).GetSafeNormal());
					BoneRotMat.SetAxis(1, FVector(BoneMat.M[1][0], BoneMat.M[1][1], BoneMat.M[1][2]).GetSafeNormal());
					BoneRotMat.SetAxis(2, FVector(BoneMat.M[2][0], BoneMat.M[2][1], BoneMat.M[2][2]).GetSafeNormal());

					BoneGizmoTarget->SetRelativeLocation(BoneWorldPos);
					BoneGizmoTarget->SetRelativeRotation(MatrixToRotator(BoneRotMat));
					BoneGizmoTarget->SetRelativeScale3D(GetBoneLocalTransform(SelectedBoneIndex).Scale);
					BoneGizmo.SetTarget(BoneGizmoTarget, false);
				}
			}

			RenderCommand::ClearDepthStencil(OverlayInfo.DepthStencil.Texture);
			RenderCommand::SetRasterizerState(ERasterizerState::SolidBack);
			GizmoRenderer->OnRender(BoneGizmo, ViewProj, CachedCameraLocation, false);
		}
		else
		{
			BoneGizmo.SetTarget(nullptr, false);
		}
	}

	RenderCommand::EndRenderPass(OverlayInfo);
	Renderer->SetViewMode(PreviousViewMode);
}

void FSkeletalMeshViewerPanel::OnRender()
{
	bViewportVisible = false;
	bViewportHovered = false;
	bool bOpen = IsOpen();
	if (!bOpen)
		return;

	ImGui::SetNextWindowSize(ImVec2(1280, 800), ImGuiCond_FirstUseEver);

	if (bNeedFocus)
	{
		ImGui::SetNextWindowFocus();
		bNeedFocus = false;
	}

	// 독립 윈도우 시작
	const bool bVisible = ImGui::Begin(GetPanelName(), &bOpen, ImGuiWindowFlags_MenuBar);

	if (bVisible)
	{
		DrawToolbarUI();
		DrawAssetTabBar();
		if (!OpenAssetMessage.empty()) ImGui::TextWrapped("%s", OpenAssetMessage.c_str());

		// 열려 있는 메시가 없을 때의 안내 화면
		if (OpenedMeshes.Num() == 0 || !SelectedMesh)
		{
			ImGui::Spacing();
			ImGui::TextDisabled("No Skeletal Mesh currently open.");
			ImGui::Spacing();
			ImGui::Text("You can open a mesh from the 'Mesh' menu above, or click [Edit] next to the SkeletalMesh property in the Details Panel.");
		}
	}

	// 뷰어 내부 전용 DockSpace 생성 (레벨 에디터 패널과 도킹 격리)
	ImGuiWindowClass SkelViewerClass;
	SkelViewerClass.ClassId = ImHashStr("SkeletalMeshViewerClass");
	SkelViewerClass.DockingAllowUnclassed = false;

	ImGuiID DockSpaceID = ImGui::GetID("SkeletalMeshViewerInnerDockSpace");

	ImGuiDockNode* Node = ImGui::DockBuilderGetNode(DockSpaceID);
	ImGuiWindow* SkelVpWin = ImGui::FindWindowByName("Skeletal Mesh Viewport");
	const bool bSkelVpOrphaned = (SkelVpWin != nullptr && SkelVpWin->DockNode == nullptr && SkelVpWin->WasActive);

	if (Node == nullptr || bResetLayout || (Node->IsLeafNode() && Node->Windows.Size == 0) || bSkelVpOrphaned)
	{
		bResetLayout = false;
		ImVec2 Avail = ImGui::GetContentRegionAvail();
		ImVec2 DockSize(std::max(Avail.x, 800.0f), std::max(Avail.y, 600.0f));

		ImGui::DockBuilderRemoveNode(DockSpaceID);
		ImGui::DockBuilderAddNode(DockSpaceID, ImGuiDockNodeFlags_DockSpace);
		ImGui::DockBuilderSetNodeSize(DockSpaceID, DockSize);

		ImGuiID CenterNode = DockSpaceID;
		ImGuiID LeftNode = ImGui::DockBuilderSplitNode(CenterNode, ImGuiDir_Left, 0.22f, nullptr, &CenterNode);
		ImGuiID RightNode = ImGui::DockBuilderSplitNode(CenterNode, ImGuiDir_Right, 0.28f, nullptr, &CenterNode);
		ImGuiID RightBottomNode = ImGui::DockBuilderSplitNode(RightNode, ImGuiDir_Down, 0.50f, nullptr, &RightNode);
		ImGuiID CenterBottomNode = ImGui::DockBuilderSplitNode(CenterNode, ImGuiDir_Down, 0.25f, nullptr, &CenterNode);

		ImGui::DockBuilderDockWindow("Skeleton Tree", LeftNode);
		ImGui::DockBuilderDockWindow("Asset Details", RightNode);
		ImGui::DockBuilderDockWindow("Preview Scene Settings", RightBottomNode);
		ImGui::DockBuilderDockWindow("Animation", CenterBottomNode);
		ImGui::DockBuilderDockWindow("Blend Space", CenterBottomNode);
		bBlendSpaceDockInitialized = true;
		ImGui::DockBuilderDockWindow("Skeletal Mesh Viewport", CenterNode);
		ImGui::DockBuilderFinish(DockSpaceID);
	}

	// Migrate only the new tab into saved layouts, preserving every existing split.
	if (!bBlendSpaceDockInitialized)
	{
		if (!ImGui::FindWindowSettingsByID(ImHashStr("Blend Space")))
		{
			ImGuiID Target = 0;
			if (const auto* Animation = ImGui::FindWindowByName("Animation")) Target = Animation->DockId;
			if (!Target)
				if (const auto* Settings = ImGui::FindWindowSettingsByID(ImHashStr("Animation"))) Target = Settings->DockId;
			if (Target) ImGui::DockBuilderDockWindow("Blend Space", Target);
		}
		bBlendSpaceDockInitialized = true;
	}
	const bool bHasActiveMesh = (OpenedMeshes.Num() > 0 && SelectedMesh != nullptr);
	ImGuiDockNodeFlags InnerDockFlags = (bHasActiveMesh && bVisible && bOpen) ? ImGuiDockNodeFlags_None : ImGuiDockNodeFlags_KeepAliveOnly;
	ImGui::DockSpace(DockSpaceID, ImVec2(0.0f, 0.0f), InnerDockFlags, &SkelViewerClass);
	ImGui::End(); // Skeletal Mesh Viewer 메인 창 끝

	bAnimationWindowFocused = false;
	bBlendSpaceWindowFocused = false;
	bViewportFocused = false;
	if (bHasActiveMesh && bVisible && bOpen)
	{
		// 뷰어 전용 도킹 서브 패널 렌더링
		if (bViewportOpen)
		{
			DrawViewportWindow(SkelViewerClass);
		}
		if (bSkeletonTreeOpen)
		{
			DrawSkeletonTreeWindow(SkelViewerClass);
		}
		if (bAssetDetailsOpen)
		{
			DrawAssetDetailsWindow(SkelViewerClass);
		}
		if (bPreviewSettingsOpen)
		{
			DrawPreviewSettingsWindow(SkelViewerClass);
		}
		if (bAnimationOpen) DrawAnimationWindow(SkelViewerClass);
		else bAnimationWasVisible = false;
		if (bBlendSpaceOpen) DrawBlendSpaceWindow(SkelViewerClass);
		else bBlendSpaceWasVisible = false;
	}
	else
	{
		bViewportHovered = false;
	}

	SetOpen(bOpen);
}

void FSkeletalMeshViewerPanel::DrawAssetTabBar()
{
	if (OpenedMeshes.Num() == 0)
		return;

	if (ImGui::BeginTabBar("##SkelMeshAssetTabs", ImGuiTabBarFlags_Reorderable | ImGuiTabBarFlags_AutoSelectNewTabs))
	{
		for (int32 i = 0; i < OpenedMeshes.Num(); ++i)
		{
			USkeletalMesh* Mesh = OpenedMeshes[i];
			if (!Mesh) continue;

			bool bTabOpen = true;
			ImGuiTabItemFlags TabFlags = ImGuiTabItemFlags_None;
			if (MeshToSelect == Mesh)
			{
				TabFlags |= ImGuiTabItemFlags_SetSelected;
			}

			FString Path = Mesh->GetPath();
			FString DisplayName = Path.empty() ? "Skeletal Mesh" : std::filesystem::path(Path).filename().string();
			if (DisplayName.empty()) DisplayName = "Mesh";

			ImGui::PushID(Mesh);
			if (ImGui::BeginTabItem(DisplayName.c_str(), &bTabOpen, TabFlags))
			{
				if (SelectedMesh != Mesh)
				{
					SetSelectedMesh(Mesh);
				}
				ImGui::EndTabItem();
			}
			ImGui::PopID();

			if (!bTabOpen)
			{
				// 탭 닫기
				OpenedMeshes.RemoveAt(i);
				--i;

				if (SelectedMesh == Mesh)
				{
					SetSelectedMesh(OpenedMeshes.IsEmpty() ? nullptr : OpenedMeshes[0]);
				}
			}
		}

		MeshToSelect = nullptr;
		ImGui::EndTabBar();
	}
}

void FSkeletalMeshViewerPanel::DrawViewportWindow(const ImGuiWindowClass& WindowClass)
{
	ImGuiWindowClass ViewportClass = WindowClass;
	ViewportClass.DockNodeFlagsOverrideSet = ImGuiDockNodeFlags_NoUndocking;
	ImGui::SetNextWindowClass(&ViewportClass);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
	const bool bVisible = ImGui::Begin("Skeletal Mesh Viewport", &bViewportOpen,
		ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
	ImGui::PopStyleVar();

	if (bVisible)
	{
		const ImVec2 AvailSize = ImGui::GetContentRegionAvail();
		const uint32 TargetWidth = static_cast<uint32>(std::max(1.0f, AvailSize.x));
		const uint32 TargetHeight = static_cast<uint32>(std::max(1.0f, AvailSize.y));

		ResizeViewport(TargetWidth, TargetHeight);
		if (bFrameRequested)
		{
			FocusOnMesh();
			bFrameRequested = false;
		}

		if (ColorTarget && ColorTarget->GetSRV())
		{
			const ImVec2 CursorPos = ImGui::GetCursorScreenPos();
			const ImVec2 ImageSize(static_cast<float>(ViewportWidth), static_cast<float>(ViewportHeight));
			bViewportVisible = true;
			bViewportFocused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
			CachedViewportOrigin = CursorPos;
			CachedViewportSize = ImageSize;
			ImGui::Image(ColorTarget->GetSRV(), ImageSize);
			bViewportHovered = ImGui::IsItemHovered();

			// 2단계: 뷰포트 본 이름 텍스트 라벨 및 선택 하이라이트 태그 오버레이
			DrawSkeletonOverlay(CursorPos, ImageSize);

			ImDrawList* DrawList = ImGui::GetWindowDrawList();
			const char* HintText = "LMB: Pick Bone | RMB+WASD: Fly | Alt+LMB: Orbit | Wheel: Zoom | MMB: Pan | F: Focus | L+LMB: Light";
			const ImVec2 TextSize = ImGui::CalcTextSize(HintText);
			DrawList->AddRectFilled(
				ImVec2(CursorPos.x + 8, CursorPos.y + 8),
				ImVec2(CursorPos.x + TextSize.x + 16, CursorPos.y + TextSize.y + 14),
				IM_COL32(0, 0, 0, 160), 4.0f);
			DrawList->AddText(ImVec2(CursorPos.x + 12, CursorPos.y + 10), IM_COL32(230, 230, 230, 240), HintText);

			if (HasActivePreview())
			{
				const char* LockText = "ANIMATION PREVIEW  -  editing locked (select None in the active playback tab to edit)";
				const ImVec2 LockSize = ImGui::CalcTextSize(LockText);
				const float LockY = CursorPos.y + TextSize.y + 20;
				DrawList->AddRectFilled(
					ImVec2(CursorPos.x + 8, LockY - 2),
					ImVec2(CursorPos.x + LockSize.x + 16, LockY + LockSize.y + 4),
					IM_COL32(150, 90, 0, 200), 4.0f);
				DrawList->AddText(ImVec2(CursorPos.x + 12, LockY), IM_COL32(255, 235, 200, 255), LockText);
			}
		}
	}
	else
	{
		bViewportHovered = false;
		HoveredBoneIndex = -1;
	}
	ImGui::End();
}

void FSkeletalMeshViewerPanel::ComputeBoneWorldMatrices(TArray<FMatrix>& OutBoneWorldMatrices) const
{
	OutBoneWorldMatrices.Empty();
	if (!SelectedMesh)
		return;

	const TArray<FSkeletalBone>& Bones = SelectedMesh->MeshData.Bones;
	const int32 NumBones = Bones.Num();
	if (NumBones == 0)
		return;

	OutBoneWorldMatrices.SetNum(NumBones);
	TArray<uint8> State;
	State.SetNum(NumBones);
	for (int32 i = 0; i < NumBones; ++i)
	{
		State[i] = 0;
	}

	const FMatrix CompWorldMatrix = PreviewComponent ? PreviewComponent->GetWorldMatrix() : FMatrix::Identity;

	std::function<void(int32)> EvaluateBone = [&](int32 Idx)
	{
		if (Idx < 0 || Idx >= NumBones)
			return;
		if (State[Idx] == 2)
			return;
		if (State[Idx] == 1) // 순환 참조 방지
		{
			OutBoneWorldMatrices[Idx] = FMatrix::Identity;
			State[Idx] = 2;
			return;
		}

		State[Idx] = 1;

		FTransform LocalTf;
		if (!PreviewComponent || !PreviewComponent->GetBoneLocalTransform(Idx, LocalTf))
			LocalTf = Bones[Idx].RefPoseTransform;
		const FMatrix LocalMat = LocalTf.GetLocalMatrix();

		const int32 ParentIdx = Bones[Idx].ParentIndex;
		if (ParentIdx >= 0 && ParentIdx < NumBones && ParentIdx != Idx)
		{
			EvaluateBone(ParentIdx);
			OutBoneWorldMatrices[Idx] = LocalMat * OutBoneWorldMatrices[ParentIdx];
		}
		else
		{
			OutBoneWorldMatrices[Idx] = LocalMat * CompWorldMatrix;
		}

		State[Idx] = 2;
	};

	for (int32 i = 0; i < NumBones; ++i)
	{
		EvaluateBone(i);
	}
}

bool FSkeletalMeshViewerPanel::IsBoneAncestorOf(int32 AncestorIndex, int32 TargetBoneIndex, const TArray<FSkeletalBone>& Bones) const
{
	if (AncestorIndex < 0 || TargetBoneIndex < 0 || AncestorIndex >= Bones.Num() || TargetBoneIndex >= Bones.Num())
		return false;

	int32 Cur = Bones[TargetBoneIndex].ParentIndex;
	int32 Safety = 0;
	while (Cur >= 0 && Cur < Bones.Num() && Safety < Bones.Num())
	{
		if (Cur == AncestorIndex)
			return true;
		Cur = Bones[Cur].ParentIndex;
		++Safety;
	}
	return false;
}

void FSkeletalMeshViewerPanel::UpdateBoneInteraction()
{
	if (!SelectedMesh || CachedViewportSize.x <= 1.0f || CachedViewportSize.y <= 1.0f)
		return;

	const TArray<FSkeletalBone>& Bones = SelectedMesh->MeshData.Bones;
	const int32 NumBones = Bones.Num();
	if (NumBones == 0)
		return;

	TArray<FMatrix> BoneMatrices;
	ComputeBoneWorldMatrices(BoneMatrices);
	if (BoneMatrices.Num() != NumBones)
		return;

	const FMatrix ViewProj = CachedViewMatrix * CachedProjMatrix;
	ImGuiIO& IO = ImGui::GetIO();
	const bool bCameraManipulating = ImGui::IsMouseDown(ImGuiMouseButton_Right)
		|| ImGui::IsMouseDown(ImGuiMouseButton_Middle)
		|| IO.KeyAlt
		|| ImGui::IsKeyDown(ImGuiKey_L);

	bool bGizmoBlocksPick = false;
	if (bShowBoneGizmo && !HasActivePreview() && PreviewComponent && BoneGizmoTarget && SelectedBoneIndex >= 0 && SelectedBoneIndex < NumBones)
	{
		if (!BoneGizmo.IsUsing())
		{
			const FMatrix& BoneMat = BoneMatrices[SelectedBoneIndex];
			const FVector BoneWorldPos(BoneMat.M[3][0], BoneMat.M[3][1], BoneMat.M[3][2]);
			FMatrix BoneRotMat = FMatrix::Identity;
			BoneRotMat.SetAxis(0, FVector(BoneMat.M[0][0], BoneMat.M[0][1], BoneMat.M[0][2]).GetSafeNormal());
			BoneRotMat.SetAxis(1, FVector(BoneMat.M[1][0], BoneMat.M[1][1], BoneMat.M[1][2]).GetSafeNormal());
			BoneRotMat.SetAxis(2, FVector(BoneMat.M[2][0], BoneMat.M[2][1], BoneMat.M[2][2]).GetSafeNormal());

			BoneGizmoTarget->SetRelativeLocation(BoneWorldPos);
			BoneGizmoTarget->SetRelativeRotation(MatrixToRotator(BoneRotMat));
			BoneGizmoTarget->SetRelativeScale3D(GetBoneLocalTransform(SelectedBoneIndex).Scale);
			BoneGizmo.SetTarget(BoneGizmoTarget, false);
		}

		if ((bViewportHovered && !bCameraManipulating) || BoneGizmo.IsUsing())
		{
			const FVector2 LocalMousePos(IO.MousePos.x - CachedViewportOrigin.x, IO.MousePos.y - CachedViewportOrigin.y);
			const float NdcX = (LocalMousePos.X / CachedViewportSize.x) * 2.0f - 1.0f;
			const float NdcY = 1.0f - (LocalMousePos.Y / CachedViewportSize.y) * 2.0f;

			const FMatrix InvViewProj = ViewProj.Inverse();
			FVector4 NearClip = InvViewProj.TransformFVector4(FVector4(NdcX, NdcY, 0.0f, 1.0f));
			FVector4 FarClip = InvViewProj.TransformFVector4(FVector4(NdcX, NdcY, 1.0f, 1.0f));
			if (std::abs(NearClip.W) > 1e-6f) NearClip /= NearClip.W;
			if (std::abs(FarClip.W) > 1e-6f) FarClip /= FarClip.W;

			FRay MouseRay{};
			MouseRay.Origin = CachedCameraLocation;
			MouseRay.Direction = (FVector(FarClip.X, FarClip.Y, FarClip.Z) - FVector(NearClip.X, NearClip.Y, NearClip.Z)).GetSafeNormal();

			const bool bLeftMouseDown = ImGui::IsMouseDown(ImGuiMouseButton_Left) && !bCameraManipulating;
			BoneGizmo.Update(
				MouseRay,
				LocalMousePos,
				ViewProj,
				static_cast<int>(CachedViewportSize.x),
				static_cast<int>(CachedViewportSize.y),
				bLeftMouseDown,
				CachedCameraLocation,
				false);

			if (BoneGizmo.IsUsing())
			{
				FTransform NewLocalTf = GetBoneLocalTransform(SelectedBoneIndex);
				const int32 ParentIdx = Bones[SelectedBoneIndex].ParentIndex;
				const FMatrix ParentWorldMat = (ParentIdx >= 0 && ParentIdx < NumBones)
					? BoneMatrices[ParentIdx]
					: PreviewComponent->GetWorldMatrix();

				if (BoneGizmo.GetMode() == EGizmoMode::Rotation)
				{
					const FMatrix NewWorldRotMat = BoneGizmoTarget->GetRelativeRotation().Quaternion().ToFMatrix();
					FMatrix ParentRotMat = FMatrix::Identity;
					ParentRotMat.SetAxis(0, FVector(ParentWorldMat.M[0][0], ParentWorldMat.M[0][1], ParentWorldMat.M[0][2]).GetSafeNormal());
					ParentRotMat.SetAxis(1, FVector(ParentWorldMat.M[1][0], ParentWorldMat.M[1][1], ParentWorldMat.M[1][2]).GetSafeNormal());
					ParentRotMat.SetAxis(2, FVector(ParentWorldMat.M[2][0], ParentWorldMat.M[2][1], ParentWorldMat.M[2][2]).GetSafeNormal());

					const FMatrix NewLocalRotMat = NewWorldRotMat * ParentRotMat.GetTransposed();
					NewLocalTf.Rotation = MatrixToRotator(NewLocalRotMat);
				}
				else if (BoneGizmo.GetMode() == EGizmoMode::Location)
				{
					const FVector NewWorldPos = BoneGizmoTarget->GetWorldLocation();
					NewLocalTf.Location = ParentWorldMat.Inverse().TransformPosition(NewWorldPos);
				}
				else if (BoneGizmo.GetMode() == EGizmoMode::Scale)
				{
					NewLocalTf.Scale = BoneGizmoTarget->GetRelativeScale3D();
				}

				PreviewComponent->SetBoneLocalTransform(SelectedBoneIndex, NewLocalTf);
				ComputeBoneWorldMatrices(BoneMatrices);
			}
		}

		bGizmoBlocksPick = BoneGizmo.IsUsing() || (BoneGizmo.GetHoveredAxis() >= 0);
	}
	else
	{
		BoneGizmo.SetTarget(nullptr, false);
	}

	auto ProjectPoint = [&](const FVector& WorldPos, ImVec2& OutScreen) -> bool
	{
		const FVector4 Clip = ViewProj.TransformFVector4(FVector4(WorldPos.X, WorldPos.Y, WorldPos.Z, 1.0f));
		if (Clip.W <= 0.1f)
			return false;

		const float InvW = 1.0f / Clip.W;
		const float NdcX = Clip.X * InvW;
		const float NdcY = Clip.Y * InvW;

		OutScreen.x = CachedViewportOrigin.x + (NdcX * 0.5f + 0.5f) * CachedViewportSize.x;
		OutScreen.y = CachedViewportOrigin.y + (1.0f - (NdcY * 0.5f + 0.5f)) * CachedViewportSize.y;
		return true;
	};

	TArray<ImVec2> BoneScreenPts;
	TArray<uint8> BoneInFront;
	BoneScreenPts.SetNum(NumBones);
	BoneInFront.SetNum(NumBones);
	for (int32 i = 0; i < NumBones; ++i)
	{
		const FMatrix& Mat = BoneMatrices[i];
		const FVector Pos(Mat.M[3][0], Mat.M[3][1], Mat.M[3][2]);
		BoneInFront[i] = ProjectPoint(Pos, BoneScreenPts[i]) ? 1 : 0;
	}

	// 뷰포트 마우스 호버 및 좌클릭 본 피킹 (기즈모 조작 중이 아닐 때만 수행)
	HoveredBoneIndex = -1;
	if (bViewportHovered && !bCameraManipulating && !bGizmoBlocksPick && bShowBones)
	{
		const FVector2 MousePt(IO.MousePos.x, IO.MousePos.y);
		float BestJointScore = 14.0f;
		int32 BestJointBone = -1;

		for (int32 i = 0; i < NumBones; ++i)
		{
			if (!BoneInFront[i])
				continue;

			const float dx = MousePt.X - BoneScreenPts[i].x;
			const float dy = MousePt.Y - BoneScreenPts[i].y;
			const float Dist = std::sqrt(dx * dx + dy * dy);
			if (Dist < BestJointScore)
			{
				BestJointScore = Dist;
				BestJointBone = i;
			}
		}

		if (BestJointBone >= 0)
		{
			HoveredBoneIndex = BestJointBone;
		}
		else
		{
			float BestSegDist = 9.0f;
			int32 BestSegBone = -1;
			for (int32 i = 0; i < NumBones; ++i)
			{
				const int32 ParentIdx = Bones[i].ParentIndex;
				if (ParentIdx < 0 || ParentIdx >= NumBones)
					continue;
				if (!BoneInFront[ParentIdx] || !BoneInFront[i])
					continue;

				const FVector2 SegA(BoneScreenPts[ParentIdx].x, BoneScreenPts[ParentIdx].y);
				const FVector2 SegB(BoneScreenPts[i].x, BoneScreenPts[i].y);
				const float SegDist = DistanceToSegment(MousePt, SegA, SegB);
				if (SegDist < BestSegDist)
				{
					BestSegDist = SegDist;
					BestSegBone = ParentIdx;
				}
			}
			HoveredBoneIndex = BestSegBone;
		}

		if (ImGui::IsMouseClicked(ImGuiMouseButton_Left))
		{
			SelectedBoneIndex = HoveredBoneIndex;
			if (SelectedBoneIndex >= 0)
			{
				bScrollToSelectedBone = true;
			}
		}
	}
}

void FSkeletalMeshViewerPanel::Render3DBones(const FMatrix& ViewProj)
{
	if (!bShowBones || !SelectedMesh)
		return;

	if (!BoneMesh)
	{
		BoneMesh = UAssetManager::GetAssetByPath<UStaticMesh>("OctahedralBone");
	}
	if (!BoneSphereMesh)
	{
		BoneSphereMesh = UAssetManager::GetAssetByPath<UStaticMesh>("GizmoSphere");
	}
	if (!BoneCB)
	{
		BoneCB = RenderCommand::CreateConstantBuffer(sizeof(FGizmoData));
	}
	if (!BoneShader)
	{
		BoneShader = FRenderResourceManager::GetShaderProgram("Resources/Shader/GizmoShader.hlsl");
		BonePipelineState.Shader = BoneShader;
		BonePipelineState.Topology = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	}

	if (!BoneMesh || !BoneCB || !BoneShader)
		return;

	const TArray<FSkeletalBone>& Bones = SelectedMesh->MeshData.Bones;
	const int32 NumBones = Bones.Num();
	if (NumBones == 0)
		return;

	TArray<FMatrix> BoneMatrices;
	ComputeBoneWorldMatrices(BoneMatrices);
	if (BoneMatrices.Num() != NumBones)
		return;

	// 뼈대가 캐릭터 메쉬 안쪽에 파묻히지 않도록 깊이 버퍼를 클리어하고, 뼈대들끼리는 서로 깊이 판정(Z-Test) 수행
	RenderCommand::ClearDepthStencil(RenderingInfo.DepthStencil.Texture);
	RenderCommand::BindPipelineState(BonePipelineState);

	// 1. 본 바디(입체 사각뿔 / 팔면체) 렌더링
	if (bDrawBoneCones)
	{
		// 1-1. 반투명 채우기 (Alpha Blend Pass)
		RenderCommand::SetBlendState(EBlendState::AlphaBlend);
		RenderCommand::SetDepthStencilState(EDepthStencilState::Default);
		RenderCommand::SetRasterizerState(ERasterizerState::SolidNone);
		RenderCommand::BindMesh(BoneMesh);

		for (int32 i = 0; i < NumBones; ++i)
		{
			const int32 ParentIdx = Bones[i].ParentIndex;
			if (ParentIdx < 0 || ParentIdx >= NumBones)
				continue;

			const FVector P0(BoneMatrices[ParentIdx].M[3][0], BoneMatrices[ParentIdx].M[3][1], BoneMatrices[ParentIdx].M[3][2]);
			const FVector P1(BoneMatrices[i].M[3][0], BoneMatrices[i].M[3][1], BoneMatrices[i].M[3][2]);
			const FVector Diff = P1 - P0;
			const float BoneLength = Diff.Length();
			if (BoneLength <= 0.001f)
				continue;

			const FVector Dir = Diff / BoneLength;
			const FVector RefAxis = (std::abs(Dir.Z) < 0.9f) ? FVector(0.0f, 0.0f, 1.0f) : FVector(1.0f, 0.0f, 0.0f);
			const FVector SideA = Dir.Cross(RefAxis).GetSafeNormal();
			const FVector SideB = SideA.Cross(Dir).GetSafeNormal();
			const float Radius = BoneLength * BoneConeWidthRatio;

			FMatrix BoneWorld = FMatrix::Identity;
			BoneWorld.SetAxis(0, SideA * Radius);
			BoneWorld.SetAxis(1, SideB * Radius);
			BoneWorld.SetAxis(2, Dir * BoneLength);
			BoneWorld.M[3][0] = P0.X;
			BoneWorld.M[3][1] = P0.Y;
			BoneWorld.M[3][2] = P0.Z;

			const bool bIsIncomingToSelected = (i == SelectedBoneIndex);
			const bool bIsOutgoingFromSelected = (ParentIdx == SelectedBoneIndex);
			const bool bIsHovered = (i == HoveredBoneIndex || ParentIdx == HoveredBoneIndex);

			FVector4 FillColor = FVector4(0.25f, 0.75f, 0.98f, 0.30f);
			if (bIsIncomingToSelected)
				FillColor = FVector4(1.0f, 0.57f, 0.0f, 0.55f);
			else if (bIsOutgoingFromSelected)
				FillColor = FVector4(1.0f, 0.45f, 0.08f, 0.50f);
			else if (bIsHovered)
				FillColor = FVector4(1.0f, 0.95f, 0.45f, 0.40f);

			FGizmoData Data{};
			Data.World = BoneWorld;
			Data.ViewProj = ViewProj;
			Data.Color = FillColor;
			RenderCommand::UpdateBufferData(BoneCB.get(), &Data, sizeof(FGizmoData));
			RenderCommand::BindConstantBuffer(0, BoneCB.get(), EShaderBindFlagBits::Vertex);
			RenderCommand::DrawIndexed(BoneMesh->IndexBuffer->GetIndexCount());
		}

		// 1-2. 모서리 외곽선 와이어프레임 (Wireframe Pass)
		RenderCommand::SetRasterizerState(ERasterizerState::Wireframe);
		for (int32 i = 0; i < NumBones; ++i)
		{
			const int32 ParentIdx = Bones[i].ParentIndex;
			if (ParentIdx < 0 || ParentIdx >= NumBones)
				continue;

			const FVector P0(BoneMatrices[ParentIdx].M[3][0], BoneMatrices[ParentIdx].M[3][1], BoneMatrices[ParentIdx].M[3][2]);
			const FVector P1(BoneMatrices[i].M[3][0], BoneMatrices[i].M[3][1], BoneMatrices[i].M[3][2]);
			const FVector Diff = P1 - P0;
			const float BoneLength = Diff.Length();
			if (BoneLength <= 0.001f)
				continue;

			const FVector Dir = Diff / BoneLength;
			const FVector RefAxis = (std::abs(Dir.Z) < 0.9f) ? FVector(0.0f, 0.0f, 1.0f) : FVector(1.0f, 0.0f, 0.0f);
			const FVector SideA = Dir.Cross(RefAxis).GetSafeNormal();
			const FVector SideB = SideA.Cross(Dir).GetSafeNormal();
			const float Radius = BoneLength * BoneConeWidthRatio;

			FMatrix BoneWorld = FMatrix::Identity;
			BoneWorld.SetAxis(0, SideA * Radius);
			BoneWorld.SetAxis(1, SideB * Radius);
			BoneWorld.SetAxis(2, Dir * BoneLength);
			BoneWorld.M[3][0] = P0.X;
			BoneWorld.M[3][1] = P0.Y;
			BoneWorld.M[3][2] = P0.Z;

			const bool bIsIncomingToSelected = (i == SelectedBoneIndex);
			const bool bIsOutgoingFromSelected = (ParentIdx == SelectedBoneIndex);
			const bool bIsHovered = (i == HoveredBoneIndex || ParentIdx == HoveredBoneIndex);

			FVector4 WireColor = FVector4(0.37f, 0.85f, 1.0f, 0.90f);
			if (bIsIncomingToSelected)
				WireColor = FVector4(1.0f, 0.88f, 0.15f, 1.0f);
			else if (bIsOutgoingFromSelected)
				WireColor = FVector4(1.0f, 0.76f, 0.12f, 1.0f);
			else if (bIsHovered)
				WireColor = FVector4(1.0f, 0.96f, 0.55f, 0.95f);

			FGizmoData Data{};
			Data.World = BoneWorld;
			Data.ViewProj = ViewProj;
			Data.Color = WireColor;
			RenderCommand::UpdateBufferData(BoneCB.get(), &Data, sizeof(FGizmoData));
			RenderCommand::BindConstantBuffer(0, BoneCB.get(), EShaderBindFlagBits::Vertex);
			RenderCommand::DrawIndexed(BoneMesh->IndexBuffer->GetIndexCount());
		}
	}
	else
	{
		// 1-3. 선 모드 (Line Mode)일 때 얇은 와이어프레임 본 렌더링
		RenderCommand::SetBlendState(EBlendState::Opaque);
		RenderCommand::SetDepthStencilState(EDepthStencilState::Default);
		RenderCommand::SetRasterizerState(ERasterizerState::Wireframe);
		RenderCommand::BindMesh(BoneMesh);

		for (int32 i = 0; i < NumBones; ++i)
		{
			const int32 ParentIdx = Bones[i].ParentIndex;
			if (ParentIdx < 0 || ParentIdx >= NumBones)
				continue;

			const FVector P0(BoneMatrices[ParentIdx].M[3][0], BoneMatrices[ParentIdx].M[3][1], BoneMatrices[ParentIdx].M[3][2]);
			const FVector P1(BoneMatrices[i].M[3][0], BoneMatrices[i].M[3][1], BoneMatrices[i].M[3][2]);
			const FVector Diff = P1 - P0;
			const float BoneLength = Diff.Length();
			if (BoneLength <= 0.001f)
				continue;

			const FVector Dir = Diff / BoneLength;
			const FVector RefAxis = (std::abs(Dir.Z) < 0.9f) ? FVector(0.0f, 0.0f, 1.0f) : FVector(1.0f, 0.0f, 0.0f);
			const FVector SideA = Dir.Cross(RefAxis).GetSafeNormal();
			const FVector SideB = SideA.Cross(Dir).GetSafeNormal();
			const float Radius = BoneLength * 0.012f;

			FMatrix BoneWorld = FMatrix::Identity;
			BoneWorld.SetAxis(0, SideA * Radius);
			BoneWorld.SetAxis(1, SideB * Radius);
			BoneWorld.SetAxis(2, Dir * BoneLength);
			BoneWorld.M[3][0] = P0.X;
			BoneWorld.M[3][1] = P0.Y;
			BoneWorld.M[3][2] = P0.Z;

			const bool bIsIncomingToSelected = (i == SelectedBoneIndex);
			const bool bIsOutgoingFromSelected = (ParentIdx == SelectedBoneIndex);
			const bool bIsHovered = (i == HoveredBoneIndex || ParentIdx == HoveredBoneIndex);

			FVector4 WireColor = FVector4(0.37f, 0.85f, 1.0f, 0.90f);
			if (bIsIncomingToSelected)
				WireColor = FVector4(1.0f, 0.88f, 0.15f, 1.0f);
			else if (bIsOutgoingFromSelected)
				WireColor = FVector4(1.0f, 0.76f, 0.12f, 1.0f);
			else if (bIsHovered)
				WireColor = FVector4(1.0f, 0.96f, 0.55f, 0.95f);

			FGizmoData Data{};
			Data.World = BoneWorld;
			Data.ViewProj = ViewProj;
			Data.Color = WireColor;
			RenderCommand::UpdateBufferData(BoneCB.get(), &Data, sizeof(FGizmoData));
			RenderCommand::BindConstantBuffer(0, BoneCB.get(), EShaderBindFlagBits::Vertex);
			RenderCommand::DrawIndexed(BoneMesh->IndexBuffer->GetIndexCount());
		}
	}

	// 2. 조인트 구(Sphere) 렌더링
	if (BoneSphereMesh && BoneJointRadius > 0.01f)
	{
		RenderCommand::SetBlendState(EBlendState::AlphaBlend);
		RenderCommand::SetDepthStencilState(EDepthStencilState::Default);
		RenderCommand::SetRasterizerState(ERasterizerState::SolidNone);
		RenderCommand::BindMesh(BoneSphereMesh);

		// 모델 크기(MeshRadius)에 비례하도록 자동 정규화 (모델 크기의 약 0.5% ~ 1.0% 수준)
		const float ModelScale = (MeshRadius > 0.001f) ? MeshRadius : 1.0f;
		const float DesiredRadius = ModelScale * 0.0020f * BoneJointRadius;
		const float BaseSphereRadius = 0.2f;
		const float ScaleFactor = DesiredRadius / BaseSphereRadius;

		for (int32 i = 0; i < NumBones; ++i)
		{
			const FVector Pos(BoneMatrices[i].M[3][0], BoneMatrices[i].M[3][1], BoneMatrices[i].M[3][2]);
			const bool bIsSelected = (i == SelectedBoneIndex);
			const bool bIsHovered = (i == HoveredBoneIndex);
			const bool bIsRoot = (Bones[i].ParentIndex == -1);

			float CurScale = ScaleFactor;
			FVector4 SphereColor = FVector4(0.70f, 0.90f, 1.0f, 0.85f);
			if (bIsSelected)
			{
				CurScale *= 1.35f;
				SphereColor = FVector4(1.0f, 0.55f, 0.0f, 0.95f);
			}
			else if (bIsHovered)
			{
				CurScale *= 1.20f;
				SphereColor = FVector4(1.0f, 0.95f, 0.4f, 0.90f);
			}
			else if (bIsRoot)
			{
				SphereColor = FVector4(1.0f, 0.35f, 0.35f, 0.90f);
			}

			FMatrix SphereWorld = FMatrix::Identity;
			SphereWorld.M[0][0] = SphereWorld.M[1][1] = SphereWorld.M[2][2] = CurScale;
			SphereWorld.M[3][0] = Pos.X;
			SphereWorld.M[3][1] = Pos.Y;
			SphereWorld.M[3][2] = Pos.Z;

			FGizmoData Data{};
			Data.World = SphereWorld;
			Data.ViewProj = ViewProj;
			Data.Color = SphereColor;
			RenderCommand::UpdateBufferData(BoneCB.get(), &Data, sizeof(FGizmoData));
			RenderCommand::BindConstantBuffer(0, BoneCB.get(), EShaderBindFlagBits::Vertex);
			RenderCommand::DrawIndexed(BoneSphereMesh->IndexBuffer->GetIndexCount());
		}
	}

	// 렌더링 상태 복원
	RenderCommand::SetBlendState(EBlendState::Opaque);
	RenderCommand::SetRasterizerState(ERasterizerState::SolidBack);
}

void FSkeletalMeshViewerPanel::DrawSkeletonOverlay(const ImVec2& ViewportOrigin, const ImVec2& ViewportSize)
{
	if (!SelectedMesh || ViewportSize.x <= 1.0f || ViewportSize.y <= 1.0f)
		return;

	const TArray<FSkeletalBone>& Bones = SelectedMesh->MeshData.Bones;
	const int32 NumBones = Bones.Num();
	if (NumBones == 0)
		return;

	if (!bShowBones && SelectedBoneIndex < 0)
		return;

	TArray<FMatrix> BoneMatrices;
	ComputeBoneWorldMatrices(BoneMatrices);
	if (BoneMatrices.Num() != NumBones)
		return;

	const FMatrix ViewProj = CachedViewMatrix * CachedProjMatrix;

	auto ProjectPoint = [&](const FVector& WorldPos, ImVec2& OutScreen) -> bool
	{
		const FVector4 Clip = ViewProj.TransformFVector4(FVector4(WorldPos.X, WorldPos.Y, WorldPos.Z, 1.0f));
		if (Clip.W <= 0.1f)
			return false;

		const float InvW = 1.0f / Clip.W;
		const float NdcX = Clip.X * InvW;
		const float NdcY = Clip.Y * InvW;

		OutScreen.x = ViewportOrigin.x + (NdcX * 0.5f + 0.5f) * ViewportSize.x;
		OutScreen.y = ViewportOrigin.y + (1.0f - (NdcY * 0.5f + 0.5f)) * ViewportSize.y;
		return true;
	};

	TArray<ImVec2> BoneScreenPts;
	TArray<uint8> BoneInFront;
	BoneScreenPts.SetNum(NumBones);
	BoneInFront.SetNum(NumBones);

	for (int32 i = 0; i < NumBones; ++i)
	{
		const FMatrix& Mat = BoneMatrices[i];
		const FVector Pos(Mat.M[3][0], Mat.M[3][1], Mat.M[3][2]);
		BoneInFront[i] = ProjectPoint(Pos, BoneScreenPts[i]) ? 1 : 0;
	}

	ImDrawList* DrawList = ImGui::GetWindowDrawList();
	DrawList->PushClipRect(
		ViewportOrigin,
		ImVec2(ViewportOrigin.x + ViewportSize.x, ViewportOrigin.y + ViewportSize.y),
		true);

	// 1. 모든 본 이름 라벨 표시 (옵션 활성화 시)
	if (bShowAllBoneNames)
	{
		for (int32 i = 0; i < NumBones; ++i)
		{
			if (!BoneInFront[i] || i == SelectedBoneIndex)
				continue;

			const ImVec2& Pt = BoneScreenPts[i];
			DrawList->AddText(ImVec2(Pt.x + 6.0f, Pt.y - 6.0f), IM_COL32(215, 230, 245, 210), Bones[i].Name.c_str());
		}
	}

	// 2. 선택된 본의 이름 태그 오버레이 표시
	if (SelectedBoneIndex >= 0 && SelectedBoneIndex < NumBones && BoneInFront[SelectedBoneIndex])
	{
		const ImVec2& SelScreen = BoneScreenPts[SelectedBoneIndex];

		char BoneTag[160];
		sprintf_s(BoneTag, sizeof(BoneTag), "[%d] %s", SelectedBoneIndex, Bones[SelectedBoneIndex].Name.c_str());
		const ImVec2 TagSize = ImGui::CalcTextSize(BoneTag);
		const ImVec2 TagMin(SelScreen.x + 10.0f, SelScreen.y - TagSize.y - 8.0f);
		const ImVec2 TagMax(TagMin.x + TagSize.x + 12.0f, TagMin.y + TagSize.y + 6.0f);

		DrawList->AddRectFilled(TagMin, TagMax, IM_COL32(20, 20, 20, 215), 4.0f);
		DrawList->AddRect(TagMin, TagMax, IM_COL32(255, 185, 30, 255), 4.0f, 0, 1.5f);
		DrawList->AddText(ImVec2(TagMin.x + 6.0f, TagMin.y + 3.0f), IM_COL32(255, 230, 110, 255), BoneTag);
	}

	DrawList->PopClipRect();
}

void FSkeletalMeshViewerPanel::DrawSkeletonTreeWindow(const ImGuiWindowClass& WindowClass)
{
	ImGui::SetNextWindowClass(&WindowClass);
	if (ImGui::Begin("Skeleton Tree", &bSkeletonTreeOpen))
	{
		DrawBoneHierarchyUI();
	}
	ImGui::End();
}

void FSkeletalMeshViewerPanel::DrawAssetDetailsWindow(const ImGuiWindowClass& WindowClass)
{
	ImGui::SetNextWindowClass(&WindowClass);
	if (ImGui::Begin("Asset Details", &bAssetDetailsOpen))
	{
		DrawMeshDetailsUI();
	}
	ImGui::End();
}

void FSkeletalMeshViewerPanel::DrawPreviewSettingsWindow(const ImGuiWindowClass& WindowClass)
{
	ImGui::SetNextWindowClass(&WindowClass);
	if (ImGui::Begin("Preview Scene Settings", &bPreviewSettingsOpen))
	{
		DrawLightingSettingsUI();
	}
	ImGui::End();
}

namespace
{
	constexpr const char* PreviewLockedTooltip = "Animation preview: select None in the Animation window to edit";

	void ShowEditTooltip(bool bLocked, const char* NormalTooltip = nullptr)
	{
		if (bLocked)
		{
			if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
			{
				ImGui::SetTooltip("%s", PreviewLockedTooltip);
			}
		}
		else if (NormalTooltip && ImGui::IsItemHovered())
		{
			ImGui::SetTooltip("%s", NormalTooltip);
		}
	}

	enum class ETransportIcon : uint8
	{
		Play,
		Pause,
		Reverse,
		ToFirst,
		ToLast,
		StepBack,
		StepForward,
		Loop,
	};

	bool IsPlayableClip(const UAnimSequence* Sequence)
	{
		const UAnimDataModel* Model = Sequence ? Sequence->GetDataModel() : nullptr;
		return Model && Model->GetNumberOfFrames() > 0 && Model->GetPlayLength() > 0.0f;
	}

	std::string FormatPlaySpeed(float Speed)
	{
		char Buffer[32];
		snprintf(Buffer, sizeof(Buffer), "%.2f", Speed);
		std::string Text(Buffer);
		while (Text.size() > 3 && Text.back() == '0' && Text[Text.size() - 2] != '.')
		{
			Text.pop_back();
		}
		return "x" + Text;
	}

	bool DrawTransportButton(const char* Id, ETransportIcon Icon, bool bActive)
	{
		const float Size = ImGui::GetFrameHeight();
		const ImVec2 Min = ImGui::GetCursorScreenPos();
		const ImVec2 Max(Min.x + Size, Min.y + Size);

		const bool bPressed = ImGui::InvisibleButton(Id, ImVec2(Size, Size));
		const bool bHovered = ImGui::IsItemHovered();
		const bool bHeld = ImGui::IsItemActive();

		ImDrawList* DrawList = ImGui::GetWindowDrawList();

		ImGuiCol BgCol = ImGuiCol_Button;
		if (bHeld) BgCol = ImGuiCol_ButtonActive;
		else if (bHovered) BgCol = ImGuiCol_ButtonHovered;
		DrawList->AddRectFilled(Min, Max, ImGui::GetColorU32(BgCol), ImGui::GetStyle().FrameRounding);

		const ImU32 IconCol = ImGui::GetColorU32(bActive ? ImGuiCol_CheckMark : ImGuiCol_Text);
		const float Cx = (Min.x + Max.x) * 0.5f;
		const float Cy = (Min.y + Max.y) * 0.5f;
		const float R = Size * 0.28f;
		const float Bar = R * 0.35f;

		auto Triangle = [&](float TipX, float Dir, float Width)
		{
			const float BaseX = TipX - Dir * Width;
			DrawList->AddTriangleFilled(ImVec2(BaseX, Cy - R), ImVec2(TipX, Cy), ImVec2(BaseX, Cy + R), IconCol);
		};
		auto VerticalBar = [&](float X0)
		{
			DrawList->AddRectFilled(ImVec2(X0, Cy - R), ImVec2(X0 + Bar, Cy + R), IconCol);
		};

		switch (Icon)
		{
		case ETransportIcon::Play:
			Triangle(Cx + R, +1.0f, R * 1.8f);
			break;
		case ETransportIcon::Reverse:
			Triangle(Cx - R, -1.0f, R * 1.8f);
			break;
		case ETransportIcon::Pause:
			VerticalBar(Cx - R * 0.7f);
			VerticalBar(Cx + R * 0.7f - Bar);
			break;
		case ETransportIcon::ToFirst:
			VerticalBar(Cx - R);
			Triangle(Cx - R + Bar, -1.0f, R);
			Triangle(Cx, -1.0f, R);
			break;
		case ETransportIcon::ToLast:
			VerticalBar(Cx + R - Bar);
			Triangle(Cx + R - Bar, +1.0f, R);
			Triangle(Cx, +1.0f, R);
			break;
		case ETransportIcon::StepBack:
			VerticalBar(Cx - R * 0.8f);
			Triangle(Cx - R * 0.8f + Bar, -1.0f, R * 1.5f);
			break;
		case ETransportIcon::StepForward:
			VerticalBar(Cx + R * 0.8f - Bar);
			Triangle(Cx + R * 0.8f - Bar, +1.0f, R * 1.5f);
			break;
		case ETransportIcon::Loop:
		{
			const float Pi = 3.14159265f;
			const float A0 = -Pi * 0.35f;
			const float A1 = Pi * 1.25f;
			DrawList->PathArcTo(ImVec2(Cx, Cy), R, A0, A1, 16);
			DrawList->PathStroke(IconCol, ImDrawFlags_None, 1.8f);

			const ImVec2 End(Cx + std::cos(A1) * R, Cy + std::sin(A1) * R);
			const float Head = R * 0.55f;
			const float Tx = -std::sin(A1), Ty = std::cos(A1);
			const float Nx = std::cos(A1), Ny = std::sin(A1);
			DrawList->AddTriangleFilled(
				ImVec2(End.x + Tx * Head, End.y + Ty * Head),
				ImVec2(End.x + Nx * Head * 0.7f, End.y + Ny * Head * 0.7f),
				ImVec2(End.x - Nx * Head * 0.7f, End.y - Ny * Head * 0.7f),
				IconCol);
			break;
		}
		}

		return bPressed;
	}
}

void FSkeletalMeshViewerPanel::DrawAnimationWindow(const ImGuiWindowClass& WindowClass)
{
 if (PreviewMode == EPreviewMode::StateMachine || PreviewMode == EPreviewMode::AnimGraph) return;
	ImGui::SetNextWindowClass(&WindowClass);
	// 키보드 내비게이션을 끈다. 켜 두면 Space가 포커스된 버튼도 눌러 재생 토글이 두 번 일어난다.
	const bool Visible = ImGui::Begin("Animation", &bAnimationOpen, ImGuiWindowFlags_NoNavInputs);
	if (Visible)
	{
		bAnimationWindowFocused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
		if (bAnimationWindowFocused || (!bAnimationWasVisible && ImGui::IsWindowDocked())) ActivatePreviewMode(EPreviewMode::Sequence);

		UAnimSingleNodeInstance* AnimInstance = GetPreviewAnimInstance();

		if (!AnimInstance)
		{
			ImGui::TextDisabled("Preview has no single-node anim instance. Clip playback unavailable.");
			ImGui::End();
			return;
		}

		const bool bHasSelection = SelectedClipIndex >= 0 && SelectedClipIndex < PreviewClips.Num();
		const char* PreviewLabel = bHasSelection ? PreviewClips[SelectedClipIndex].Name.c_str() : "None";

		ImGui::AlignTextToFramePadding();
		ImGui::TextUnformatted("Clip");
		ImGui::SameLine();
		ImGui::SetNextItemWidth(260.0f);
		if (ImGui::BeginCombo("##PreviewClip", PreviewLabel, ImGuiComboFlags_HeightLargest))
		{
			if (ImGui::IsWindowAppearing())
			{
				ClipSearchFilter[0] = '\0';
				ImGui::SetKeyboardFocusHere();
			}
			ImGui::SetNextItemWidth(-FLT_MIN);
			const bool bSearchSubmitted = ImGui::InputTextWithHint("##ClipSearch", "Search clip...", ClipSearchFilter,
				sizeof(ClipSearchFilter), ImGuiInputTextFlags_EnterReturnsTrue);

			auto ToLower = [](FString Text)
			{
				std::transform(Text.begin(), Text.end(), Text.begin(), [](unsigned char C) { return static_cast<char>(std::tolower(C)); });
				return Text;
			};
			const FString Filter = ToLower(ClipSearchFilter);

			TArray<int32> Matches;
			for (int32 i = 0; i < PreviewClips.Num(); ++i)
			{
				if (Filter.empty() || ToLower(PreviewClips[i].Name).find(Filter) != FString::npos)
				{
					Matches.Add(i);
				}
			}
			ImGui::TextDisabled("%d / %d clips", Matches.Num(), PreviewClips.Num());

			if (bSearchSubmitted && !Matches.IsEmpty() && IsPlayableClip(PreviewClips[Matches[0]].Sequence))
			{
				SelectPreviewClip(Matches[0]);
				ImGui::CloseCurrentPopup();
			}

			const float ListHeight = ImGui::GetTextLineHeightWithSpacing() * std::min(Matches.Num() + 1, 14) + ImGui::GetStyle().WindowPadding.y;
			ImGui::BeginChild("##ClipList", ImVec2(0.0f, ListHeight));
			if (Filter.empty() && ImGui::Selectable("None", !bHasSelection) && bHasSelection)
			{
				SelectPreviewClip(-1);
			}

			for (const int32 i : Matches)
			{
				const bool bSelected = (i == SelectedClipIndex);
				const bool bPlayable = IsPlayableClip(PreviewClips[i].Sequence);
				ImGui::PushID(i);
				ImGui::BeginDisabled(!bPlayable);
				if (ImGui::Selectable(PreviewClips[i].Name.c_str(), bSelected) && !bSelected)
				{
					SelectPreviewClip(i);
				}
				ImGui::EndDisabled();
				if (!bPlayable && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
				{
					ImGui::SetTooltip("Clip has no frames.");
				}
				if (bSelected && ImGui::IsWindowAppearing())
				{
					ImGui::SetScrollHereY();
				}
				ImGui::PopID();
			}
			if (Matches.IsEmpty())
			{
				ImGui::TextDisabled("No matching clip");
			}
			ImGui::EndChild();
			ImGui::EndCombo();
		}

		if (PreviewMode == EPreviewMode::BlendSpace)
		{
			ImGui::TextDisabled("BlendSpace preview is active. Focus this tab to use sequence playback.");
			bAnimationWasVisible = Visible;
			ImGui::End();
			return;
		}
		// 콤보에서 선택이 바뀌었을 수 있어 다시 확인한다.
		ImGui::SameLine();
		const UAnimSequence* Clip = (SelectedClipIndex >= 0 && SelectedClipIndex < PreviewClips.Num())
			? PreviewClips[SelectedClipIndex].Sequence
			: nullptr;
		const UAnimDataModel* Model = Clip ? Clip->GetDataModel() : nullptr;
		if (Model)
		{
			ImGui::TextDisabled("%.2fs | %d frames | %.0f fps",
				Model->GetPlayLength(),
				Model->GetNumberOfFrames(),
				Model->GetFrameRate().AsDecimal());
		}
		else
		{
			ImGui::TextDisabled("No animation clip selected (edit mode)");
		}

		if (Model)
		{
			ImGui::SameLine(0.0f, 16.0f);
			const char* SaveLabel = bNotifyDirty ? "Save Notifies *###SaveNotifies" : "Save Notifies###SaveNotifies";
			if (ImGui::Button(SaveLabel))
			{
				SavePreviewNotifies();
			}
			if (ImGui::IsItemHovered())
			{
				ImGui::SetTooltip("Write notifies of this FBX to <fbx>.notify");
			}
		}

		DrawPreviewTimeline(Model);
		DrawNotifyProperties(Model);
		DrawFiredNotifies();

		ImGui::Separator();
		ImGui::BeginDisabled(Model == nullptr);
		{
			const bool bPlaying = Model && AnimInstance->IsPlaying();
			const bool bPlayingForward = bPlaying && PreviewPlayDirection > 0;
			const bool bPlayingReverse = bPlaying && PreviewPlayDirection < 0;
			const float ClipLength = Model ? Model->GetPlayLength() : 0.0f;

			auto TransportButton = [](const char* Id, ETransportIcon Icon, const char* Tooltip) -> bool
			{
				const bool bPressed = DrawTransportButton(Id, Icon, false);
				if (ImGui::IsItemHovered())
				{
					ImGui::SetTooltip("%s", Tooltip);
				}
				ImGui::SameLine();
				return bPressed;
			};

			if (TransportButton("##ToFirst", ETransportIcon::ToFirst, "To Front"))
			{
				ScrubPreviewToTime(0.0f);
			}
			if (TransportButton("##StepBack", ETransportIcon::StepBack, "Previous Frame (Left Arrow)"))
			{
				StepPreviewFrame(-1);
			}
			if (TransportButton("##Reverse", bPlayingReverse ? ETransportIcon::Pause : ETransportIcon::Reverse,
				bPlayingReverse ? "Pause" : "Play Reverse"))
			{
				PlayPreviewInDirection(-1);
			}
			if (TransportButton("##PlayPause", bPlayingForward ? ETransportIcon::Pause : ETransportIcon::Play,
				bPlayingForward ? "Pause (Space)" : "Play Forward (Space)"))
			{
				PlayPreviewInDirection(+1);
			}
			if (TransportButton("##StepForward", ETransportIcon::StepForward, "Next Frame (Right Arrow)"))
			{
				StepPreviewFrame(+1);
			}
			if (TransportButton("##ToLast", ETransportIcon::ToLast, "To End"))
			{
				ScrubPreviewToTime(ClipLength);
			}

			ImGui::SameLine(0.0f, 12.0f);
			const bool bLooping = AnimInstance->IsLooping();
			if (DrawTransportButton("##Loop", ETransportIcon::Loop, bLooping))
			{
				AnimInstance->SetLooping(!bLooping);
			}
			if (ImGui::IsItemHovered())
			{
				ImGui::SetTooltip(bLooping ? "Looping: On" : "Looping: Off");
			}

			ImGui::SameLine();
			const std::string SpeedLabel = FormatPlaySpeed(PreviewPlaySpeed) + "###PlaybackSpeed";
			if (ImGui::Button(SpeedLabel.c_str()))
			{
				ImGui::OpenPopup("##PlaybackSpeedPopup");
			}
			if (ImGui::IsItemHovered())
			{
				ImGui::SetTooltip("Playback Speed");
			}
			if (ImGui::BeginPopup("##PlaybackSpeedPopup"))
			{
				static constexpr float SpeedPresets[] = { 0.1f, 0.25f, 0.5f, 0.75f, 1.0f, 2.0f, 5.0f, 10.0f };

				ImGui::TextDisabled("Playback Speed");
				ImGui::Separator();

				bool bIsPreset = false;
				for (const float Preset : SpeedPresets)
				{
					bIsPreset |= std::fabs(PreviewPlaySpeed - Preset) < 1e-4f;
				}

				for (const float Preset : SpeedPresets)
				{
					const bool bSelected = std::fabs(PreviewPlaySpeed - Preset) < 1e-4f;
					if (ImGui::RadioButton(FormatPlaySpeed(Preset).c_str(), bSelected))
					{
						SetPreviewPlaySpeed(Preset);
						ImGui::CloseCurrentPopup();
					}
				}
				if (ImGui::RadioButton("xCustom", !bIsPreset))
				{
					SetPreviewPlaySpeed(PreviewCustomSpeed);
					PreviewCustomSpeed = PreviewPlaySpeed;
				}

				ImGui::SetNextItemWidth(80.0f);
				ImGui::InputFloat("Custom Speed", &PreviewCustomSpeed, 0.0f, 0.0f, "%.2f");
				if (ImGui::IsItemDeactivatedAfterEdit())
				{
					SetPreviewPlaySpeed(PreviewCustomSpeed);
					PreviewCustomSpeed = PreviewPlaySpeed;
				}
				ImGui::EndPopup();
			}

			ImGui::SameLine(0.0f, 16.0f);
			ImGui::AlignTextToFramePadding();
			if (Model)
			{
				ImGui::Text("Frame %d / %d    %.2fs / %.2fs",
					GetPreviewCurrentFrame(),
					Model->GetNumberOfFrames(),
					AnimInstance->GetPosition(),
					Model->GetPlayLength());
			}
			else
			{
				ImGui::TextUnformatted("Frame - / -    -s / -s");
			}
		}
		ImGui::EndDisabled();

		const char* SkinningText = (PreviewComponent && PreviewComponent->UsesGPUSkinning()) ? "Skinning: GPU" : "Skinning: CPU";
		const float SkinningTextW = ImGui::CalcTextSize(SkinningText).x;
		ImGui::SameLine(0.0f, 16.0f);
		const float RightAlignedX = ImGui::GetWindowContentRegionMax().x - SkinningTextW;
		if (RightAlignedX > ImGui::GetCursorPosX())
		{
			ImGui::SetCursorPosX(RightAlignedX);
		}
		ImGui::TextDisabled("%s", SkinningText);
	}
	bAnimationWasVisible = Visible;
	ImGui::End();
}

void FSkeletalMeshViewerPanel::DrawPreviewTimeline(const UAnimDataModel* Model)
{
	constexpr float RulerHeight = 18.0f;
	constexpr float TrackHeight = 28.0f;
	constexpr float SidePadding = 10.0f;

	const float Width = ImGui::GetContentRegionAvail().x;
	if (Width <= SidePadding * 2.0f + 1.0f)
	{
		return;
	}

	const ImVec2 Origin = ImGui::GetCursorScreenPos();
	const ImVec2 Size(Width, RulerHeight + TrackHeight);
	const ImVec2 Max(Origin.x + Size.x, Origin.y + Size.y);
	const float RulerBottom = Origin.y + RulerHeight;

	ImGui::InvisibleButton("##PreviewTimeline", Size);
	const bool bHovered = ImGui::IsItemHovered();
	const bool bActive = ImGui::IsItemActive();
	const bool bActivated = ImGui::IsItemActivated();
	const bool bDeactivated = ImGui::IsItemDeactivated();

	ImDrawList* DrawList = ImGui::GetWindowDrawList();
	DrawList->AddRectFilled(Origin, ImVec2(Max.x, RulerBottom), ImGui::GetColorU32(ImGuiCol_FrameBg));
	DrawList->AddRectFilled(ImVec2(Origin.x, RulerBottom), Max, ImGui::GetColorU32(ImGuiCol_FrameBg, 0.5f));
	DrawList->AddLine(ImVec2(Origin.x, RulerBottom), ImVec2(Max.x, RulerBottom), ImGui::GetColorU32(ImGuiCol_Border));

	UAnimSingleNodeInstance* AnimInstance = GetPreviewAnimInstance();
	if (!Model || !AnimInstance || !HasPreviewClip())
	{
		return;
	}

	const int32 NumFrames = std::max(Model->GetNumberOfFrames(), 1);
	const float Length = Model->GetPlayLength();
	const float FPS = Model->GetFrameRate().AsDecimal();
	const float X0 = Origin.x + SidePadding;
	const float X1 = Max.x - SidePadding;
	const float Span = X1 - X0;
	const float PixelsPerFrame = Span / static_cast<float>(NumFrames);

	auto FrameToX = [&](float Frame) { return X0 + (Frame / static_cast<float>(NumFrames)) * Span; };
	auto XToTime = [&](float X) { return std::clamp((X - X0) / Span, 0.0f, 1.0f) * Length; };
	auto TimeToX = [&](float Time)
	{
		return X0 + (Length > 0.0f ? std::clamp(Time / Length, 0.0f, 1.0f) : 0.0f) * Span;
	};

	const ImVec2 MousePos = ImGui::GetIO().MousePos;
	const float TrackTop = RulerBottom + 4.0f;
	const float TrackBottom = Max.y - 4.0f;
	const float TrackMidY = (TrackTop + TrackBottom) * 0.5f;
	const bool bMouseInTrack = bHovered && MousePos.y >= TrackTop && MousePos.y <= TrackBottom;
	constexpr float DiamondRadius = 5.0f;

	const UAnimSequence* Sequence = GetSelectedPreviewSequence();
	const TArray<FAnimNotifyEvent>* Notifies = Sequence ? &Sequence->Notifies : nullptr;
	const int32 NotifyCount = Notifies ? Notifies->Num() : 0;
	if (SelectedNotifyIndex >= NotifyCount)
	{
		SelectPreviewNotify(-1);
		bDraggingNotify = false;
	}

	int32 HoveredNotify = -1;
	if (bMouseInTrack)
	{
		for (int32 i = NotifyCount - 1; i >= 0 && HoveredNotify < 0; --i)
		{
			const FAnimNotifyEvent& Notify = (*Notifies)[i];
			const float XStart = TimeToX(Notify.TriggerTime);
			if (IsNotifyStateEvent(Notify))
			{
				const float XEnd = std::max(TimeToX(Notify.TriggerTime + Notify.Duration), XStart + 2.0f);
				if (MousePos.x >= XStart - DiamondRadius && MousePos.x <= XEnd)
				{
					HoveredNotify = i;
				}
			}
			else if (std::fabs(MousePos.x - XStart) <= DiamondRadius + 1.0f)
			{
				HoveredNotify = i;
			}
		}
	}

	if (bActivated)
	{
		if (HoveredNotify >= 0)
		{
			SelectPreviewNotify(HoveredNotify);
			bDraggingNotify = true;
			NotifyDragOffset = XToTime(MousePos.x) - (*Notifies)[HoveredNotify].TriggerTime;
		}
		else
		{
			SelectPreviewNotify(-1);
			bDraggingNotify = false;
		}
	}

	if (bActive)
	{
		if (bDraggingNotify && SelectedNotifyIndex >= 0)
		{
			SetPreviewNotifyTime(SelectedNotifyIndex, XToTime(MousePos.x) - NotifyDragOffset, !ImGui::GetIO().KeyShift);
		}
		else
		{
			ScrubPreviewToTime(XToTime(MousePos.x));
		}
	}

	if (bDeactivated && bDraggingNotify)
	{
		bDraggingNotify = false;
		if (SelectedNotifyIndex >= 0)
		{
			SelectPreviewNotify(SortPreviewNotifies(SelectedNotifyIndex));
		}
	}

	if (bMouseInTrack && ImGui::IsMouseClicked(ImGuiMouseButton_Right))
	{
		NotifyPopupIndex = HoveredNotify;
		NotifyPopupTime = XToTime(MousePos.x);
		if (HoveredNotify >= 0)
		{
			SelectPreviewNotify(HoveredNotify);
		}
		ImGui::OpenPopup("##NotifyContext");
	}

	const float MinLabelSpacing = ImGui::CalcTextSize(std::to_string(NumFrames).c_str()).x + 12.0f;
	int32 MajorStep = 1;
	for (const int32 Candidate : { 1, 2, 5, 10, 20, 50, 100, 200, 500, 1000, 2000, 5000 })
	{
		MajorStep = Candidate;
		if (Candidate * PixelsPerFrame >= MinLabelSpacing)
		{
			break;
		}
	}
	int32 MinorStep = (MajorStep % 5 == 0) ? MajorStep / 5 : ((MajorStep % 2 == 0) ? MajorStep / 2 : MajorStep);
	if (MinorStep * PixelsPerFrame < 4.0f)
	{
		MinorStep = MajorStep;
	}

	const ImU32 TickCol = ImGui::GetColorU32(ImGuiCol_TextDisabled);
	const ImU32 LabelCol = ImGui::GetColorU32(ImGuiCol_Text);
	const ImU32 GridCol = ImGui::GetColorU32(ImGuiCol_Border, 0.6f);
	for (int32 Frame = 0; Frame <= NumFrames; Frame += MinorStep)
	{
		const float X = FrameToX(static_cast<float>(Frame));
		if (Frame % MajorStep == 0)
		{
			DrawList->AddLine(ImVec2(X, RulerBottom - 7.0f), ImVec2(X, RulerBottom), TickCol);
			DrawList->AddLine(ImVec2(X, RulerBottom), ImVec2(X, Max.y), GridCol);
			DrawList->AddText(ImVec2(X + 3.0f, Origin.y + 1.0f), LabelCol, std::to_string(Frame).c_str());
		}
		else
		{
			DrawList->AddLine(ImVec2(X, RulerBottom - 3.0f), ImVec2(X, RulerBottom), TickCol);
		}
	}
	DrawList->AddLine(ImVec2(X1, Origin.y), ImVec2(X1, Max.y), TickCol);

	const float CurrentTime = AnimInstance->GetPosition();
	auto DrawDiamond = [&](float X, ImU32 Col)
	{
		DrawList->AddQuadFilled(
			ImVec2(X, TrackMidY - DiamondRadius), ImVec2(X + DiamondRadius, TrackMidY),
			ImVec2(X, TrackMidY + DiamondRadius), ImVec2(X - DiamondRadius, TrackMidY), Col);
	};
	auto DrawDiamondOutline = [&](float X, ImU32 Col)
	{
		const float R = DiamondRadius + 2.0f;
		DrawList->AddQuad(
			ImVec2(X, TrackMidY - R), ImVec2(X + R, TrackMidY),
			ImVec2(X, TrackMidY + R), ImVec2(X - R, TrackMidY), Col, 2.0f);
	};

	const ImU32 PointCol = IM_COL32(235, 200, 60, 255);
	const ImU32 RangeCol = IM_COL32(70, 150, 220, 170);
	const ImU32 RangeActiveCol = IM_COL32(120, 200, 255, 240);
	const ImU32 NotifyTextCol = IM_COL32(15, 15, 20, 255);
	const ImU32 SelectedCol = IM_COL32(255, 255, 255, 255);
	const ImU32 FiredCol = IM_COL32(255, 120, 60, 255);

	DrawList->PushClipRect(Origin, Max, true);
	for (int32 i = 0; i < NotifyCount; ++i)
	{
		const FAnimNotifyEvent& Notify = (*Notifies)[i];
		const FString NotifyName = GetNotifyLabel(Notify);
		const float XStart = TimeToX(Notify.TriggerTime);
		const ImVec2 NameSize = ImGui::CalcTextSize(NotifyName.c_str());
		const bool bSelected = (i == SelectedNotifyIndex);

		if (IsNotifyStateEvent(Notify))
		{
			const float EndTime = Notify.TriggerTime + Notify.Duration;
			const float XEnd = std::max(TimeToX(EndTime), XStart + 2.0f);
			const bool bActiveNow = (CurrentTime >= Notify.TriggerTime && CurrentTime <= EndTime) || IsPreviewNotifyFlashing(Notify);

			DrawList->AddRectFilled(ImVec2(XStart, TrackTop), ImVec2(XEnd, TrackBottom),
				bActiveNow ? RangeActiveCol : RangeCol, 4.0f);
			if (bSelected)
			{
				DrawList->AddRect(ImVec2(XStart, TrackTop), ImVec2(XEnd, TrackBottom), SelectedCol, 4.0f, 0, 2.0f);
			}
			DrawDiamond(XStart, PointCol);
			if (NameSize.x < XEnd - XStart - DiamondRadius * 2.0f - 4.0f)
			{
				DrawList->AddText(ImVec2(XStart + DiamondRadius + 4.0f, TrackMidY - NameSize.y * 0.5f),
					NotifyTextCol, NotifyName.c_str());
			}
		}
		else
		{
			const ImU32 MarkerCol = IsPreviewNotifyFlashing(Notify) ? FiredCol : PointCol;
			DrawList->AddLine(ImVec2(XStart, TrackTop), ImVec2(XStart, TrackBottom), MarkerCol);
			DrawDiamond(XStart, MarkerCol);
			if (bSelected)
			{
				DrawDiamondOutline(XStart, SelectedCol);
			}
			DrawList->AddText(ImVec2(XStart + DiamondRadius + 3.0f, TrackMidY - NameSize.y * 0.5f),
				LabelCol, NotifyName.c_str());
		}
	}
	DrawList->PopClipRect();

	const ImU32 PlayheadCol = IM_COL32(230, 60, 50, 255);
	const float PlayheadX = FrameToX(AnimInstance->GetPosition() * FPS);
	DrawList->AddLine(ImVec2(PlayheadX, Origin.y), ImVec2(PlayheadX, Max.y), PlayheadCol, 2.0f);

	const std::string FrameLabel = std::to_string(GetPreviewCurrentFrame());
	const ImVec2 LabelSize = ImGui::CalcTextSize(FrameLabel.c_str());
	const float HandleW = LabelSize.x + 8.0f;
	const float HandleX = std::clamp(PlayheadX - HandleW * 0.5f, Origin.x, Max.x - HandleW);
	DrawList->AddRectFilled(ImVec2(HandleX, Origin.y), ImVec2(HandleX + HandleW, RulerBottom), PlayheadCol, 3.0f);
	DrawList->AddText(ImVec2(HandleX + 4.0f, Origin.y + (RulerHeight - LabelSize.y) * 0.5f), IM_COL32(255, 255, 255, 255), FrameLabel.c_str());

	if (bHovered && !bActive)
	{
		if (HoveredNotify >= 0)
		{
			const FAnimNotifyEvent& Notify = (*Notifies)[HoveredNotify];
			const FString NotifyName = GetNotifyLabel(Notify);
			const int32 StartFrame = static_cast<int32>(std::floor(Notify.TriggerTime * FPS));
			if (IsNotifyStateEvent(Notify))
			{
				ImGui::SetTooltip("%s\nStart %.2fs (frame %d)\nDuration %.2fs",
					NotifyName.c_str(), Notify.TriggerTime, StartFrame, Notify.Duration);
			}
			else
			{
				ImGui::SetTooltip("%s\nTime %.2fs (frame %d)", NotifyName.c_str(), Notify.TriggerTime, StartFrame);
			}
		}
		else
		{
			const float HoverTime = XToTime(MousePos.x);
			const int32 HoverFrame = std::clamp(static_cast<int32>(std::floor(HoverTime * FPS)), 0, NumFrames);
			ImGui::SetTooltip("Frame %d  (%.2fs)", HoverFrame, HoverTime);
		}
	}

	if (ImGui::BeginPopup("##NotifyContext"))
	{
		if (NotifyPopupIndex >= 0)
		{
			if (ImGui::MenuItem("Delete Notify"))
			{
				RemovePreviewNotify(NotifyPopupIndex);
				SelectPreviewNotify(-1);
			}
		}
		else
		{
			if (ImGui::MenuItem("Add Notify"))
			{
				SelectPreviewNotify(AddPreviewNotify(NotifyPopupTime, 0.0f));
			}
			if (ImGui::MenuItem("Add Notify State"))
			{
				SelectPreviewNotify(AddPreviewNotify(NotifyPopupTime, 0.2f));
			}
		}
		ImGui::EndPopup();
	}
}

void FSkeletalMeshViewerPanel::DrawNotifyProperties(const UAnimDataModel* Model)
{
	const UAnimSequence* Sequence = GetSelectedPreviewSequence();
	if (!Model || !Sequence)
	{
		return;
	}

	const int32 NotifyCount = Sequence->Notifies.Num();
	if (SelectedNotifyIndex < 0 || SelectedNotifyIndex >= NotifyCount)
	{
		ImGui::TextDisabled("Notifies: %d    Right-click the timeline to add a notify.", NotifyCount);
		return;
	}

	const float Length = Model->GetPlayLength();
	const float FPS = Model->GetFrameRate().AsDecimal();

	ImGui::PushID("##NotifyProperties");

	UAnimNotify* NotifyObject = Sequence->Notifies[SelectedNotifyIndex].Notify;

	ImGui::AlignTextToFramePadding();
	ImGui::TextUnformatted("Notify");
	ImGui::SameLine();
	const FString TypeLabel = NotifyObject ? UAnimNotify::GetDisplayNameForClass(NotifyObject->GetClass()) : FString("Name Notify");

	TArray<UClass*> NotifyClasses;
	GetDerivedClasses(UAnimNotify::StaticClass(), NotifyClasses);
	float TypeTextWidth = ImGui::CalcTextSize("Name Notify").x;
	for (const UClass* NotifyClass : NotifyClasses)
	{
		TypeTextWidth = std::max(TypeTextWidth, ImGui::CalcTextSize(UAnimNotify::GetDisplayNameForClass(NotifyClass).c_str()).x);
	}
	ImGui::SetNextItemWidth(TypeTextWidth + ImGui::GetFrameHeight() + ImGui::GetStyle().FramePadding.x * 2.0f);
	if (ImGui::BeginCombo("##NotifyType", TypeLabel.c_str()))
	{
		if (ImGui::Selectable("Name Notify", NotifyObject == nullptr) && NotifyObject)
		{
			SetPreviewNotifyClass(SelectedNotifyIndex, nullptr);
		}

		const bool bIsState = IsNotifyStateEvent(Sequence->Notifies[SelectedNotifyIndex]);
		for (UClass* NotifyClass : NotifyClasses)
		{
			if (!NotifyClass->CanCreateInstance())
			{
				continue;
			}

			const bool bSelected = NotifyObject && NotifyObject->GetClass() == NotifyClass;
			const FString Label = UAnimNotify::GetDisplayNameForClass(NotifyClass);
			const bool bDisabled = bIsState && !IsNotifyStateClass(NotifyClass);
			ImGui::BeginDisabled(bDisabled);
			if (ImGui::Selectable(Label.c_str(), bSelected) && !bSelected)
			{
				SetPreviewNotifyClass(SelectedNotifyIndex, NotifyClass);
			}
			ImGui::EndDisabled();
			if (bDisabled && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
			{
				ImGui::SetTooltip("Notify State supports Name Notify or NotifyState classes. Set Duration to 0 first.");
			}
		}
		ImGui::EndCombo();
	}
	if (ImGui::IsItemHovered())
	{
		ImGui::SetTooltip("Notify type");
	}

	if (SelectedNotifyIndex >= 0 && SelectedNotifyIndex < Sequence->Notifies.Num())
	{
		NotifyObject = Sequence->Notifies[SelectedNotifyIndex].Notify;
	}

	if (!NotifyObject)
	{
		ImGui::SameLine();
		ImGui::SetNextItemWidth(150.0f);
		ImGui::InputText("##NotifyName", NotifyNameBuffer, sizeof(NotifyNameBuffer));
		if (ImGui::IsItemDeactivatedAfterEdit())
		{
			RenamePreviewNotify(SelectedNotifyIndex, NotifyNameBuffer);
			SelectPreviewNotify(SelectedNotifyIndex);
		}

		ImGui::SameLine();
		if (ImGui::BeginCombo("##NotifyPreset", nullptr, ImGuiComboFlags_NoPreview))
		{
			const TArray<FString> UsedNames = CollectUsedNotifyNames();
			if (UsedNames.IsEmpty())
			{
				ImGui::TextDisabled("No names used yet");
			}
			for (const FString& UsedName : UsedNames)
			{
				if (ImGui::Selectable(UsedName.c_str(), UsedName == NotifyNameBuffer))
				{
					RenamePreviewNotify(SelectedNotifyIndex, UsedName);
					SelectPreviewNotify(SelectedNotifyIndex);
				}
			}
			ImGui::EndCombo();
		}
		if (ImGui::IsItemHovered())
		{
			ImGui::SetTooltip("Names already used in this FBX");
		}
	}

	if (SelectedNotifyIndex >= 0 && SelectedNotifyIndex < Sequence->Notifies.Num())
	{
		ImGui::SameLine(0.0f, 16.0f);
		ImGui::TextUnformatted("Time");
		ImGui::SameLine();
		float Time = Sequence->Notifies[SelectedNotifyIndex].TriggerTime;
		ImGui::SetNextItemWidth(90.0f);
		if (ImGui::DragFloat("##NotifyTime", &Time, 0.01f, 0.0f, Length, "%.3fs"))
		{
			SetPreviewNotifyTime(SelectedNotifyIndex, Time, false);
		}
		if (ImGui::IsItemDeactivatedAfterEdit())
		{
			SelectPreviewNotify(SortPreviewNotifies(SelectedNotifyIndex));
		}
		ImGui::SameLine();
		ImGui::TextDisabled("frame %d", static_cast<int32>(std::floor(Time * FPS)));
	}

	if (SelectedNotifyIndex >= 0 && SelectedNotifyIndex < Sequence->Notifies.Num())
	{
		const FAnimNotifyEvent& Current = Sequence->Notifies[SelectedNotifyIndex];
		if (CanHaveNotifyDuration(Current))
		{
			ImGui::SameLine(0.0f, 16.0f);
			ImGui::TextUnformatted("Duration");
			ImGui::SameLine();
			float Duration = Current.Duration;
			ImGui::SetNextItemWidth(90.0f);
			if (ImGui::DragFloat("##NotifyDuration", &Duration, 0.01f, 0.0f, std::max(0.0f, Length - Current.TriggerTime), "%.3fs"))
			{
				SetPreviewNotifyDuration(SelectedNotifyIndex, Duration);
			}
			if (ImGui::IsItemHovered())
			{
				ImGui::SetTooltip("0 = single notify, > 0 = notify state");
			}
		}

		ImGui::SameLine(0.0f, 16.0f);
		if (ImGui::Button("Delete"))
		{
			RemovePreviewNotify(SelectedNotifyIndex);
			SelectPreviewNotify(-1);
		}
	}

	if (SelectedNotifyIndex >= 0 && SelectedNotifyIndex < Sequence->Notifies.Num() && Sequence->Notifies[SelectedNotifyIndex].Notify)
	{
		DrawNotifyObjectProperties(SelectedNotifyIndex, Sequence->Notifies[SelectedNotifyIndex].Notify);
	}

	ImGui::PopID();
}

void FSkeletalMeshViewerPanel::DrawNotifyObjectProperties(int32 Index, UAnimNotify* NotifyObject)
{
	ImGui::Indent(ImGui::GetFontSize());
	bool bFirst = true;
	for (const UClass* Class = NotifyObject->GetClass(); Class; Class = Class->Super)
	{
		for (const FProperty& Property : Class->GetProperties())
		{
			if (!Property.HasAnyFlags(CPF_Edit))
			{
				continue;
			}

			const FString Name = Property.Name.ToString();
			void* Ptr = reinterpret_cast<uint8*>(NotifyObject) + Property.Offset;
			const bool bReadOnly = Property.HasAnyFlags(CPF_EditConst);

			if (!bFirst)
			{
				ImGui::SameLine(0.0f, 16.0f);
			}
			bFirst = false;

			ImGui::PushID(Name.c_str());
			ImGui::AlignTextToFramePadding();
			ImGui::TextUnformatted(Name.c_str());
			ImGui::SameLine();
			ImGui::BeginDisabled(bReadOnly);

			bool bChanged = false;
			switch (Property.Type)
			{
			case EPropertyType::Float:
				ImGui::SetNextItemWidth(70.0f);
				bChanged = ImGui::DragFloat("##Value", static_cast<float*>(Ptr), 0.01f, 0.0f, 0.0f, "%.2f");
				break;
			case EPropertyType::Int:
				ImGui::SetNextItemWidth(70.0f);
				bChanged = ImGui::DragInt("##Value", static_cast<int32*>(Ptr));
				break;
			case EPropertyType::Bool:
				bChanged = ImGui::Checkbox("##Value", static_cast<bool*>(Ptr));
				break;
			case EPropertyType::String:
			{
				FString& Value = *static_cast<FString*>(Ptr);
				if (const char* AssetFolder = NotifyObject->GetPropertyAssetFolder(Property))
				{
					bChanged = DrawAudioFilePicker(AssetFolder, Value);
					break;
				}

				char Buffer[260];
				snprintf(Buffer, sizeof(Buffer), "%s", Value.c_str());
				ImGui::SetNextItemWidth(240.0f);
				ImGui::InputText("##Value", Buffer, sizeof(Buffer));
				if (ImGui::IsItemDeactivatedAfterEdit())
				{
					Value = Buffer;
					bChanged = true;
				}
				break;
			}
			default:
				ImGui::TextDisabled("(unsupported)");
				break;
			}

			ImGui::EndDisabled();
			ImGui::PopID();

			if (bChanged)
			{
				OnPreviewNotifyPropertyChanged(Index, Property);
			}
		}
	}
	ImGui::Unindent(ImGui::GetFontSize());
}

void FSkeletalMeshViewerPanel::HandlePreviewNotify(const FAnimNotifyEvent& Event, UAnimSequenceBase* Sequence)
{
	AddFiredPreviewNotify(Event, GetNotifyLabel(Event));

	if (bPreviewCameraShake)
	{
		if (const UAnimNotify_CameraShake* Shake = Cast<UAnimNotify_CameraShake>(Event.Notify))
		{
			PreviewShake = Shake->CreateShakePattern();
			PreviewShakeScale = Shake->ShakeScale;
			if (PreviewShake)
			{
				PreviewShake->StartShakePattern();
			}
		}
	}
}

void FSkeletalMeshViewerPanel::AddFiredPreviewNotify(const FAnimNotifyEvent& Event, const FString& Label)
{
	constexpr int32 MaxFiredNotifies = 8;

	FFiredPreviewNotify& Fired = FiredNotifies.AddDefaulted_GetRef();
	Fired.Event = Event;
	Fired.Label = Label;
	Fired.FiredAt = ImGui::GetTime();
	while (FiredNotifies.Num() > MaxFiredNotifies)
	{
		FiredNotifies.RemoveAt(0);
	}
}

void FSkeletalMeshViewerPanel::HandlePreviewNotifyStateBegin(const FAnimNotifyEvent& Event, UAnimSequenceBase* Sequence)
{
	AddFiredPreviewNotify(Event, GetNotifyLabel(Event) + " [Begin]");
}

void FSkeletalMeshViewerPanel::HandlePreviewNotifyStateEnd(const FAnimNotifyEvent& Event, UAnimSequenceBase* Sequence)
{
	AddFiredPreviewNotify(Event, GetNotifyLabel(Event) + " [End]");
}

bool FSkeletalMeshViewerPanel::IsPreviewNotifyFlashing(const FAnimNotifyEvent& Event) const
{
	constexpr double FlashSeconds = 0.3;
	const double Now = ImGui::GetTime();
	for (const FFiredPreviewNotify& Fired : FiredNotifies)
	{
		if (Now - Fired.FiredAt <= FlashSeconds &&
			Fired.Event.Notify == Event.Notify &&
			Fired.Event.TriggerTime == Event.TriggerTime &&
			Fired.Event.NotifyName == Event.NotifyName)
		{
			return true;
		}
	}
	return false;
}

void FSkeletalMeshViewerPanel::DrawFiredNotifies()
{
	if (!HasPreviewClip())
	{
		return;
	}

	ImGui::Checkbox("Preview Camera Shake", &bPreviewCameraShake);
	ImGui::SameLine(0.0f, 16.0f);
	ImGui::TextDisabled("Fired:");
	for (int32 i = FiredNotifies.Num() - 1; i >= 0; --i)
	{
		const FFiredPreviewNotify& Fired = FiredNotifies[i];
		ImGui::SameLine();
		ImGui::Text("%s (%.2fs)", Fired.Label.c_str(), Fired.Event.TriggerTime);
	}
}

TArray<FString> FSkeletalMeshViewerPanel::CollectUsedNotifyNames() const
{
	TArray<FString> Names;
	const FFbxAssetSet* Assets = SelectedMesh ? UAssetManager::Get().FindFbxAssets(SelectedMesh->GetPath()) : nullptr;
	if (!Assets)
	{
		return Names;
	}

	for (const UAnimSequence* Sequence : Assets->Animations)
	{
		if (!Sequence)
		{
			continue;
		}
		for (const FAnimNotifyEvent& Event : Sequence->Notifies)
		{
			if (Event.Notify)
			{
				continue;
			}
			const FString Name = Event.NotifyName.ToString();
			if (!Name.empty() && Name != "None" && Names.Find(Name) == INDEX_NONE)
			{
				Names.Add(Name);
			}
		}
	}

	std::sort(Names.begin(), Names.end());
	return Names;
}

bool FSkeletalMeshViewerPanel::DrawAudioFilePicker(const char* Folder, FString& InOutPath)
{
	bool bChanged = false;
	const FString Preview = InOutPath.empty() ? FString("None") : FFileHelper::ToUtf8Path(FFileHelper::ToNativePath(InOutPath).filename());

	ImGui::SetNextItemWidth(220.0f);
	if (ImGui::BeginCombo("##AudioFile", Preview.c_str()))
	{
		if (ImGui::IsWindowAppearing())
		{
			RefreshAudioFileList(Folder);
		}

		if (ImGui::Selectable("None", InOutPath.empty()) && !InOutPath.empty())
		{
			InOutPath.clear();
			bChanged = true;
		}
		if (AudioFileList.IsEmpty())
		{
			ImGui::TextDisabled("No audio files in %s", Folder);
		}
		for (const FString& File : AudioFileList)
		{
			const FString Label = FFileHelper::ToUtf8Path(FFileHelper::ToNativePath(File).lexically_relative(FFileHelper::ToNativePath(Folder)));
			const bool bSelected = File == InOutPath;
			if (ImGui::Selectable(Label.c_str(), bSelected) && !bSelected)
			{
				InOutPath = File;
				bChanged = true;
			}
			if (bSelected)
			{
				ImGui::SetItemDefaultFocus();
			}
		}
		ImGui::EndCombo();
	}
	if (!InOutPath.empty() && ImGui::IsItemHovered())
	{
		ImGui::SetTooltip("%s", InOutPath.c_str());
	}

	ImGui::SameLine();
	ImGui::BeginDisabled(InOutPath.empty());
	if (ImGui::SmallButton("Play"))
	{
		if (FAudioSystem* Audio = GEngine ? GEngine->GetAudioSystem() : nullptr)
		{
			Audio->Play(InOutPath, false, 1.0f);
		}
	}
	ImGui::EndDisabled();

	return bChanged;
}

void FSkeletalMeshViewerPanel::RefreshAudioFileList(const char* Folder)
{
	AudioFileList.Reset();

	std::error_code Error;
	const std::filesystem::path Root = FFileHelper::ToNativePath(Folder);
	if (!std::filesystem::is_directory(Root, Error))
	{
		return;
	}

	for (const std::filesystem::directory_entry& Entry : std::filesystem::recursive_directory_iterator(Root, Error))
	{
		if (!Entry.is_regular_file())
		{
			continue;
		}

		FString Extension = FFileHelper::ToUtf8Path(Entry.path().extension());
		std::transform(Extension.begin(), Extension.end(), Extension.begin(),
			[](unsigned char C) { return static_cast<char>(std::tolower(C)); });
		if (Extension == ".wav" || Extension == ".mp3" || Extension == ".flac" || Extension == ".ogg")
		{
			const std::u8string GenericPath = Entry.path().lexically_normal().generic_u8string();
			AudioFileList.Add(FString(GenericPath.begin(), GenericPath.end()));
		}
	}
	std::sort(AudioFileList.begin(), AudioFileList.end());
}

void FSkeletalMeshViewerPanel::SelectPreviewNotify(int32 Index)
{
	SelectedNotifyIndex = Index;
	NotifyNameBuffer[0] = '\0';

	const UAnimSequence* Sequence = GetSelectedPreviewSequence();
	if (Sequence && Index >= 0 && Index < Sequence->Notifies.Num())
	{
		const FString Name = Sequence->Notifies[Index].NotifyName.ToString();
		snprintf(NotifyNameBuffer, sizeof(NotifyNameBuffer), "%s", Name.c_str());
	}
}

UAnimSequence* FSkeletalMeshViewerPanel::GetSelectedPreviewSequence() const
{
	if (SelectedClipIndex < 0 || SelectedClipIndex >= PreviewClips.Num())
	{
		return nullptr;
	}
	return PreviewClips[SelectedClipIndex].Sequence;
}

int32 FSkeletalMeshViewerPanel::AddPreviewNotify(float Time, float Duration)
{
	UAnimSequence* Sequence = GetSelectedPreviewSequence();
	if (!Sequence)
	{
		return -1;
	}

	const float Length = Sequence->GetPlayLength();
	FAnimNotifyEvent Notify;
	Notify.NotifyName = FName("NewNotify");
	Notify.TriggerTime = std::clamp(SnapNotifyTimeToFrame(*Sequence, Time), 0.0f, Length);
	Notify.Duration = std::clamp(Duration, 0.0f, Length - Notify.TriggerTime);
	Sequence->Notifies.Add(Notify);
	bNotifyDirty = true;

	return SortPreviewNotifies(Sequence->Notifies.Num() - 1);
}

void FSkeletalMeshViewerPanel::RemovePreviewNotify(int32 Index)
{
	UAnimSequence* Sequence = GetSelectedPreviewSequence();
	if (!FindPreviewNotify(Sequence, Index))
	{
		return;
	}

	Sequence->RemoveNotifyAt(Index);
	bNotifyDirty = true;
}

void FSkeletalMeshViewerPanel::SetPreviewNotifyTime(int32 Index, float Time, bool bSnapToFrame)
{
	UAnimSequence* Sequence = GetSelectedPreviewSequence();
	FAnimNotifyEvent* Notify = FindPreviewNotify(Sequence, Index);
	if (!Notify)
	{
		return;
	}

	if (bSnapToFrame)
	{
		Time = SnapNotifyTimeToFrame(*Sequence, Time);
	}
	const float MaxTime = std::max(0.0f, Sequence->GetPlayLength() - Notify->Duration);
	Time = std::clamp(Time, 0.0f, MaxTime);

	if (Time != Notify->TriggerTime)
	{
		Notify->TriggerTime = Time;
		bNotifyDirty = true;
	}
}

int32 FSkeletalMeshViewerPanel::SortPreviewNotifies(int32 KeepIndex)
{
	UAnimSequence* Sequence = GetSelectedPreviewSequence();
	if (!Sequence)
	{
		return -1;
	}

	TArray<FAnimNotifyEvent>& Notifies = Sequence->Notifies;
	TArray<int32> Order;
	for (int32 i = 0; i < Notifies.Num(); ++i)
	{
		Order.Add(i);
	}
	std::stable_sort(Order.begin(), Order.end(), [&Notifies](int32 A, int32 B)
	{
		return Notifies[A].TriggerTime < Notifies[B].TriggerTime;
	});

	TArray<FAnimNotifyEvent> Sorted;
	int32 NewKeepIndex = -1;
	for (int32 i = 0; i < Order.Num(); ++i)
	{
		Sorted.Add(Notifies[Order[i]]);
		if (Order[i] == KeepIndex)
		{
			NewKeepIndex = i;
		}
	}
	Notifies = std::move(Sorted);
	return NewKeepIndex;
}

void FSkeletalMeshViewerPanel::SetPreviewNotifyDuration(int32 Index, float Duration)
{
	UAnimSequence* Sequence = GetSelectedPreviewSequence();
	FAnimNotifyEvent* Notify = FindPreviewNotify(Sequence, Index);
	if (!Notify || !CanHaveNotifyDuration(*Notify))
	{
		return;
	}

	const float MaxDuration = std::max(0.0f, Sequence->GetPlayLength() - Notify->TriggerTime);
	Duration = std::clamp(Duration, 0.0f, MaxDuration);

	if (Duration != Notify->Duration)
	{
		Notify->Duration = Duration;
		bNotifyDirty = true;
	}
}

void FSkeletalMeshViewerPanel::RenamePreviewNotify(int32 Index, const FString& Name)
{
	FAnimNotifyEvent* Notify = FindPreviewNotify(GetSelectedPreviewSequence(), Index);
	if (!Notify || Name.empty())
	{
		return;
	}

	const FName NewName(Name);
	if (!(NewName == Notify->NotifyName))
	{
		Notify->NotifyName = NewName;
		bNotifyDirty = true;
	}
}

void FSkeletalMeshViewerPanel::SetPreviewNotifyClass(int32 Index, UClass* NotifyClass)
{
	UAnimSequence* Sequence = GetSelectedPreviewSequence();
	FAnimNotifyEvent* Notify = FindPreviewNotify(Sequence, Index);
	if (!Notify)
	{
		return;
	}

	const UClass* CurrentClass = Notify->Notify ? Notify->Notify->GetClass() : nullptr;
	if (CurrentClass == NotifyClass)
	{
		return;
	}

	UAnimNotify* NewNotify = nullptr;
	if (NotifyClass)
	{
		if (!NotifyClass->IsChildOf(UAnimNotify::StaticClass()))
		{
			return;
		}
		NewNotify = NewObject<UAnimNotify>(Sequence, NotifyClass);
		if (!NewNotify)
		{
			return;
		}
		NewNotify->NormalizeProperties();
	}

	Sequence->SetNotifyObject(Index, NewNotify);
	if (NewNotify && IsNotifyStateClass(NotifyClass))
	{
		if (Notify->Duration <= 0.0f)
		{
			Notify->Duration = std::max(0.0f, std::min(0.25f, Sequence->GetPlayLength() - Notify->TriggerTime));
		}
	}
	else if (NewNotify)
	{
		Notify->Duration = 0.0f;
	}
	bNotifyDirty = true;
}

void FSkeletalMeshViewerPanel::OnPreviewNotifyPropertyChanged(int32 Index, const FProperty& Property)
{
	FAnimNotifyEvent* Notify = FindPreviewNotify(GetSelectedPreviewSequence(), Index);
	if (!Notify || !Notify->Notify)
	{
		return;
	}

	Notify->Notify->NormalizeProperties(&Property);
	bNotifyDirty = true;
}

bool FSkeletalMeshViewerPanel::SavePreviewNotifies()
{
	if (!SelectedMesh)
	{
		return false;
	}

	const FFbxAssetSet* Assets = UAssetManager::Get().FindFbxAssets(SelectedMesh->GetPath());
	if (!Assets)
	{
		SaveStatusMessage = "Cannot save notifies: mesh was not loaded from an FBX.";
		SaveStatusTimer = 3.0f;
		return false;
	}

	std::filesystem::path FbxFile = FFileHelper::ToNativePath(SelectedMesh->GetPath());
	if (FbxFile.extension() == ".bin")
	{
		FbxFile.replace_extension();
	}
	if (!std::filesystem::exists(FbxFile))
	{
		FbxFile = std::filesystem::path("Assets") / FbxFile;
	}

	const FString FbxPath = FFileHelper::ToUtf8Path(FbxFile);
	if (!FAnimNotifyFile::Save(FbxPath, Assets->Animations))
	{
		SaveStatusMessage = "Failed to save notifies.";
		SaveStatusTimer = 3.0f;
		return false;
	}

	bNotifyDirty = false;
	SaveStatusMessage = "Saved notifies: " + FAnimNotifyFile::GetNotifyPath(FbxPath);
	SaveStatusTimer = 3.0f;
	return true;
}

void FSkeletalMeshViewerPanel::DrawToolbarUI()
{
	if (ImGui::BeginMenuBar())
	{
		// 1. 메시 추가 메뉴
		if (ImGui::BeginMenu("Mesh"))
		{
			if (SelectedMesh)
			{
				const bool bEditLocked = HasActivePreview();
				if (ImGui::MenuItem("Save Asset (.bin)", "Ctrl+S", false, !bEditLocked))
				{
					SaveCurrentMesh();
				}
				ShowEditTooltip(bEditLocked);
				if (ImGui::MenuItem("Save As New .bin...", nullptr, false, !bEditLocked))
				{
					bShowSaveAsModal = true;
				}
				ShowEditTooltip(bEditLocked);
				if (ImGui::MenuItem("Restore to Original FBX Pose", nullptr, false, !bEditLocked))
				{
					RestoreToOriginalFBXPose();
				}
				ShowEditTooltip(bEditLocked);
				ImGui::Separator();
			}

			for (TObjectIterator<USkeletalMesh> Itr; Itr; ++Itr)
			{
				USkeletalMesh* Mesh = *Itr;
				if (!Mesh) continue;

				FString Path = Mesh->GetPath();
				FString Name = Path.empty() ? "Mesh" : std::filesystem::path(Path).filename().string();
				const bool bCurrent = (Mesh == SelectedMesh);

				if (ImGui::MenuItem(Name.c_str(), nullptr, bCurrent))
				{
					OpenMesh(Mesh);
				}
			}
			ImGui::EndMenu();
		}

		// 2. 뷰 모드 메뉴
		if (ImGui::BeginMenu("View Mode"))
		{
			// 표시 모드: 라디오 그룹. 체크는 DisplayMode 하나로 계산하므로 항상 하나만 켜진다.
			struct FDisplayModeItem
			{
				const char* Label;
				EViewerDisplayMode Mode;
			};
			static constexpr FDisplayModeItem DisplayModeItems[] =
			{
				{ "Lit (Blinn-Phong)", EViewerDisplayMode::LitBlinnPhong },
				{ "Lit (Lambert)",     EViewerDisplayMode::LitLambert },
				{ "Unlit",             EViewerDisplayMode::Unlit },
				{ "World Normal",      EViewerDisplayMode::WorldNormal },
				{ "Bone Weight",       EViewerDisplayMode::BoneWeightHeatmap },
			};
			for (const FDisplayModeItem& Item : DisplayModeItems)
			{
				if (ImGui::MenuItem(Item.Label, nullptr, DisplayMode == Item.Mode))
				{
					DisplayMode = Item.Mode;
				}
			}

			// 독립 토글: 표시 모드와 상관없이 켜고 끈다.
			ImGui::Separator();
			ImGui::MenuItem("Wireframe", nullptr, &bWireframe);

			ImGui::EndMenu();
		}

		// 3. 윈도우 메뉴
		if (ImGui::BeginMenu("Window"))
		{
			ImGui::MenuItem("Skeletal Mesh Viewport", nullptr, &bViewportOpen);
			ImGui::MenuItem("Skeleton Tree", nullptr, &bSkeletonTreeOpen);
			ImGui::MenuItem("Asset Details", nullptr, &bAssetDetailsOpen);
			ImGui::MenuItem("Preview Scene Settings", nullptr, &bPreviewSettingsOpen);
			ImGui::MenuItem("Animation", nullptr, &bAnimationOpen);
			ImGui::MenuItem("Blend Space", nullptr, &bBlendSpaceOpen);
			ImGui::Separator();
			if (ImGui::MenuItem("Reset Layout"))
			{
				bResetLayout = true;
				bViewportOpen = true;
				bSkeletonTreeOpen = true;
				bAssetDetailsOpen = true;
				bPreviewSettingsOpen = true;
				bAnimationOpen = true;
				bBlendSpaceOpen = true;
			}
			ImGui::EndMenu();
		}

		// 4. 저장 및 복원 관련 툴바 버튼
		if (SelectedMesh)
		{
			ImGui::Separator();
			const bool bEditLocked = HasActivePreview();

			ImGui::BeginDisabled(bEditLocked);
			if (ImGui::Button("Save Asset"))
			{
				SaveCurrentMesh();
			}
			ImGui::EndDisabled();
			ShowEditTooltip(bEditLocked, "Save modified bone transforms to current .bin file (Ctrl+S)");

			ImGui::SameLine();
			ImGui::BeginDisabled(bEditLocked);
			if (ImGui::Button("Save As New .bin"))
			{
				bShowSaveAsModal = true;
			}
			ImGui::EndDisabled();
			ShowEditTooltip(bEditLocked, "Save current modified pose as a new .fbx.bin asset and register to editor");

			ImGui::SameLine();
			ImGui::BeginDisabled(bEditLocked);
			if (ImGui::Button("Restore to Original FBX Pose"))
			{
				RestoreToOriginalFBXPose();
			}
			ImGui::EndDisabled();
			ShowEditTooltip(bEditLocked, "Discard modified pose and restore .bin to original FBX bind pose");

			if (SaveStatusTimer > 0.0f)
			{
				ImGui::SameLine();
				ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.4f, 1.0f), "%s", SaveStatusMessage.c_str());
			}
		}

		ImGui::EndMenuBar();
	}

	DrawSaveAsModal();
}

void FSkeletalMeshViewerPanel::DrawMeshDetailsUI()
{
	ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.4f, 1.0f), "--- Skeletal Mesh Statistics ---");

	if (!SelectedMesh)
	{
		ImGui::TextDisabled("No Skeletal Mesh selected.");
		return;
	}

	const FSkeletalMeshData& Data = SelectedMesh->GetMeshData();

	FString Path = SelectedMesh->GetPath();
	FString DisplayName = Path.empty() ? "Skeletal Mesh" : std::filesystem::path(Path).filename().string();
	ImGui::Text("Asset: %s", DisplayName.c_str());
	ImGui::Text("Path: %s", Path.c_str());
	ImGui::Text("Bones: %d", Data.Bones.Num());
	int32 VertexCount = 0;
	int32 IndexCount = 0;
	for (const FSkeletalMesh& Part : Data.Meshes)
	{
		VertexCount += Part.Vertices.Num();
		IndexCount += Part.Indices.Num();
	}
	ImGui::Text("Vertices: %d", VertexCount);
	ImGui::Text("Triangles: %d", IndexCount / 3);
	ImGui::Text("Sections: %d", SelectedMesh->Sections.Num());
	ImGui::Text("Material Slots: %d", SelectedMesh->Materials.Num());

	const FFbxAssetSet* Assets = UAssetManager::Get().FindFbxAssets(Path);
	if (Assets && !Assets->StaticMeshes.IsEmpty() &&
		ImGui::CollapsingHeader("Separate Static Meshes", ImGuiTreeNodeFlags_DefaultOpen))
	{
		for (const FFbxStaticAsset& Part : Assets->StaticMeshes)
		{
			if (!Part.Mesh) continue;
			const FString& AssetPath = Part.Mesh->GetPath();
			const FString AssetName = AssetPath.substr(AssetPath.find_last_of('/') + 1);
			const int32 VisibleIndex = VisibleStaticMeshes.Find(Part.Mesh);
			bool bVisible = VisibleIndex != INDEX_NONE;
			ImGui::PushID(Part.Mesh);
			if (ImGui::Checkbox("##ShowStaticMesh", &bVisible))
			{
				if (bVisible)
					VisibleStaticMeshes.Add(Part.Mesh);
				else
					VisibleStaticMeshes.RemoveAt(VisibleIndex);
			}
			ImGui::SameLine();
			ImGui::TextUnformatted(AssetName.c_str());
			if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", AssetPath.c_str());
			ImGui::PopID();
		}
	}

	const FBox& Bounds = PreviewBounds;
	const FVector Size = Bounds.Max - Bounds.Min;
	ImGui::Text("Bounds Size: (%.2f, %.2f, %.2f)", Size.X, Size.Y, Size.Z);

	if (ImGui::CollapsingHeader("Material Slots", ImGuiTreeNodeFlags_DefaultOpen))
	{
		const char* Samplers[] = { "Clamp", "Wrap" };
		int32 SlotIndex = 0;
		for (const FSkeletalMesh& Part : Data.Meshes)
		{
			for (const FSkeletalMaterialSlot& Slot : Part.MaterialSlots)
			{
				ImGui::PushID(SlotIndex);
				ImGui::BulletText("[%d] %s", SlotIndex, Slot.Name.c_str());
				if (!Slot.BaseColorTexturePath.empty())
					ImGui::TextWrapped("Diffuse: %s", Slot.BaseColorTexturePath.c_str());

				if (const UMaterial* Material = GetPreviewMaterial(SlotIndex))
				{
					int Mode = Material->SamplerState == ESamplerState::LinearWrap ? 1 : 0;
					if (ImGui::Combo("Sampler", &Mode, Samplers, IM_ARRAYSIZE(Samplers)))
						SetPreviewSampler(SlotIndex, Mode == 1 ? ESamplerState::LinearWrap : ESamplerState::LinearClamp);
				}
				ImGui::PopID();
				++SlotIndex;
			}
		}
	}
}

void FSkeletalMeshViewerPanel::DrawBoneNode(int32 BoneIndex, const TArray<FSkeletalBone>& Bones)
{
	if (BoneIndex < 0 || BoneIndex >= Bones.Num())
		return;

	const FSkeletalBone& Bone = Bones[BoneIndex];

	TArray<int32> ChildIndices;
	for (int32 i = 0; i < Bones.Num(); ++i)
	{
		if (Bones[i].ParentIndex == BoneIndex)
		{
			ChildIndices.Add(i);
		}
	}

	// 뷰포트에서 본을 피킹했을 때 부모 노드를 자동으로 펼쳐주고 기본으로 상위 계층을 열어둠
	if (bScrollToSelectedBone && IsBoneAncestorOf(BoneIndex, SelectedBoneIndex, Bones))
	{
		ImGui::SetNextItemOpen(true, ImGuiCond_Always);
	}

	ImGuiTreeNodeFlags NodeFlags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick | ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_DefaultOpen;
	if (SelectedBoneIndex == BoneIndex)
	{
		NodeFlags |= ImGuiTreeNodeFlags_Selected;
	}
	if (ChildIndices.Num() == 0)
	{
		NodeFlags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
	}

	char Label[128];
	sprintf_s(Label, sizeof(Label), "[%d] %s", BoneIndex, Bone.Name.c_str());

	const bool bIsSelected = (SelectedBoneIndex == BoneIndex);
	if (bIsSelected)
	{
		ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.85f, 0.25f, 1.0f));
	}

	const bool bNodeOpen = ImGui::TreeNodeEx((void*)(intptr_t)BoneIndex, NodeFlags, "%s", Label);

	if (bIsSelected)
	{
		ImGui::PopStyleColor();
		if (bScrollToSelectedBone)
		{
			ImGui::SetScrollHereY(0.5f);
			bScrollToSelectedBone = false;
		}
	}

	if (ImGui::IsItemClicked(ImGuiMouseButton_Left))
	{
		SelectedBoneIndex = BoneIndex;
	}

	if (ChildIndices.Num() > 0 && bNodeOpen)
	{
		for (int32 ChildIdx : ChildIndices)
		{
			DrawBoneNode(ChildIdx, Bones);
		}
		ImGui::TreePop();
	}
}

void FSkeletalMeshViewerPanel::DrawBoneHierarchyUI()
{
	ImGui::TextColored(ImVec4(0.3f, 0.8f, 1.0f, 1.0f), "--- Skeleton Tree (Bone Hierarchy) ---");

	if (!SelectedMesh)
	{
		ImGui::TextDisabled("No Skeletal Mesh selected.");
		return;
	}

	const TArray<FSkeletalBone>& Bones = SelectedMesh->MeshData.Bones;
	if (Bones.Num() == 0)
	{
		ImGui::TextDisabled("No bones in this skeletal mesh.");
		return;
	}

	const bool bEditLocked = HasActivePreview();
	ImGui::BeginDisabled(bEditLocked);
	if (ImGui::Button("Reset All Bones to Bind Pose", ImVec2(-1.0f, 0.0f)))
	{
		if (PreviewComponent)
		{
			PreviewComponent->ResetPoseToBindPose();
		}
	}
	ImGui::EndDisabled();
	ShowEditTooltip(bEditLocked);

	ImGui::SetNextItemWidth(-1.0f);
	ImGui::InputTextWithHint("##BoneFilter", "Search bone name...", BoneSearchFilter, sizeof(BoneSearchFilter));

	const float FooterHeight = (SelectedBoneIndex >= 0 && SelectedBoneIndex < Bones.Num())
		? ImGui::GetFrameHeightWithSpacing() * 9.2f
		: ImGui::GetFrameHeightWithSpacing() * 1.5f;

	ImGui::BeginChild("BoneHierarchyScroll", ImVec2(0, -FooterHeight), true);
	{
		const bool bHasFilter = (strlen(BoneSearchFilter) > 0);

		if (bHasFilter)
		{
			for (int32 i = 0; i < Bones.Num(); ++i)
			{
				if (Bones[i].Name.find(BoneSearchFilter) != std::string::npos)
				{
					const bool bSelected = (SelectedBoneIndex == i);
					char Label[128];
					sprintf_s(Label, sizeof(Label), "[%d] %s (Parent: %d)", i, Bones[i].Name.c_str(), Bones[i].ParentIndex);
					if (ImGui::Selectable(Label, bSelected))
					{
						SelectedBoneIndex = i;
					}
				}
			}
		}
		else
		{
			for (int32 i = 0; i < Bones.Num(); ++i)
			{
				if (Bones[i].ParentIndex == -1)
				{
					DrawBoneNode(i, Bones);
				}
			}
		}

		// 빈 공간에서도 스크롤 플래그 클리어 보장
		bScrollToSelectedBone = false;
	}
	ImGui::EndChild();

	if (SelectedBoneIndex >= 0 && SelectedBoneIndex < Bones.Num())
	{
		const FSkeletalBone& SelBone = Bones[SelectedBoneIndex];
		ImGui::Separator();
		ImGui::TextColored(ImVec4(1.0f, 0.82f, 0.25f, 1.0f), "Selected: [%d] %s", SelectedBoneIndex, SelBone.Name.c_str());

		if (SelBone.ParentIndex >= 0 && SelBone.ParentIndex < Bones.Num())
		{
			ImGui::Text("Parent: [%d] %s", SelBone.ParentIndex, Bones[SelBone.ParentIndex].Name.c_str());
		}
		else
		{
			ImGui::Text("Parent: None (Root Bone)");
		}

		const bool bEditLocked = HasActivePreview();
		ImGui::BeginDisabled(bEditLocked);
		if (PreviewComponent)
		{
			FTransform LocalTf = GetBoneLocalTransform(SelectedBoneIndex);
			bool bChanged = false;
			bChanged |= ImGui::DragFloat3("Local Loc", &LocalTf.Location.X, 0.05f, -10000.0f, 10000.0f, "%.2f");

			float RotArr[3] = { LocalTf.Rotation.Pitch, LocalTf.Rotation.Yaw, LocalTf.Rotation.Roll };
			if (ImGui::DragFloat3("Local Rot (P/Y/R)", RotArr, 0.5f, -360.0f, 360.0f, "%.1f"))
			{
				LocalTf.Rotation.Pitch = RotArr[0];
				LocalTf.Rotation.Yaw = RotArr[1];
				LocalTf.Rotation.Roll = RotArr[2];
				bChanged = true;
			}

			bChanged |= ImGui::DragFloat3("Local Scale", &LocalTf.Scale.X, 0.01f, 0.01f, 100.0f, "%.2f");

			if (bChanged)
			{
				PreviewComponent->SetBoneLocalTransform(SelectedBoneIndex, LocalTf);
			}
		}

		if (ImGui::Button("Reset Bone", ImVec2(-1.0f, 0.0f)))
		{
			if (PreviewComponent)
			{
				PreviewComponent->SetBoneLocalTransform(SelectedBoneIndex, SelBone.RefPoseTransform);
			}
		}
		ImGui::EndDisabled();
		ShowEditTooltip(bEditLocked);
		if (ImGui::Button("Focus Bone", ImVec2(-1.0f, 0.0f)))
		{
			TArray<FMatrix> BoneMatrices;
			ComputeBoneWorldMatrices(BoneMatrices);
			if (SelectedBoneIndex < BoneMatrices.Num())
			{
				const FMatrix& Mat = BoneMatrices[SelectedBoneIndex];
				TargetPosition = FVector(Mat.M[3][0], Mat.M[3][1], Mat.M[3][2]);
				const FRotator Rot(CameraPitch, CameraYaw, 0.0f);
				CameraLocation = TargetPosition - Rot.Quaternion().GetForwardVector() * CameraDistance;
			}
		}
		if (ImGui::Button("Deselect", ImVec2(-1.0f, 0.0f)))
		{
			SelectedBoneIndex = -1;
			BoneGizmo.SetTarget(nullptr, false);
		}
	}
	else
	{
		ImGui::TextDisabled("Click a bone in the tree or viewport to inspect & manipulate.");
	}
}

void FSkeletalMeshViewerPanel::DrawLightingSettingsUI()
{
	ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.3f, 1.0f), "--- Preview Scene Settings ---");
	ImGui::TextWrapped("Hold 'L' and drag LMB in Viewport to rotate Directional Light in real-time.");
	ImGui::Spacing();

	if (ImGui::CollapsingHeader("Camera & Gizmo Settings", ImGuiTreeNodeFlags_DefaultOpen))
	{
		ImGui::SliderFloat("Camera Speed", &CameraSpeed, 1.0f, 5000.0f, "%.1f");
		ImGui::SliderFloat("Camera FOV", &FovDegrees, 20.0f, 120.0f, "%.1f deg");

		ImGui::Checkbox("Show Gizmo", &bShowBoneGizmo);
		ImGui::Text("Gizmo Mode (W / E / R):");
		const EGizmoMode CurMode = BoneGizmo.GetMode();
		if (ImGui::RadioButton("Rotate (E)", CurMode == EGizmoMode::Rotation))
		{
			BoneGizmo.SetMode(EGizmoMode::Rotation);
		}
		ImGui::SameLine();
		if (ImGui::RadioButton("Translate (W)", CurMode == EGizmoMode::Location))
		{
			BoneGizmo.SetMode(EGizmoMode::Location);
		}
		ImGui::SameLine();
		if (ImGui::RadioButton("Scale (R)", CurMode == EGizmoMode::Scale))
		{
			BoneGizmo.SetMode(EGizmoMode::Scale);
		}

		const EGizmoSpace CurSpace = BoneGizmo.GetSpace();
		ImGui::Text("Gizmo Space:");
		ImGui::SameLine();
		if (ImGui::RadioButton("Local", CurSpace == EGizmoSpace::Local))
		{
			BoneGizmo.SetSpace(EGizmoSpace::Local);
		}
		ImGui::SameLine();
		if (ImGui::RadioButton("World", CurSpace == EGizmoSpace::World))
		{
			BoneGizmo.SetSpace(EGizmoSpace::World);
		}
	}

	if (ImGui::CollapsingHeader("Skeleton Display", ImGuiTreeNodeFlags_DefaultOpen))
	{
		ImGui::Checkbox("Show Bones", &bShowBones);

		ImGui::Text("Bone Style:");
		ImGui::SameLine();
		if (ImGui::RadioButton("3D Cone", bDrawBoneCones))
		{
			bDrawBoneCones = true;
		}
		ImGui::SameLine();
		if (ImGui::RadioButton("Line", !bDrawBoneCones))
		{
			bDrawBoneCones = false;
		}

		ImGui::Checkbox("Show All Bone Names", &bShowAllBoneNames);
		if (bDrawBoneCones)
		{
			ImGui::SliderFloat("Cone Width Ratio", &BoneConeWidthRatio, 0.03f, 0.30f, "%.2f");
		}
		ImGui::SliderFloat("Joint Sphere Size", &BoneJointRadius, 0.0f, 10.0f, "%.1f");
	}

	if (ImGui::CollapsingHeader("Directional Light", ImGuiTreeNodeFlags_DefaultOpen))
	{
		ImGui::Checkbox("Enable Directional Light", &bEnableDirectionalLight);
		if (bEnableDirectionalLight)
		{
			ImGui::SliderFloat("Light Yaw", &LightYaw, 0.0f, 360.0f, "%.1f deg");
			ImGui::SliderFloat("Light Pitch", &LightPitch, -89.0f, 89.0f, "%.1f deg");
			ImGui::ColorEdit3("Light Color", &LightColor.X);
			ImGui::SliderFloat("Light Intensity", &LightIntensity, 0.0f, 10.0f, "%.2f");
		}
	}

	if (ImGui::CollapsingHeader("SkyLight & Ambient", ImGuiTreeNodeFlags_DefaultOpen))
	{
		ImGui::Checkbox("Enable SkyLight", &bEnableSkyLight);
		if (bEnableSkyLight)
		{
			ImGui::ColorEdit3("Sky Color", &SkyColor.X);
			ImGui::ColorEdit3("Ground Color", &GroundColor.X);
			ImGui::SliderFloat("Ambient Intensity", &SkyLightIntensity, 0.0f, 5.0f, "%.2f");
		}
	}

	if (ImGui::CollapsingHeader("Viewport Environment", ImGuiTreeNodeFlags_DefaultOpen))
	{
		ImGui::ColorEdit4("Clear Color", &BackgroundClearColor.X);
		ImGui::Checkbox("Show Floor Grid", &bShowGrid);
		ImGui::Checkbox("Show Wireframe", &bWireframe);
	}

	ImGui::Spacing();
	if (ImGui::Button("Reset Lighting to Defaults", ImVec2(-1.0f, 26.0f)))
	{
		ResetLighting();
	}
}

UAnimSingleNodeInstance* FSkeletalMeshViewerPanel::GetPreviewAnimInstance() const
{
	if (!PreviewComponent)
	{
		return (nullptr);
	}

	return (PreviewComponent->GetSingleNodeInstance());
}



void FSkeletalMeshViewerPanel::ClearPreviewClips()
{
	// 클립을 지우기 전에 인스턴스에서 먼저 떼어 낸다.
	if (UAnimSingleNodeInstance* AnimInstance = GetPreviewAnimInstance())
	{
		AnimInstance->SetAnimationAsset(nullptr);
	}

	// 클립은 에셋 매니저 소유이므로 목록만 비운다.
	PreviewClips.Empty();
}

void FSkeletalMeshViewerPanel::BuildPreviewClips()
{
	ClearPreviewClips();
	bNotifyDirty = false;

	if (!SelectedMesh)
	{
		return;
	}

	if (const FFbxAssetSet* Assets = UAssetManager::Get().FindFbxAssets(SelectedMesh->GetPath()))
	{
		for (UAnimSequence* Sequence : Assets->Animations)
		{
			if (!Sequence)
			{
				continue;
			}

			FPreviewClip& Clip = PreviewClips.AddDefaulted_GetRef();
			Clip.Sequence = Sequence;
			Clip.Name = Sequence->GetName();
			if (Clip.Name.empty() || Clip.Name == "None")
			{
				Clip.Name = "Clip " + std::to_string(PreviewClips.Num() - 1);
			}
		}
	}
}


void FSkeletalMeshViewerPanel::SelectPreviewClip(int32 ClipIndex)
{
	CapturePreviewPlayback();
	SelectedClipIndex = ClipIndex >= 0 && ClipIndex < PreviewClips.Num() &&
		IsPlayableClip(PreviewClips[ClipIndex].Sequence) ? ClipIndex : -1;
	SequencePlayback = FPreviewPlaybackState();
	SequencePlayback.Rate = PreviewPlaySpeed;
	if (auto* Instance = GetPreviewAnimInstance())
	{
		Instance->SetAnimationAsset(nullptr);
		Instance->SetPlaying(false);
	}
	// Force reapplication even when replacing a clip within the same mode.
	PreviewMode = EPreviewMode::None;
	ActivatePreviewMode(EPreviewMode::Sequence);
	if (SelectedClipIndex < 0 && PreviewComponent)
	{
		PreviewComponent->ResetPoseToBindPose();
		PreviewComponent->FlushPendingPose();
	}
	FiredNotifies.Empty();
	PreviewShake.reset();
	SelectedNotifyIndex = -1;
	bDraggingNotify = false;
	NotifyNameBuffer[0] = '\0';
}

bool FSkeletalMeshViewerPanel::HasPreviewClip() const
{
	return PreviewMode == EPreviewMode::Sequence && SelectedClipIndex >= 0 && SelectedClipIndex < PreviewClips.Num();
}

bool FSkeletalMeshViewerPanel::BlockEditDuringAnimationPreview()
{
	if (!HasActivePreview())
	{
		return (false);
	}

	SaveStatusMessage = "Animation preview: select None in the active playback tab to edit or save.";
	SaveStatusTimer = 3.5f;
	return (true);
}

bool FSkeletalMeshViewerPanel::IsPreviewAtEnd() const
{
	UAnimSingleNodeInstance* AnimSingleNodeInstance = GetPreviewAnimInstance();
	if (!AnimSingleNodeInstance || !HasActivePreview())
	{
		return (false);
	}

	if (AnimSingleNodeInstance->GetPlayRate() >= 0.0f)
	{
		return (AnimSingleNodeInstance->GetPosition() >= AnimSingleNodeInstance->GetLength() - 1e-4f);
	}
	return (AnimSingleNodeInstance->GetPosition() <= 1e-4f);
}

void FSkeletalMeshViewerPanel::UpdatePreviewPlaybackEnd()
{
	UAnimSingleNodeInstance* AnimSingleNodeInstance = GetPreviewAnimInstance();
	if (!AnimSingleNodeInstance || !HasActivePreview() || !AnimSingleNodeInstance->IsPlaying() || AnimSingleNodeInstance->IsLooping() || !IsPreviewAtEnd())
	{
		return;
	}
	AnimSingleNodeInstance->SetPlaying(false);
}

void FSkeletalMeshViewerPanel::TogglePreviewPlayback() 
{
	UAnimSingleNodeInstance* AnimSingleNodeInstance = GetPreviewAnimInstance();
	if (!AnimSingleNodeInstance || !HasActivePreview())
	{
		return;
	}

	if (AnimSingleNodeInstance->IsPlaying())
	{
		AnimSingleNodeInstance->SetPlaying(false);
		return;
	}

	if (!AnimSingleNodeInstance->IsLooping() && IsPreviewAtEnd())
	{
		AnimSingleNodeInstance->SetPosition(AnimSingleNodeInstance->GetPlayRate() >= 0.0f ? 0.0f : AnimSingleNodeInstance->GetLength());
	}
	AnimSingleNodeInstance->SetPlaying(true);
}

int32 FSkeletalMeshViewerPanel::GetPreviewCurrentFrame() const
{
	UAnimSingleNodeInstance* AnimSingleNodeInstance = GetPreviewAnimInstance();
	if (!AnimSingleNodeInstance || !HasPreviewClip())
	{
		return (0);
	}

	const UAnimDataModel* Model = PreviewClips[SelectedClipIndex].Sequence->GetDataModel();
	const float ClipFPS = Model->GetFrameRate().AsDecimal();
	// 정규화 시간 왕복 오차(29 → 28.99999)로 한 프레임 낮게 내림되지 않도록 여유를 둔다.
	const int32 Frame = static_cast<int32>(std::floor(AnimSingleNodeInstance->GetPosition() * ClipFPS + 1e-3f));
	return (std::clamp(Frame, 0, Model->GetNumberOfFrames()));
}

void FSkeletalMeshViewerPanel::ScrubPreviewToTime(float Seconds)
{
	UAnimSingleNodeInstance* AnimSingleNodeInstance = GetPreviewAnimInstance();
	if (!AnimSingleNodeInstance || !HasActivePreview())
	{
		return;
	}

	AnimSingleNodeInstance->SetPlaying(false);
	AnimSingleNodeInstance->SetPosition(Seconds);
	PreviewComponent->RefreshAnimationPose();
}

void FSkeletalMeshViewerPanel::ApplyPreviewPlayRate()
{
	UAnimSingleNodeInstance* AnimSingleNodeInstance = GetPreviewAnimInstance();
	if (!AnimSingleNodeInstance)
	{
		return;
	}
	AnimSingleNodeInstance->SetPlayRate(static_cast<float>(PreviewPlayDirection) * PreviewPlaySpeed);
}

void FSkeletalMeshViewerPanel::SetPreviewPlaySpeed(float Speed)
{
	PreviewPlaySpeed = std::clamp(Speed, 0.01f, 100.0f);
	ApplyPreviewPlayRate();
}


void FSkeletalMeshViewerPanel::HandlePreviewShortcuts()
{
	const ImGuiIO& IO = ImGui::GetIO();
	if (!HasActivePreview() || IO.WantTextInput || IO.KeyCtrl || IO.KeyAlt ||
		ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId) ||
		ImGui::IsMouseDown(ImGuiMouseButton_Right) || ImGui::IsMouseDown(ImGuiMouseButton_Middle) || bIsInteracting) return;
	if (!bViewportFocused && !(PreviewMode == EPreviewMode::Sequence ? bAnimationWindowFocused : bBlendSpaceWindowFocused)) return;
	if (ImGui::IsKeyPressed(ImGuiKey_Space, false)) TogglePreviewPlayback();
	if (HasPreviewClip())
	{
		if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow)) StepPreviewFrame(-1);
		if (ImGui::IsKeyPressed(ImGuiKey_RightArrow)) StepPreviewFrame(+1);
		if (bAnimationWindowFocused && SelectedNotifyIndex >= 0 && ImGui::IsKeyPressed(ImGuiKey_Delete, false))
		{
			RemovePreviewNotify(SelectedNotifyIndex);
			SelectPreviewNotify(-1);
		}
	}
}

void FSkeletalMeshViewerPanel::PlayPreviewInDirection(int32 Direction)
{
	UAnimSingleNodeInstance* AnimSingleNodeInstance = GetPreviewAnimInstance();
	if (!AnimSingleNodeInstance || !HasPreviewClip())
	{
		return;
	}

	if (AnimSingleNodeInstance->IsPlaying() && PreviewPlayDirection == Direction)
	{
		AnimSingleNodeInstance->SetPlaying(false);
		return;
	}
	PreviewPlayDirection = Direction;
	ApplyPreviewPlayRate();
	if (!AnimSingleNodeInstance->IsLooping() && IsPreviewAtEnd())
	{
		if (PreviewPlayDirection == 1)
		{
			AnimSingleNodeInstance->SetPosition(0.0f);
		}
		else
		{
			AnimSingleNodeInstance->SetPosition(AnimSingleNodeInstance->GetLength());
		}
	}
	AnimSingleNodeInstance->SetPlaying(true);
}

void FSkeletalMeshViewerPanel::StepPreviewFrame(int32 Delta)
{
	UAnimSingleNodeInstance* AnimSingleNodeInstance = GetPreviewAnimInstance();
	if (!AnimSingleNodeInstance || !HasPreviewClip() || Delta == 0)
	{
		return;
	}

	const UAnimDataModel* Model = PreviewClips[SelectedClipIndex].Sequence->GetDataModel();
	const float ClipFPS = Model->GetFrameRate().AsDecimal();
	if (ClipFPS <= 0.0f)
	{
		return;
	}

	const float ExactFrame = AnimSingleNodeInstance->GetPosition() * ClipFPS;
	const int32 CurrentFrame = static_cast<int32>(std::floor(ExactFrame + 1e-3f));

	int32 TargetFrame = CurrentFrame + Delta;
	if (Delta < 0 && (ExactFrame - static_cast<float>(CurrentFrame)) > 1e-3f)
	{
		TargetFrame = CurrentFrame + Delta + 1;
	}

	TargetFrame = std::clamp(TargetFrame, 0, Model->GetNumberOfFrames());
	ScrubPreviewToTime(static_cast<float>(TargetFrame) / ClipFPS);
}
void FSkeletalMeshViewerPanel::CapturePreviewPlayback()
{
	auto* Instance = GetPreviewAnimInstance();
	if (!Instance || PreviewMode == EPreviewMode::None) return;
	auto& State = PreviewMode == EPreviewMode::Sequence ? SequencePlayback : BlendSpacePlayback;
	const float Length = Instance->GetLength();
	State.NormalizedTime = Length > 0 ? std::clamp(Instance->GetPosition() / Length, 0.0f, 1.0f) : 0.0f;
	State.Playing = Instance->IsPlaying();
	State.Looping = Instance->IsLooping();
	State.Rate = Instance->GetPlayRate();
}

void FSkeletalMeshViewerPanel::ResetPreviewModes()
{
 StopStateMachinePreview();
 StopAnimGraphPreview();
	if (auto* Instance = GetPreviewAnimInstance())
	{
		Instance->SetAnimationAsset(nullptr);
		Instance->SetPlaying(false);
	}
	PreviewMode = EPreviewMode::None;
	SelectedClipIndex = -1;
	SelectedBlendSpace = nullptr;
	SequencePlayback = FPreviewPlaybackState();
	BlendSpacePlayback = FPreviewPlaybackState();
	BlendSpaceInput = 0.0f;
	BlendSpaceMessage.clear();
	PreviewPlayDirection = 1;
	PreviewPlaySpeed = PreviewCustomSpeed = 1.0f;
}

bool FSkeletalMeshViewerPanel::HasActivePreview() const
{
	return (PreviewMode == EPreviewMode::StateMachine && GetStateMachinePreview()) || (PreviewMode == EPreviewMode::AnimGraph && GetAnimGraphPreview()) || HasPreviewClip() || (PreviewMode == EPreviewMode::BlendSpace && SelectedBlendSpace);
}

bool FSkeletalMeshViewerPanel::IsBlendSpacePlayable() const
{
	if (!SelectedBlendSpace || SelectedBlendSpace->GetSkeletalMesh() != SelectedMesh || SelectedBlendSpace->GetNumSamples() == 0) return false;
	const auto* Axis = SelectedBlendSpace->GetBlendParameter(0);
	if (!Axis || !std::isfinite(Axis->Min) || !std::isfinite(Axis->Max) || Axis->Min >= Axis->Max) return false;
	for (const auto& Sample : SelectedBlendSpace->GetSamples())
	{
		if (!Sample.Animation || Sample.Animation->GetSkeletalMesh() != SelectedMesh || !Sample.Animation->GetDataModel()) return false;
		const float Length = Sample.Animation->GetPlayLength();
		if (!std::isfinite(Length) || Length <= 0.0f || !std::isfinite(Sample.SampleValue.X) ||
			Sample.SampleValue.X < Axis->Min || Sample.SampleValue.X > Axis->Max) return false;
	}
	return true;
}

void FSkeletalMeshViewerPanel::RefreshPreviewPose()
{
	// Zero delta updates BlendSpace weights, evaluates the pose, and flushes skinning.
	if (PreviewComponent) PreviewComponent->TickComponent(0.0f);
}

void FSkeletalMeshViewerPanel::ActivatePreviewMode(EPreviewMode Mode)
{
 if (PreviewMode == EPreviewMode::StateMachine || PreviewMode == EPreviewMode::AnimGraph) { PreviewComponent->ClearAnimInstance(); PreviewComponent->SetAnimationMode(EAnimationMode::AnimationSingleNode); PreviewComponent->InitializeAnimInstance(); PreviewMode=EPreviewMode::None; }
	auto* Instance = GetPreviewAnimInstance();
	if (!Instance) return;
	if (Mode == EPreviewMode::Sequence && (SelectedClipIndex < 0 || SelectedClipIndex >= PreviewClips.Num())) Mode = EPreviewMode::None;
	if (Mode == EPreviewMode::BlendSpace && !IsBlendSpacePlayable()) Mode = EPreviewMode::None;
	if (PreviewMode == Mode) return;
	CapturePreviewPlayback();
	PreviewMode = Mode;
	if (Mode == EPreviewMode::None)
	{
		Instance->SetAnimationAsset(nullptr);
		Instance->SetPlaying(false);
		PreviewComponent->ResetPoseToBindPose();
		PreviewComponent->FlushPendingPose();
		return;
	}
	const auto& State = Mode == EPreviewMode::Sequence ? SequencePlayback : BlendSpacePlayback;
	if (Mode == EPreviewMode::Sequence)
	{
		Instance->SetAnimationAsset(PreviewClips[SelectedClipIndex].Sequence);
		PreviewPlayDirection = State.Rate < 0 ? -1 : 1;
		PreviewPlaySpeed = std::abs(State.Rate);
		PreviewCustomSpeed = PreviewPlaySpeed;
	}
	else
	{
		Instance->SetBlendSpaceAsset(SelectedBlendSpace);
		const auto& Axis = *SelectedBlendSpace->GetBlendParameter(0);
		BlendSpaceInput = std::clamp(BlendSpaceInput, Axis.Min, Axis.Max);
		Instance->SetBlendSpaceInput(BlendSpaceInput);
	}
	Instance->SetLooping(State.Looping);
	Instance->SetPlayRate(State.Rate);
	Instance->SetPosition(State.NormalizedTime * Instance->GetLength());
	Instance->SetPlaying(State.Playing);
	RefreshPreviewPose();
}

void FSkeletalMeshViewerPanel::SelectPreviewBlendSpace(UBlendSpace1D* Asset)
{
	CapturePreviewPlayback();
	SelectedBlendSpace = Asset;
	BlendSpacePlayback = FPreviewPlaybackState();
	BlendSpaceMessage.clear();
	const auto* Axis = Asset ? Asset->GetBlendParameter(0) : nullptr;
	BlendSpaceInput = Axis ? Axis->Min : 0.0f;
	// Detach even on a same-mode selection so time and weights are reinitialized.
	if (auto* Instance = GetPreviewAnimInstance()) { Instance->SetAnimationAsset(nullptr); Instance->SetPlaying(false); }
	PreviewMode = EPreviewMode::None;
	if (Asset && !IsBlendSpacePlayable())
	{
		BlendSpaceMessage = "BlendSpace has no playable samples for this mesh. Use Edit Asset to configure it.";
		BlendSpacePlayback.Playing = false;
	}
	ActivatePreviewMode(EPreviewMode::BlendSpace);
	if (PreviewMode == EPreviewMode::None && PreviewComponent)
	{
		PreviewComponent->ResetPoseToBindPose();
		PreviewComponent->FlushPendingPose();
	}
}

void FSkeletalMeshViewerPanel::ApplyBlendSpaceInput(float Value)
{
	if (!std::isfinite(Value) || !IsBlendSpacePlayable()) return;
	ActivatePreviewMode(EPreviewMode::BlendSpace);
	const auto& Axis = *SelectedBlendSpace->GetBlendParameter(0);
	BlendSpaceInput = std::clamp(Value, Axis.Min, Axis.Max);
	if (auto* Instance = GetPreviewAnimInstance()) Instance->SetBlendSpaceInput(BlendSpaceInput);
	RefreshPreviewPose();
}

void FSkeletalMeshViewerPanel::DrawBlendSpaceGraph()
{
	if (!SelectedBlendSpace) return;
	const auto* Axis = SelectedBlendSpace->GetBlendParameter(0);
	if (!Axis || !std::isfinite(Axis->Min) || !std::isfinite(Axis->Max) || Axis->Min >= Axis->Max) return;
	const ImVec2 Origin = ImGui::GetCursorScreenPos();
	const ImVec2 Size(std::max(ImGui::GetContentRegionAvail().x, 120.0f), std::max(ImGui::GetContentRegionAvail().y - 50.0f, 100.0f));
	ImGui::InvisibleButton("##BlendSpacePreviewGraph", Size);
	const bool Hovered = ImGui::IsItemHovered();
	const float Left = Origin.x + 24, Width = std::max(Size.x - 48, 1.0f), Y = Origin.y + Size.y * .45f;
	const double Range = static_cast<double>(Axis->Max) - Axis->Min;
	auto ScreenX = [&](float X) { return Left + static_cast<float>((static_cast<double>(X) - Axis->Min) / Range) * Width; };
	if (Hovered && ImGui::GetIO().KeyCtrl && IsBlendSpacePlayable())
	{
		const double Alpha = std::clamp(static_cast<double>((ImGui::GetMousePos().x - Left) / Width), 0.0, 1.0);
		ApplyBlendSpaceInput(static_cast<float>(Axis->Min + Alpha * Range));
	}
	TArray<FBlendSampleWeight> Weights;
	SelectedBlendSpace->GetSampleWeights(FVector(BlendSpaceInput, 0, 0), Weights);
	auto* Draw = ImGui::GetWindowDrawList();
	Draw->AddRectFilled(Origin, ImVec2(Origin.x + Size.x, Origin.y + Size.y), IM_COL32(25, 27, 30, 255));
	for (int I = 0; I <= 10; ++I)
	{
		const float X = Left + Width * I / 10.0f;
		Draw->AddLine(ImVec2(X, Origin.y + 8), ImVec2(X, Origin.y + Size.y - 28), IM_COL32(55, 57, 62, 255));
		char Label[32]; snprintf(Label, sizeof(Label), "%.3g", Axis->Min + Range * I / 10.0);
		Draw->AddText(ImVec2(X - ImGui::CalcTextSize(Label).x * .5f, Origin.y + Size.y - 22), IM_COL32(180, 183, 190, 255), Label);
	}
	Draw->AddLine(ImVec2(Left, Y), ImVec2(Left + Width, Y), IM_COL32(120, 125, 130, 255));
	const bool Influence = Hovered && ImGui::GetIO().KeyCtrl && ImGui::GetIO().KeyAlt;
	for (int I = 0; I < SelectedBlendSpace->GetNumSamples(); ++I)
	{
		const auto& Sample = *SelectedBlendSpace->GetSample(I);
		const float X = ScreenX(Sample.SampleValue.X);
		float Weight = 0;
		for (const auto& Entry : Weights) if (Entry.SampleIndex == I) Weight = Entry.Weight;
		const ImU32 Color = Influence && Weight > 0 ? IM_COL32(255, 180, 65, 255) : IM_COL32(105, 180, 220, 255);
		Draw->AddQuadFilled(ImVec2(X, Y-6), ImVec2(X+6, Y), ImVec2(X, Y+6), ImVec2(X-6, Y), Color);
		if (Influence && Weight > 0)
		{
			char Label[24]; snprintf(Label, sizeof(Label), "%.1f%%", Weight * 100.0f);
			Draw->AddText(ImVec2(X-16, Y+12), Color, Label);
			Draw->AddLine(ImVec2(X,Y), ImVec2(ScreenX(BlendSpaceInput),Y), Color, 2);
		}
		if (Hovered && std::abs(ImGui::GetMousePos().x-X) < 10 && std::abs(ImGui::GetMousePos().y-Y) < 10)
		{
			ImGui::BeginTooltip();
			ImGui::TextUnformatted(Sample.Animation ? Sample.Animation->GetPath().c_str() : "Missing animation");
			ImGui::Text("X: %.6g | Weight: %.1f%%", Sample.SampleValue.X, Weight*100);
			ImGui::EndTooltip();
		}
	}
	const float X = ScreenX(std::clamp(BlendSpaceInput, Axis->Min, Axis->Max));
	const ImU32 Green = IM_COL32(80, 235, 120, 255);
	Draw->AddLine(ImVec2(X-9,Y), ImVec2(X+9,Y), Green, 2);
	Draw->AddLine(ImVec2(X,Y-9), ImVec2(X,Y+9), Green, 2);
	ImGui::TextDisabled("Ctrl: preview input | Ctrl+Alt: show sample influences");
	for (const auto& Entry : Weights)
	{
		const auto* Sample = SelectedBlendSpace->GetSample(Entry.SampleIndex);
		if (Sample && Sample->Animation) ImGui::Text("%.1f%%  %s", Entry.Weight*100, Sample->Animation->GetPath().c_str());
	}
}

void FSkeletalMeshViewerPanel::DrawBlendSpaceWindow(const ImGuiWindowClass& WindowClass)
{
 if (PreviewMode == EPreviewMode::StateMachine || PreviewMode == EPreviewMode::AnimGraph) return;
	ImGui::SetNextWindowClass(&WindowClass);
	const bool Visible = ImGui::Begin("Blend Space", &bBlendSpaceOpen, ImGuiWindowFlags_NoNavInputs);
	if (Visible)
	{
		bBlendSpaceWindowFocused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
		if (bBlendSpaceWindowFocused || (!bBlendSpaceWasVisible && ImGui::IsWindowDocked())) ActivatePreviewMode(EPreviewMode::BlendSpace);
		const auto Assets = UAssetManager::Get().GetBlendSpaceAssets(SelectedMesh);
		ImGui::SetNextItemWidth(std::min(ImGui::GetContentRegionAvail().x * .65f, 450.0f));
		if (ImGui::BeginCombo("##PreviewBlendSpace", SelectedBlendSpace ? SelectedBlendSpace->GetPath().c_str() : "None"))
		{
			if (ImGui::Selectable("None", !SelectedBlendSpace)) SelectPreviewBlendSpace(nullptr);
			for (auto* Asset : Assets)
				if (ImGui::Selectable(Asset->GetPath().c_str(), Asset == SelectedBlendSpace)) SelectPreviewBlendSpace(Asset);
			ImGui::EndCombo();
		}
		ImGui::SameLine();
		ImGui::BeginDisabled(!SelectedBlendSpace || !OnOpenBlendSpaceEditor);
		if (ImGui::Button("Edit Asset"))
		{
			const FString Path = FFileHelper::ToUtf8Path(fs::path("Assets") / FFileHelper::ToNativePath(SelectedBlendSpace->GetPath()));
			OnOpenBlendSpaceEditor(Path);
		}
		ImGui::EndDisabled();
		if (!SelectedBlendSpace)
		{
			ImGui::TextDisabled(Assets.IsEmpty() ? "No compatible BlendSpace 1D assets. Create one in the Content Drawer." : "Select a BlendSpace 1D asset to preview.");
		}
		else
		{
			const bool Playable = IsBlendSpacePlayable();
			if (!Playable && BlendSpaceMessage.empty()) BlendSpaceMessage = "No playable samples for this mesh.";
			if (Playable) BlendSpaceMessage.clear();
			if (!BlendSpaceMessage.empty()) ImGui::TextWrapped("%s", BlendSpaceMessage.c_str());
			auto* Instance = GetPreviewAnimInstance();
			ImGui::BeginDisabled(!Playable || !Instance);
			const auto* Axis = SelectedBlendSpace->GetBlendParameter(0);
			if (Axis && std::isfinite(Axis->Min) && std::isfinite(Axis->Max) && Axis->Min < Axis->Max)
			{
				float Input = BlendSpaceInput;
				ImGui::SetNextItemWidth(std::min(260.0f, ImGui::GetContentRegionAvail().x*.45f));
				const FString AxisLabel = Axis->Name.ToString() + "###BlendSpacePreviewAxis";
				if (ImGui::SliderFloat(AxisLabel.c_str(), &Input, Axis->Min, Axis->Max, "%.3f", ImGuiSliderFlags_AlwaysClamp)) ApplyBlendSpaceInput(Input);
				ImGui::SameLine(); ImGui::SetNextItemWidth(100);
				if (ImGui::InputFloat("##PreviewInput", &Input, 0, 0, "%.4f")) ApplyBlendSpaceInput(Input);
			}
			if (DrawTransportButton("##BlendFront", ETransportIcon::ToFirst, false))
			{
				ActivatePreviewMode(EPreviewMode::BlendSpace);
				Instance->SetPlaying(false); Instance->SetPosition(0); RefreshPreviewPose();
			}
			ImGui::SameLine();
			const bool Playing = PreviewMode == EPreviewMode::BlendSpace && Instance && Instance->IsPlaying();
			if (DrawTransportButton("##BlendPlay", Playing ? ETransportIcon::Pause : ETransportIcon::Play, false))
			{
				ActivatePreviewMode(EPreviewMode::BlendSpace); TogglePreviewPlayback();
			}
			ImGui::SameLine();
			bool Loop = PreviewMode == EPreviewMode::BlendSpace && Instance ? Instance->IsLooping() : BlendSpacePlayback.Looping;
			if (ImGui::Checkbox("Loop", &Loop)) { ActivatePreviewMode(EPreviewMode::BlendSpace); Instance->SetLooping(Loop); }
			ImGui::SameLine(); ImGui::SetNextItemWidth(80);
			float Rate = PreviewMode == EPreviewMode::BlendSpace && Instance ? Instance->GetPlayRate() : BlendSpacePlayback.Rate;
			if (ImGui::DragFloat("Speed###BlendSpacePlaybackRate", &Rate, .05f, .01f, 10.0f, "%.2fx", ImGuiSliderFlags_AlwaysClamp))
			{
				ActivatePreviewMode(EPreviewMode::BlendSpace); Instance->SetPlayRate(std::clamp(Rate, .01f, 10.0f));
			}
			const bool Active = PreviewMode == EPreviewMode::BlendSpace && Instance;
			TArray<FBlendSampleWeight> Weights;
			SelectedBlendSpace->GetSampleWeights(FVector(BlendSpaceInput,0,0), Weights);
			const float Length = Active ? Instance->GetLength() : SelectedBlendSpace->GetEffectivePlayLength(Weights);
			float Progress = Active && Length > 0 ? Instance->GetPosition()/Length : BlendSpacePlayback.NormalizedTime;
			ImGui::SetNextItemWidth(std::min(400.0f, ImGui::GetContentRegionAvail().x*.6f));
			if (ImGui::SliderFloat("Progress", &Progress, 0, 1, "%.3f", ImGuiSliderFlags_AlwaysClamp))
			{
				ActivatePreviewMode(EPreviewMode::BlendSpace); Instance->SetPlaying(false);
				Instance->SetPosition(Progress*Instance->GetLength()); RefreshPreviewPose();
			}
			ImGui::SameLine(); ImGui::Text("%.2fs / %.2fs", Progress*Length, Length);
			ImGui::EndDisabled();
			DrawBlendSpaceGraph();
		}
	}
	bBlendSpaceWasVisible = Visible;
	ImGui::End();
}

UAssetAnimInstance *FSkeletalMeshViewerPanel::GetStateMachinePreview() const
{
	return PreviewMode == EPreviewMode::StateMachine && PreviewComponent
			   ? Cast<UAssetAnimInstance>(PreviewComponent->GetAnimInstance())
			   : nullptr;
}
void FSkeletalMeshViewerPanel::StopStateMachinePreview()
{
	if (PreviewMode != EPreviewMode::StateMachine)
		return;
	PreviewMode = EPreviewMode::None;
	if (PreviewComponent)
	{
		PreviewComponent->ClearAnimInstance();
		PreviewComponent->SetAnimationMode(EAnimationMode::AnimationSingleNode);
		PreviewComponent->InitializeAnimInstance();
	}
}
bool FSkeletalMeshViewerPanel::PreviewStateMachine(const FAnimStateMachineAssetData &Data, FString &Error)
{
	if (!Data.Validate(Error))
		return false;
	auto *mesh = UAssetManager::GetAssetByPath<USkeletalMesh>(Data.MeshKey);
	OpenMesh(mesh);
	if (!PreviewComponent)
	{
		Error = "Preview component is unavailable.";
		return false;
	}
	PreviewComponent->ClearAnimInstance();
	PreviewComponent->SetAnimationMode(EAnimationMode::StateMachine);
	PreviewComponent->InitializeAnimInstance();
	auto *instance = Cast<UAssetAnimInstance>(PreviewComponent->GetAnimInstance());
	if (!instance || !instance->Configure(Data, false))
	{
		Error = instance ? instance->GetLastError() : "Cannot create preview instance.";
		return false;
	}
	PreviewMode = EPreviewMode::StateMachine;
	bViewportOpen = true;
	SetOpen(true);
	RequestFocus();
	PreviewComponent->TickComponent(0);
	return true;
}

UAnimGraphInstance* FSkeletalMeshViewerPanel::GetAnimGraphPreview() const
{
    return PreviewMode == EPreviewMode::AnimGraph && PreviewComponent ? Cast<UAnimGraphInstance>(PreviewComponent->GetAnimInstance()) : nullptr;
}
void FSkeletalMeshViewerPanel::StopAnimGraphPreview()
{
    if (PreviewMode != EPreviewMode::AnimGraph) return;
    PreviewMode = EPreviewMode::None;
    if (PreviewComponent) { PreviewComponent->ClearAnimInstance(); PreviewComponent->SetAnimationMode(EAnimationMode::AnimationSingleNode); PreviewComponent->InitializeAnimInstance(); }
}
bool FSkeletalMeshViewerPanel::PreviewAnimGraph(const FAnimGraphAssetData& Data, FString& Error)
{
    if (!Data.Validate(Error)) return false;
    OpenMesh(UAssetManager::GetAssetByPath<USkeletalMesh>(Data.MeshKey));
    if (!PreviewComponent) { Error = "Preview component is unavailable"; return false; }
    PreviewComponent->ClearAnimInstance();
    PreviewComponent->SetAnimationMode(EAnimationMode::AnimGraph);
    PreviewComponent->InitializeAnimInstance();
    auto* Instance = Cast<UAnimGraphInstance>(PreviewComponent->GetAnimInstance());
    if (!Instance || !Instance->Configure(Data,false)) { Error = Instance ? Instance->GetLastError() : "Cannot create AnimGraph preview"; return false; }
    PreviewMode = EPreviewMode::AnimGraph; bViewportOpen = true; SetOpen(true); RequestFocus(); PreviewComponent->TickComponent(0); return true;
}

std::unique_ptr<FSkeletalMeshViewerPanel> FSkeletalMeshViewerPanel::CreateAnimationPreviewBackend() const
{
    auto Backend=std::make_unique<FSkeletalMeshViewerPanel>();
    Backend->SetRenderer(Renderer); Backend->SetGridRenderer(GridRenderer); Backend->SetGizmoRenderer(GizmoRenderer); Backend->SetToneMapRenderer(ToneMapRenderer);
    Backend->Init(); Backend->bEmbeddedAnimationPreview=true;
    return Backend;
}
void FSkeletalMeshViewerPanel::DrawEmbeddedPreview()
{
    bViewportVisible=false;
    if(ImGui::Button("Reset Camera")) FocusOnMesh();
    ImGui::SameLine(); ImGui::TextDisabled("Alt+LMB Orbit | Wheel Zoom");
    const auto Size=ImGui::GetContentRegionAvail();
    if(Size.x<2 || Size.y<2) { HideEmbeddedPreview(); return; }
    ResizeViewport((uint32)Size.x,(uint32)Size.y);
    if(bFrameRequested) { FocusOnMesh(); bFrameRequested=false; }
    if(ColorTarget && ColorTarget->GetSRV())
    {
        CachedViewportOrigin=ImGui::GetCursorScreenPos(); CachedViewportSize=Size;
        ImGui::Image(ColorTarget->GetSRV(),Size);
        bViewportVisible=true; bViewportOpen=true; bViewportHovered=ImGui::IsItemHovered();
        bViewportFocused=ImGui::IsWindowFocused();
    }
}
