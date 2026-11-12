#include "EnginePCH.h"
#include "Editor/EditorUI/EditorUI.h"

#include "imgui.h"
#include "imgui_internal.h"

bool FEditorUI::Init(bool bInUseDockSpace, bool bInPassthruCentralNode)
{
	bUseDockSpace = bInUseDockSpace;
	bPassthruCentralNode = bInPassthruCentralNode;

	Toolbar = MakeUnique<FLevelEditorToolbar>();

	return true;
}

void FEditorUI::Tick(float DeltaTime)
{
	for (auto& Panel : Panels)
	{
		Panel->Tick(DeltaTime);
	}
}

void FEditorUI::OnRender()
{
	ImGuiID RootWorkspaceDockId = 0;
	if (bUseDockSpace)
	{
		static bool dockspaceOpen = true;
		static bool optFullscreen = true;
		static bool optPadding = false;
		static ImGuiDockNodeFlags dockspaceFlags = bPassthruCentralNode
			? ImGuiDockNodeFlags_PassthruCentralNode
			: ImGuiDockNodeFlags_None;

		ImGuiWindowFlags windowFlags = ImGuiWindowFlags_NoDocking;
		if (optFullscreen)
		{
			const ImGuiViewport* viewport = ImGui::GetMainViewport();
			ImGui::SetNextWindowPos(viewport->WorkPos);
			ImGui::SetNextWindowSize(viewport->WorkSize);
			ImGui::SetNextWindowViewport(viewport->ID);
			ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
			ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
			windowFlags |= ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove;
			windowFlags |= ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus;
		}

		if (!optPadding)
			ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));

		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
		ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
		ImGui::PushStyleColor(ImGuiCol_MenuBarBg, ImVec4{ 0.0f, 0.0f, 0.0f, 0.0f });

		ImGui::Begin("DockingSpace", &dockspaceOpen, windowFlags);
		ImGui::PopStyleColor();
		ImGui::PopStyleVar(2);

		if (!optPadding)
			ImGui::PopStyleVar();

		if (optFullscreen)
			ImGui::PopStyleVar(2);

		// 최상위 루트 DockSpace: [Level Editor]와 [Skeletal Mesh Viewer]가 탭으로 도킹되는 공간
		ImGuiIO& io = ImGui::GetIO();
		if (io.ConfigFlags & ImGuiConfigFlags_DockingEnable)
		{
			ImGuiID rootDockId = ImGui::GetID("HitoriEngineRootDockSpace");
			RootWorkspaceDockId = rootDockId;

			ImGuiDockNode* RootNode = ImGui::DockBuilderGetNode(rootDockId);
			if (RootNode == nullptr || (RootNode->CentralNode == nullptr && RootNode->IsLeafNode()))
			{
				const ImGuiViewport* viewport = ImGui::GetMainViewport();
				ImVec2 rootSize(std::max(viewport->WorkSize.x, 800.0f), std::max(viewport->WorkSize.y, 600.0f));
				ImGui::DockBuilderRemoveNode(rootDockId);
				ImGui::DockBuilderAddNode(rootDockId, ImGuiDockNodeFlags_DockSpace);
				ImGui::DockBuilderSetNodeSize(rootDockId, rootSize);

				ImGui::DockBuilderDockWindow("Level Editor", rootDockId);
				ImGui::DockBuilderDockWindow("Skeletal Mesh Viewer", rootDockId);
				ImGui::DockBuilderDockWindow("Blend Space Editor", rootDockId);
				ImGui::DockBuilderDockWindow("State Machine Editor", rootDockId);
                ImGui::DockBuilderDockWindow("AnimGraph Editor", rootDockId);
				ImGui::DockBuilderFinish(rootDockId);
			}

			ImGui::DockSpace(rootDockId, ImVec2(0.0f, 0.0f), dockspaceFlags);
		}

		ImGui::End();
	}

	// 1. 레벨 에디터 전용 WindowClass 정의 (스켈레탈 메시 창들과 완전히 분리)
	ImGuiWindowClass LevelEditorClass;
	LevelEditorClass.ClassId = ImHashStr("LevelEditorClass");
	LevelEditorClass.DockingAllowUnclassed = false;

	ImGuiWindowClass LevelViewportClass = LevelEditorClass;
	LevelViewportClass.DockNodeFlagsOverrideSet = ImGuiDockNodeFlags_NoUndocking;

	// 2. [ Level Editor ] 마스터 컨테이너 창 렌더링
	DrawLevelEditorWorkspace(LevelEditorClass);

	// 3. 서브 패널 렌더링
	for (auto& Panel : Panels)
	{
		if (!Panel->IsOpen())
			continue;

		const char* Name = Panel->GetPanelName();
		const bool bIsSkelViewer = (strcmp(Name, "Skeletal Mesh Viewer") == 0);
		const bool bIsMainViewport = (strcmp(Name, "Viewports") == 0);
		const bool bIsAnimGraphEditor = (strcmp(Name, "AnimGraph Editor") == 0);

		if (strcmp(Name, "State Machine Editor") == 0 || bIsAnimGraphEditor)
		{
			// Asset editors belong to the outer workspace, not the locked Level Editor panels.
			ImGuiWindow* Window = ImGui::FindWindowByName(Name);
			ImGuiID SavedDockId = Window ? Window->DockId : 0;
			if (!Window)
				if (const ImGuiWindowSettings* Settings = ImGui::FindWindowSettingsByID(ImHashStr(Name)))
					SavedDockId = Settings->DockId;
			ImGuiDockNode* Node = ImGui::DockBuilderGetNode(SavedDockId);
			bool bWrongGroup = false;
			if (Node)
			{
				ImGuiDockNode* Root = ImGui::DockNodeGetRootNode(Node);
				bWrongGroup = (Node->MergedFlags & ImGuiDockNodeFlags_NoUndocking) != 0 ||
					Root->WindowClass.ClassId == LevelEditorClass.ClassId ||
					(Window && Window->WindowClass.ClassId == LevelEditorClass.ClassId);
			}
			if (bWrongGroup || (bIsAnimGraphEditor && bAnimGraphInitialDockPending && !Node))
			{
				if (bIsAnimGraphEditor && RootWorkspaceDockId)
				{
					const ImGuiWindow* LevelWindow = ImGui::FindWindowByName("Level Editor");
					const ImGuiID Target = LevelWindow && LevelWindow->DockId ? LevelWindow->DockId : RootWorkspaceDockId;
					ImGui::SetNextWindowDockID(Target, ImGuiCond_Always);
				}
				else
					ImGui::SetNextWindowDockID(0, ImGuiCond_Always);
			}
			if (bIsAnimGraphEditor)
				bAnimGraphInitialDockPending = false;
			ImGuiWindowClass AssetEditorClass;
			ImGui::SetNextWindowClass(&AssetEditorClass);
		}
		else if (bIsMainViewport)
		{
			ImGui::SetNextWindowClass(&LevelViewportClass);
		}
		else if (!bIsSkelViewer && strcmp(Name, "Blend Space Editor") != 0)
		{
			// 레벨 에디터 서브 패널들은 LevelEditorClass를 적용하여 오직 Level Editor 창 안에만 도킹됨!
			ImGui::SetNextWindowClass(&LevelEditorClass);
		}

		Panel->OnRender();
	}
}

void FEditorUI::DrawLevelEditorWorkspace(const ImGuiWindowClass& WindowClass)
{
	// Level Editor 창 자체는 루트 DockSpace(HitoriEngineRootDockSpace)에 탭으로 도킹됨
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
	const bool bVisible = ImGui::Begin("Level Editor", nullptr,
		ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
	ImGui::PopStyleVar();

	if (bVisible)
	{
		// 레벨 에디터 전용 상단 메뉴바 (File / Window)
		DrawMainMenuBar();

		// 레벨 에디터 전용 상단 PIE 툴바
		if (Toolbar)
		{
			Toolbar->Draw();
		}
	}

	// 레벨 에디터 내부 전용 DockSpace
	ImGuiID levelDockId = ImGui::GetID("LevelEditorInnerDockSpace");

	ImGuiDockNode* LevelNode = ImGui::DockBuilderGetNode(levelDockId);
	ImGuiWindow* ViewportsWin = ImGui::FindWindowByName("Viewports");
	const bool bViewportsOrphaned = (ViewportsWin != nullptr && ViewportsWin->DockNode == nullptr && ViewportsWin->WasActive);

	// 초기 상태이거나, 이전 크래시 ini로 인해 Viewports가 도킹에서 이탈했거나, 리셋 요청 시 레이아웃 재구성
	const bool bNeedBuild = (LevelNode == nullptr)
		|| bResetLevelLayout
		|| (LevelNode->IsLeafNode() && LevelNode->Windows.Size == 0)
		|| bViewportsOrphaned;

	if (bNeedBuild)
	{
		bResetLevelLayout = false;
		const ImGuiViewport* vp = ImGui::GetMainViewport();
		ImVec2 avail = ImGui::GetContentRegionAvail();
		ImVec2 dockSize(
			avail.x > 100.0f ? avail.x : std::max(vp->WorkSize.x, 800.0f),
			avail.y > 100.0f ? avail.y : std::max(vp->WorkSize.y, 600.0f));

		ImGui::DockBuilderRemoveNode(levelDockId);
		ImGui::DockBuilderAddNode(levelDockId, ImGuiDockNodeFlags_DockSpace);
		ImGui::DockBuilderSetNodeSize(levelDockId, dockSize);

		ImGuiID CenterNode = levelDockId;
		ImGuiID DownNode = ImGui::DockBuilderSplitNode(CenterNode, ImGuiDir_Down, 0.25f, nullptr, &CenterNode);
		ImGuiID RightNode = ImGui::DockBuilderSplitNode(CenterNode, ImGuiDir_Right, 0.25f, nullptr, &CenterNode);
		ImGuiID RightBottomNode = ImGui::DockBuilderSplitNode(RightNode, ImGuiDir_Down, 0.65f, nullptr, &RightNode);
		ImGuiID DownRightNode = ImGui::DockBuilderSplitNode(DownNode, ImGuiDir_Right, 0.40f, nullptr, &DownNode);

		ImGui::DockBuilderDockWindow("Viewports", CenterNode);
		ImGui::DockBuilderDockWindow("Outliner", RightNode);
		ImGui::DockBuilderDockWindow("Details", RightBottomNode);
		ImGui::DockBuilderDockWindow("Editor Controls", RightNode);
		ImGui::DockBuilderDockWindow("Settings", RightNode);
		ImGui::DockBuilderDockWindow("Content Drawer", DownNode);
		ImGui::DockBuilderDockWindow("Sequence", DownNode);
		ImGui::DockBuilderDockWindow("Output Log", DownRightNode);
		ImGui::DockBuilderFinish(levelDockId);
	}

	// 중요: Level Editor 탭이 비활성(SkipItems) 상태이더라도 DockSpace()는 매 프레임 호출해야
	// 내부적으로 KeepAliveOnly가 동작하여 자식 패널들이 도킹 해제(Undock)되지 않음
	ImGui::DockSpace(levelDockId, ImVec2(0.0f, 0.0f), ImGuiDockNodeFlags_None, &WindowClass);
	ImGui::End();
}

void FEditorUI::DrawMainMenuBar()
{
	if (ImGui::BeginMenuBar())
	{
		if (ImGui::BeginMenu("File"))
		{
			if (ImGui::MenuItem("New Scene")) { if (OnNewScene) OnNewScene(); }
			if (ImGui::MenuItem("Open Scene...")) { if (OnOpenScene) OnOpenScene(); }

			ImGui::Separator();

			if (ImGui::MenuItem("Save")) { if (OnSaveScene) OnSaveScene(); }
			if (ImGui::MenuItem("Save As...")) { if (OnSaveSceneAs) OnSaveSceneAs(); }

			ImGui::EndMenu();
		}

		if (ImGui::BeginMenu("Window"))
		{
			for (auto& Panel : Panels)
			{
				const char* Name = Panel->GetPanelName();
				bool bOpen = Panel->IsOpen();
				if (ImGui::MenuItem(Name, nullptr, bOpen))
				{
					Panel->SetOpen(!bOpen);
				}
			}

			ImGui::Separator();
			if (ImGui::MenuItem("Reset Level Editor Layout"))
			{
				bResetLevelLayout = true;
			}

			ImGui::EndMenu();
		}

		ImGui::EndMenuBar();
	}
}
