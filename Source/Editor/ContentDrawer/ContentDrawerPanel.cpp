#include "EnginePCH.h"
#include "Editor/ContentDrawer/ContentDrawerPanel.h"
#include "Asset/AssetManager.h"
#include "Text/Font.h"
#include "Render/Mesh.h"
#include "Core/Misc/FileHelper.h"
#include "Editor/HitoriEd/EditorDragDrop.h"

namespace
{
	std::string ToUTF8String(const fs::path& Path)
	{
		const std::u8string UTF8Path = Path.generic_u8string();
		return std::string{ UTF8Path.begin(), UTF8Path.end() };
	}
}

bool FContentDrawerPanel::Init()
{
	return false;
}

void FContentDrawerPanel::Tick(float DeltaTime)
{
}

void FContentDrawerPanel::OnRender()
{
	ImGui::Begin("Content Drawer");
	std::error_code Error;

	if (!fs::is_directory(CurrentPath, Error))
	{
		CurrentPath = AssetRootPath;
	}

	Error.clear();

	if (!fs::is_directory(AssetRootPath, Error))
	{
		ImGui::TextUnformatted("Asset folder is unavailable.");
		ImGui::End();
		return;
	}

	float treeViewWidth = 200.0f;
	ImGui::BeginChild("FolderTreeView", ImVec2(treeViewWidth, 0));
	{
		// 루트 노드
		if (ImGui::TreeNodeEx(AssetRootPath.string().c_str(), ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_SpanAvailWidth |
			(AssetRootPath == CurrentPath ? ImGuiTreeNodeFlags_Selected : 0) | ImGuiTreeNodeFlags_DrawLinesToNodes))
		{
			if (ImGui::IsItemClicked())
			{
				CurrentPath = AssetRootPath;
			}
			RenderFolderTree(AssetRootPath, CurrentPath);
			ImGui::TreePop();
		}
	}
	ImGui::EndChild();

	ImGui::SameLine();

	ImGui::BeginChild("AssetGridView");
	{
		// 그리드 레이아웃 계산
		float padding = 16.0f;
		float thumbnailSize = 80.0f;
		float cellSize = thumbnailSize + padding;
		float panelWidth = ImGui::GetContentRegionAvail().x;
		int ColumnCount = static_cast<int>(panelWidth / cellSize);
		if (ColumnCount < 1) ColumnCount = 1;

		// 테이블 기반 그리드 시작
		if (ImGui::BeginTable("AssetGridTable", ColumnCount))
		{
			std::error_code IterateError;
			fs::directory_iterator It(CurrentPath, IterateError);
			const fs::directory_iterator End;

			for (; !IterateError && It != End; It.increment(IterateError))
			{
				const fs::path Path = It->path();

				std::error_code TypeError;
				const bool bIsDirectory = fs::is_directory(Path, TypeError);

				if (TypeError)
					continue;

				ImGui::TableNextColumn();

				std::string PathString = ToUTF8String(Path);
				std::string FilenameString = ToUTF8String(Path.filename());

				ImGui::PushID(PathString.c_str()); // 각 위젯에 고유 ID 부여
				UTexture2D* Thumbnail = nullptr;

				// 실제 에셋일 때만 채워진다.
				// 폴더/파일 아이콘은 끌 수 없어야 하므로 썸네일과 구분해서 들고 있는다.
				UTexture2D* TextureAsset = nullptr;
				UFont* FontAsset = nullptr;

				// 폴더 또는 파일 아이콘 표시
				if (bIsDirectory)
				{
					// Resources는 스캔하지 않으므로 경로로 불러온다. 한 번 불러오면 AssetMap에 캐시된다.
					Thumbnail = UAssetManager::Get().LoadTexture(EditorResourcePaths::FolderIcon);
				}
				else // 파일인 경우
				{
					URenderAsset* Asset = UAssetManager::GetAssetByPath<URenderAsset>(PathString);
					if (Asset && Asset->IsA<UTexture2D>())
					{
						TextureAsset = Cast<UTexture2D>(Asset);
						Thumbnail = TextureAsset;
					}
					else if (Asset && Asset->IsA<UFont>())
					{
						FontAsset = Cast<UFont>(Asset);

						// 폰트는 글리프 아틀라스를 그대로 미리보기로 쓴다
						Thumbnail = FontAsset->AtlasTexture;
					}

					if (Thumbnail == nullptr)
					{
						Thumbnail = UAssetManager::Get().LoadTexture(EditorResourcePaths::FileIcon);
					}
				}
				if (Thumbnail)
				{
					ImGui::ImageButton(PathString.c_str(), (ImTextureID)Thumbnail->GetResource()->GetSRV(), { thumbnailSize, thumbnailSize });
				}

				// 페이로드에 포인터를 그대로 실어서 받는 쪽이 AssetMap을 다시 뒤지지 않게 한다.
				if (TextureAsset && ImGui::BeginDragDropSource())
				{
					ImGui::SetDragDropPayload(EditorDragDrop::Texture, &TextureAsset, sizeof(UTexture2D*));

					// 끌고 다니는 동안 커서를 따라다니는 미리보기
					ImGui::Image((ImTextureID)TextureAsset->GetResource()->GetSRV(), { thumbnailSize, thumbnailSize });
					ImGui::TextUnformatted(FilenameString.c_str());

					ImGui::EndDragDropSource();
				}
				if (FontAsset && ImGui::BeginDragDropSource())
				{
					ImGui::SetDragDropPayload(EditorDragDrop::Font, &FontAsset, sizeof(UFont*));

					ImGui::TextUnformatted(FilenameString.c_str());

					ImGui::EndDragDropSource();
				}
				// 더블 클릭 이벤트 처리
				if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
				{
					if (bIsDirectory)
					{
						CurrentPath /= Path.filename(); // 하위 폴더로 이동
					}
					else if (OnOpenAnimGraph && _stricmp(Path.extension().string().c_str(), ".animgraph") == 0) { OnOpenAnimGraph(PathString); }
                    else if (OnOpenStateMachine && _stricmp(Path.extension().string().c_str(), ".animsm") == 0) { OnOpenStateMachine(PathString); }
					else if (OnOpenBlendSpace && _stricmp(Path.extension().string().c_str(), ".blendspace") == 0)
					{
						OnOpenBlendSpace(PathString);
					}
					else if (OnOpenSkeletalMesh && (_stricmp(Path.extension().string().c_str(), ".fbx") == 0 ||
						UAssetManager::IsFbxBakePath(Path)))
					{
						OnOpenSkeletalMesh(PathString);
					}
				}
				// 파일 이름 표시
				ImGui::TextWrapped("%s", FilenameString.c_str());

				ImGui::PopID();
			}
			ImGui::EndTable();
		}
		DrawBlendSpaceCreation();
	}
	ImGui::EndChild();
	ImGui::End();
}

void FContentDrawerPanel::RenderFolderTree(const fs::path& Path, fs::path& SelectedPath)
{
	std::error_code IterateError;
	fs::directory_iterator It(Path, IterateError);
	const fs::directory_iterator End;

	for (; !IterateError && It != End; It.increment(IterateError))
	{
		const fs::path Entry = It->path();

		std::error_code TypeError;
		const bool bIsDirectory =
			fs::is_directory(Entry, TypeError);

		if (TypeError || !bIsDirectory)
			continue;

		const FString FilenameString =
			ToUTF8String(Entry.filename());

		ImGuiTreeNodeFlags Flags =
			ImGuiTreeNodeFlags_OpenOnArrow |
			ImGuiTreeNodeFlags_SpanAvailWidth |
			ImGuiTreeNodeFlags_DrawLinesToNodes;

		if (Entry == SelectedPath)
			Flags |= ImGuiTreeNodeFlags_Selected;

		const bool bIsOpen =
			ImGui::TreeNodeEx(FilenameString.c_str(), Flags);

		if (ImGui::IsItemClicked())
			SelectedPath = Entry;

		if (bIsOpen)
		{
			RenderFolderTree(Entry, SelectedPath);
			ImGui::TreePop();
		}
	}

	if (IterateError)
	{
		ImGui::TextUnformatted("Could not read this folder.");
	}
}

void FContentDrawerPanel::DrawBlendSpaceCreation()
{
	bool Create = false, CreateStateMachine = false, CreateAnimGraph = false;
	if (ImGui::BeginPopupContextWindow("CreateAssetContext", ImGuiPopupFlags_MouseButtonRight | ImGuiPopupFlags_NoOpenOverItems))
	{
		if (ImGui::BeginMenu("Create"))
		{
			if (ImGui::BeginMenu("Animation"))
			{
				Create = ImGui::MenuItem("BlendSpace 1D");
 CreateStateMachine=ImGui::MenuItem("State Machine");
 CreateAnimGraph=ImGui::MenuItem("AnimGraph");
				ImGui::EndMenu();
			}
			ImGui::EndMenu();
		}
		ImGui::EndPopup();
	}
	if(CreateAnimGraph && OnCreateAnimGraph) OnCreateAnimGraph(FFileHelper::ToUtf8Path(CurrentPath));
	if(CreateStateMachine && OnCreateStateMachine) OnCreateStateMachine(FFileHelper::ToUtf8Path(CurrentPath));
	if (Create)
	{
		CreateFolder = CurrentPath;
		CreateError.clear();
		ImGui::OpenPopup("Create BlendSpace 1D");
	}
	if (ImGui::BeginPopupModal("Create BlendSpace 1D", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
	{
		ImGui::InputText("Name", NewBlendSpaceName, sizeof(NewBlendSpaceName));
		const auto Meshes = UAssetManager::Get().GetSkeletalMeshAssets();
		if (ImGui::BeginCombo("Skeletal Mesh", NewBlendSpaceMeshKey.empty() ? "Select a mesh" : NewBlendSpaceMeshKey.c_str()))
		{
			for (auto* Mesh : Meshes)
				if (ImGui::Selectable(Mesh->GetPath().c_str(), Mesh->GetPath() == NewBlendSpaceMeshKey)) NewBlendSpaceMeshKey = Mesh->GetPath();
			ImGui::EndCombo();
		}
		if (ImGui::Button("Create"))
		{
			const FString Name(NewBlendSpaceName);
			if (Name.empty() || Name.find_first_of("<>:\"/\\|?*.") != FString::npos || Name.back() == ' ')
				CreateError = "Enter a file name without an extension or reserved characters.";
			else
			{
				const FString Path = FFileHelper::ToUtf8Path(CreateFolder / FFileHelper::ToNativePath(Name + ".blendspace"));
				auto* Mesh = UAssetManager::GetAssetByPath<USkeletalMesh>(NewBlendSpaceMeshKey);
				if (UAssetManager::Get().CreateBlendSpace1D(Path, Mesh, CreateError))
				{
					ImGui::CloseCurrentPopup();
					if (OnOpenBlendSpace) OnOpenBlendSpace(Path);
				}
			}
		}
		ImGui::SameLine();
		if (ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
		if (!CreateError.empty()) ImGui::TextWrapped("%s", CreateError.c_str());
		ImGui::EndPopup();
	}
}
