#include "EnginePCH.h"
#include "Editor/Outliner/OutlinerPanel.h"

#include "Engine/Level.h"
#include "Engine/World.h"

#include "Components/ActorComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Editor/HitoriEd/EditorDragDrop.h"

bool FOutlinerPanel::Init()
{
    return true;
}

void FOutlinerPanel::Tick(float DeltaTime)
{
}

void FOutlinerPanel::OnRender()
{
    // World 참조 확인
    if (!World)
        return;

    // 현재 Level 가져오기
    ULevel* Level = World->GetCurrentLevel();
    if (!Level)
        return;

    // Level의 Actor 목록 Tree 출력
    ImGui::Begin("Outliner");

    DrawActors(Level);

    ImGui::End();

    if (bPendingAttach)
    {
        if (PendingAttachActor && PendingAttachActor != PendingAttachParent)
        {
            if (!PendingAttachParent)
                PendingAttachActor->DetachFromActor(EAttachmentRule::KeepWorld);
            else if (!PendingAttachActor->AttachToActor(PendingAttachParent, EAttachmentRule::KeepWorld))
                HTR_LOG(Warning, "Cannot attach {} to {}", PendingAttachActor->GetName(), PendingAttachParent->GetName());
        }

        PendingAttachActor = nullptr;
        PendingAttachParent = nullptr;
        bPendingAttach = false;
    }

    // Actors 순회가 완전히 끝난 뒤 삭제
    if (PendingDeleteActor)
    {
        CollapsedActors.Remove(PendingDeleteActor);
        if (DeleteCallback)
        {
            DeleteCallback(PendingDeleteActor);
        }

        PendingDeleteActor = nullptr;
    }
}

// Level이 가지고 있는 Actor 목록 순회하며 Draw TreeNode
void FOutlinerPanel::DrawActors(ULevel* Level)
{
    if (!Level)
        return;

    const TArray<AActor*>& Actors = Level->GetActors();

    struct FActorGroup
    {
        EPrimitiveType Type;
        const char* Name;
    };

    const FActorGroup Groups[] =
    {
        { EPrimitiveType::Sphere, "Sphere" },
        { EPrimitiveType::Cube,   "Cube" },
        { EPrimitiveType::Cone,   "Cone" },
        { EPrimitiveType::Plane,  "Plane" }
    };

    Rows.Reset();
    for (AActor* Actor : Actors)
    {
        if (!Actor)
            continue;

        AActor* ParentActor = Actor->GetAttachParentActor();
        if (!ParentActor || ParentActor->GetLevel() != Level)
            AddActorRows(Actor, 0);
    }

    ImGuiListClipper Clipper;
	Clipper.Begin(Rows.Num(), ImGui::GetTextLineHeightWithSpacing());

    while (Clipper.Step())
    {
		for (int32 Index = Clipper.DisplayStart; Index < Clipper.DisplayEnd; ++Index)
			DrawActorNode(Rows[Index]);
    }

    const ImVec2 Remaining = ImGui::GetContentRegionAvail();
    ImGui::Dummy(ImVec2(std::max(Remaining.x, 1.0f), std::max(Remaining.y, ImGui::GetTextLineHeight())));
    if (ImGui::BeginDragDropTarget())
    {
        if (const ImGuiPayload* Payload = ImGui::AcceptDragDropPayload(EditorDragDrop::Actor))
        {
            PendingAttachActor = *static_cast<AActor**>(Payload->Data);
            PendingAttachParent = nullptr;
            bPendingAttach = true;
        }
        ImGui::EndDragDropTarget();
    }
}

void FOutlinerPanel::AddActorRows(AActor* Actor, int32 Depth)
{
    TArray<AActor*> Children;
    Actor->GetAttachedActors(Children);
    Rows.Add({ Actor, Depth, !Children.IsEmpty() });

    if (CollapsedActors.Contains(Actor))
        return;

    for (AActor* Child : Children)
        AddActorRows(Child, Depth + 1);
}

// Actor를 ImGui TreeNode로 출력
void FOutlinerPanel::DrawActorNode(const FOutlinerRow& Row)
{
    AActor* Actor = Row.Actor;
    if (!Actor)
        return;

    const FString& ActorName = Actor->GetObjectLabel();

    ImGuiTreeNodeFlags Flags =
        ImGuiTreeNodeFlags_NoTreePushOnOpen |
        ImGuiTreeNodeFlags_SpanAvailWidth |
        ImGuiTreeNodeFlags_OpenOnArrow;
    if (!Row.bHasChildren)
        Flags |= ImGuiTreeNodeFlags_Leaf;

    // Actor 선택 시 UI 선택 상태
    if (SelectedObject == Actor)
        Flags |= ImGuiTreeNodeFlags_Selected;

    const float IndentWidth = Row.Depth * ImGui::GetStyle().IndentSpacing;
    if (IndentWidth > 0.0f)
        ImGui::Indent(IndentWidth);

    const bool bCollapsed = CollapsedActors.Contains(Actor);
    if (Row.bHasChildren)
        ImGui::SetNextItemOpen(!bCollapsed, ImGuiCond_Always);

    const bool bOpen = ImGui::TreeNodeEx(Actor, Flags, "%s", ActorName.c_str());
    if (Row.bHasChildren && bOpen == bCollapsed)
    {
        if (bOpen)
            CollapsedActors.Remove(Actor);
        else
            CollapsedActors.Add(Actor);
    }

    // UI에서 Actor 클릭
    if (ImGui::IsItemClicked(ImGuiMouseButton_Left))
    {
        SelectActor(Actor);
    }

    if (ImGui::BeginDragDropSource())
    {
        ImGui::SetDragDropPayload(EditorDragDrop::Actor, &Actor, sizeof(AActor*));
        ImGui::TextUnformatted(ActorName.c_str());
        ImGui::EndDragDropSource();
    }

    if (ImGui::BeginDragDropTarget())
    {
        if (const ImGuiPayload* Payload = ImGui::AcceptDragDropPayload(EditorDragDrop::Actor))
        {
            PendingAttachActor = *static_cast<AActor**>(Payload->Data);
            PendingAttachParent = Actor;
            bPendingAttach = true;
        }
        ImGui::EndDragDropTarget();
    }

    if (ImGui::BeginPopupContextItem())
    {
        if (ImGui::MenuItem("Detach", nullptr, false, Actor->GetAttachParentActor() != nullptr))
        {
            PendingAttachActor = Actor;
            PendingAttachParent = nullptr;
            bPendingAttach = true;
        }

        if (ImGui::MenuItem("Delete"))
        {
            PendingDeleteActor = Actor; // 여기서는 바로 삭제 X
        }

        ImGui::EndPopup();
    }

    if (IndentWidth > 0.0f)
        ImGui::Unindent(IndentWidth);
}

// 선택 Object 변경
void FOutlinerPanel::SelectActor(AActor* Actor)
{
    if (!Actor)
    {
        SelectedObject = nullptr;

        if (Callback)
            Callback(nullptr);

        return;
    }

    SelectedObject = Actor;

    HTR_LOG(Info, "{} UUID {} is selected", SelectedObject->GetName(), SelectedObject->GetUUID());

    USceneComponent* Scene = Cast<USceneComponent>(Actor->GetRootComponent());

    if (Callback)
        Callback(Scene);
}


AActor* FOutlinerPanel::GetSelectedActor() const
{
    return Cast<AActor>(SelectedObject);
}