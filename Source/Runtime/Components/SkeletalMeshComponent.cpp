#include "EnginePCH.h"
#include "SkeletalMeshComponent.h"
#include "Render/RenderPacket.h"

#include "Animation/AnimInstance.h"
#include "Core/Stats/SkinningStats.h"
#include "UObject/UObjectGlobals.h"
#include "Animation/AnimSequence.h"

// Debug
#include "Animation/TestAnimInstance.h"
#include "Animation/MyAnimInstance.h"
#include "Animation/LuaAnimInstance.h"
#include "Animation/AssetAnimInstance.h"
#include "Animation/AnimGraph.h"
#include "Core/Misc/FileHelper.h"
#include "Serialization/Archive.h"
#include "Animation/AnimNotifyEvent.h"
#include "GameFramework/Actor/Character.h"

USkeletalMeshComponent::USkeletalMeshComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	AnimationMode = static_cast<int>(EAnimationMode::Lua);
	AnimClass = nullptr;
}

USkeletalMeshComponent::~USkeletalMeshComponent()
{
	ClearAnimInstance();
}

void USkeletalMeshComponent::BeginPlay()
{
	Super::BeginPlay();

	UStateMachineAnimInstance* LuaInstance =
	    Cast<UStateMachineAnimInstance>(GetAnimInstance());

	if (!LuaInstance || !LuaInstance->IsGraphReady())
		return;

	LuaInstance->UpdateAnimation(0.0f);

	if (RefreshAnimationPose())
	{
		FlushPendingPose();
	}
}

void USkeletalMeshComponent::TickComponent(float DeltaTime)
{
	Super::TickComponent(DeltaTime);

	if (AnimInstance && AnimInstance->IsInitialized())
	{
		AnimInstance->DispatchQueuedAnimNotifies();
	}
}

void USkeletalMeshComponent::InitializeComponent()
{
	Super::InitializeComponent();

	InitializeAnimInstance();
}

void USkeletalMeshComponent::InitializeAnimInstance()
{
	if (IsTemplate() || !GetSkeletalMesh())
	{
		return;
	}

	if (!AnimInstance)
	{
		if (GetAnimationMode() == EAnimationMode::AnimationSingleNode)
		{
			AnimInstance.reset(NewObject<UAnimSingleNodeInstance>(this));
		}
		else
		{
			UClass* SelectedClass = nullptr;
            switch (GetAnimationMode())
            {
            case EAnimationMode::Lua: SelectedClass = ULuaAnimInstance::StaticClass(); break;
            case EAnimationMode::StateMachine: SelectedClass = UAssetAnimInstance::StaticClass(); break;
            case EAnimationMode::AnimGraph: SelectedClass = UAnimGraphInstance::StaticClass(); break;
            default: SelectedClass = AnimClass; break;
            }
			UClass* ClassToSpawn =
			    (SelectedClass && SelectedClass->IsChildOf(UAnimInstance::StaticClass()))
			        ? SelectedClass
			        : UAnimInstance::StaticClass();

			AnimInstance.reset(NewObject<UAnimInstance>(this, ClassToSpawn));
		}
	}

	if (!AnimInstance->IsInitialized())
	{
		AnimInstance->InitializeAnimation(this);

		// InitializeAnimation이 CurrentTime을 0으로 되돌리므로 그 뒤에 요청을 덮어쓴다.
		// 이미 초기화된 인스턴스에는 적용하지 않는다. 진행 중인 재생 시간을 저장값으로 되감게 된다.
		if (UAnimSingleNodeInstance* SingleNode =
		        Cast<UAnimSingleNodeInstance>(AnimInstance.get()))
		{
			AnimationData.Initialize(SingleNode);
		}

		// Tick이 돌지 않는 에디터 월드·뷰어에서도 첫 화면이 RefPose가 아니라 요청한 포즈가 되게 한다.
		if (RefreshAnimationPose())
		{
			FlushPendingPose();
		}
	}
}

void USkeletalMeshComponent::ClearAnimInstance()
{
	if (AnimInstance)
	{
		// 파생 클래스의 종료 훅을 먼저 실행한다.
		AnimInstance->UnInitializeAnimation();

		// 그 다음 객체를 삭제한다.
		AnimInstance.reset();
	}

	// 이전 인스턴스, 메시를 가리키는 평가 결과도 제거한다.
	EvaluatedPose = FPoseContext();
}

void USkeletalMeshComponent::SetSkeletalMesh(USkeletalMesh* InMesh)
{
	if (GetSkeletalMesh() == InMesh)
	{
		// 동일 메시에서 누락된 실행체의 초기화
		InitializeAnimInstance();
		return;
	}

	// 이전 메시를 참조할 수 있는 시점의 실행체 종료
	ClearAnimInstance();

	// 메시 대입 이후 가상 공통 후처리로 연결
	Super::SetSkeletalMesh(InMesh);
}

UAnimSingleNodeInstance* USkeletalMeshComponent::GetSingleNodeInstance() const
{
	return Cast<UAnimSingleNodeInstance>(AnimInstance.get());
}

void USkeletalMeshComponent::SetAnimationMode(EAnimationMode InMode)
{
	if (GetAnimationMode() == InMode)
		return;

	AnimationMode = static_cast<int>(InMode);

	// 인스턴스 타입이 모드에 묶여 있으므로 기존 실행체를 버린다.
	// AnimationData는 컴포넌트에 남아 있어 SingleNode로 돌아오면 그대로 복원된다.
	ClearAnimInstance();
	InitializeAnimInstance();
}

void USkeletalMeshComponent::SetAnimInstanceClass(UClass* InClass)
{
    if (AnimClass == InClass && GetAnimationMode() == EAnimationMode::AnimationBlueprint) return;
    if (InClass && !InClass->IsChildOf(UAnimInstance::StaticClass())) return;
    AnimClass = InClass;
    AnimationMode = static_cast<int>(EAnimationMode::AnimationBlueprint);
    ClearAnimInstance(); InitializeAnimInstance();
}

void USkeletalMeshComponent::SetAnimation(UAnimSequence* NewAnimToPlay)
{
	AnimationData.AnimToPlay = NewAnimToPlay;
	AnimationData.SavedPosition = 0.0f;

	if (UAnimSingleNodeInstance* SingleNode = GetSingleNodeInstance())
	{
		SingleNode->SetAnimationAsset(NewAnimToPlay);

		// SetAnimationAsset은 같은 클립이면 아무것도 안 하므로 위치를 되돌린다. 역재생이면 처음은 끝(Length)이다
		const float StartPosition = SingleNode->IsReversePlay() ? SingleNode->GetLength() : 0.0f;
		SingleNode->SetPosition(StartPosition);
		AnimationData.SavedPosition = StartPosition;
	}
}

void USkeletalMeshComponent::Play(bool bLooping)
{
	AnimationData.bSavedLooping = bLooping;
	AnimationData.bSavedPlaying = true;

	if (UAnimSingleNodeInstance* SingleNode = GetSingleNodeInstance())
	{
		SingleNode->SetLooping(bLooping);
		SingleNode->SetPlaying(true);
	}
}

void USkeletalMeshComponent::Stop()
{
	AnimationData.bSavedPlaying = false;

	if (UAnimSingleNodeInstance* SingleNode = GetSingleNodeInstance())
	{
		SingleNode->SetPlaying(false);
	}
}

bool USkeletalMeshComponent::IsPlaying() const
{
	// 인스턴스가 아직 없으면 "재생 요청이 걸려 있는가"를 답한다.
	if (UAnimSingleNodeInstance* SingleNode = GetSingleNodeInstance())
	{
		return SingleNode->IsPlaying();
	}
	return AnimationData.AnimToPlay != nullptr && AnimationData.bSavedPlaying;
}

void USkeletalMeshComponent::PlayAnimation(UAnimSequence* NewAnimToPlay, bool bLooping)
{
	// 모드 전환이 인스턴스를 재생성할 수 있으므로 먼저 실행체를 확정하고, 그 실행체에 요청을 건다.
	SetAnimationMode(EAnimationMode::AnimationSingleNode);
	SetAnimation(NewAnimToPlay);
	Play(bLooping);
}

void USkeletalMeshComponent::TickPose(float DeltaTime)
{
	if (!AnimInstance || !AnimInstance->IsInitialized())
		return;

	SCOPE_CYCLE_COUNTER(SkinningStats::STAT_AnimEvaluate);

	if (UStateMachineAnimInstance* LuaInstance =
	        Cast<UStateMachineAnimInstance>(AnimInstance.get()))
	{
		if (!LuaInstance->IsGraphReady())
		{
			//assert(false);
			return;
		}
	}

	AnimInstance->UpdateAnimation(DeltaTime);
	RefreshAnimationPose();
}

bool USkeletalMeshComponent::ConsumeRootMotionWorld(FVector& OutWorldDelta, FQuat& OutWorldRotation)
{
	OutWorldDelta = FVector::ZeroVector;
	OutWorldRotation = FQuat::Identity();

	const USkeletalMesh* Mesh = GetSkeletalMesh();
	if (!Mesh || !AnimInstance || !AnimInstance->IsInitialized())
	{
		return false;
	}

	const FRootMotionParams Local = AnimInstance->ConsumeRootMotion();
	if (!Local.bHasRootMotion)
	{
		return false;
	}

	// 이동량은 "루트가 바라보던 방향" 기준이고, Lock 덕분에 루트는 늘 RefPose 방향이라 그 회전을 곱하면 컴포넌트 공간이 된다
	FQuat RootRefRot = FQuat::Identity();
	for (const FSkeletalBone& Bone : Mesh->MeshData.Bones)
	{
		if (Bone.ParentIndex == -1)
		{
			RootRefRot = FQuat(Bone.RefPoseTransform.Rotation);
			break;
		}
	}
	const FVector ComponentDelta = RootRefRot.RotateVector(Local.Translation);
	const FQuat ComponentRot = RootRefRot * Local.Rotation * RootRefRot.Inverse();

	// 월드 행렬은 스케일 → 회전 순이라 같은 순서로 적용한다. FBX 단위 보정 스케일이 여기서 반영된다
	const FQuat WorldQuat = GetWorldRotation().Quaternion();
	OutWorldDelta = WorldQuat.RotateVector(ComponentDelta * GetWorldScale3D());

	// 회전 축도 컴포넌트 공간 기준이라 월드 회전으로 감싸서 월드 축으로 바꾼다
	OutWorldRotation = (WorldQuat * ComponentRot * WorldQuat.Inverse()).Normalized();
	return true;
}

void USkeletalMeshComponent::HandleAnimNotify(const FAnimNotifyEvent& Notify)
{
	if (ACharacter* Character = Cast<ACharacter>(GetOwner()))
	{
		Character->HandleAnimNotify(Notify);
	}
}

void USkeletalMeshComponent::HandleAnimNotifyStateBegin(const FAnimNotifyEvent& Notify)
{
	if (ACharacter* Character = Cast<ACharacter>(GetOwner()))
	{
		Character->HandleAnimNotifyStateBegin(Notify);
	}
}

void USkeletalMeshComponent::HandleAnimNotifyStateEnd(const FAnimNotifyEvent& Notify)
{
	if (ACharacter* Character = Cast<ACharacter>(GetOwner()))
	{
		Character->HandleAnimNotifyStateEnd(Notify);
	}
}

void USkeletalMeshComponent::HandleAnimNotifyStateTick(const FAnimNotifyEvent& Notify, float DeltaSeconds)
{
	if (ACharacter* Character = Cast<ACharacter>(GetOwner()))
	{
		Character->HandleAnimNotifyStateTick(Notify, DeltaSeconds);
	}
}

bool USkeletalMeshComponent::RefreshAnimationPose()
{
	if (!GetSkeletalMesh() ||
	    !AnimInstance ||
	    !AnimInstance->IsInitialized())
	{
		return false;
	}

	AnimInstance->EvaluateAnimation(EvaluatedPose);

	return ApplyPose(EvaluatedPose.Pose);
}

FPrimitiveSceneProxy* USkeletalMeshComponent::CreateSceneProxy()
{
	return new FSkeletalMeshSceneProxy(this);
}

void USkeletalMeshComponent::SubmitToRenderQueue(FRenderQueue& RenderQueue)
{
	USkeletalMesh* SkeletalMesh = GetSkeletalMesh();
	const FPrimitiveSceneProxy* Proxy = GetSceneProxy();
	if (!SkeletalMesh || !Proxy || !Proxy->IsVisible()) return;
	SyncSkinningMode();
	// GPU 모드면 바인드 포즈 정점 버퍼와 본 행렬 버퍼를, CPU 모드면 CPU가 스키닝한 정점 버퍼를 넘긴다.
	const bool bGPUSkinned = UsesGPUSkinning();
	FVertexBuffer* VertexBuffer = bGPUSkinned ? SkeletalMesh->VertexBuffer.get() : GetSkinnedVertexBuffer();
	FIndexBuffer* IndexBuffer = SkeletalMesh->IndexBuffer.get();
	if (!VertexBuffer || !IndexBuffer || !VertexBuffer->GetBuffer() || !IndexBuffer->GetBuffer()) return;

	// 합친 버퍼 기준으로 보정된 Section마다 기존 Queue에 패킷을 하나씩 추가한다.
	const uint32 TotalIndices = IndexBuffer->GetIndexCount();
	for (const FSkeletalMeshSection& Section : SkeletalMesh->Sections)
	{
		if (Section.IndexCount == 0 || Section.StartIndex > TotalIndices || Section.IndexCount > TotalIndices - Section.StartIndex) continue;
		if (Section.MaterialSlotIndex >= static_cast<uint32>(GetNumMaterials())) continue;
		UMaterial* Material = GetMaterial(static_cast<int32>(Section.MaterialSlotIndex));
		if (!Material) continue;

		FRenderPacket& Packet = RenderQueue.AddDefaulted_GetRef();
		Packet.Proxy = Proxy;
		Packet.VertexBuffer = VertexBuffer;
		Packet.IndexBuffer = IndexBuffer;
		if (bGPUSkinned)
		{
			Packet.SkinMatrices = GetSkinMatrixBuffer();
			Packet.NormalMatrices = GetNormalMatrixBuffer();
		}
		Packet.Material = Material;
		Packet.StartIndex = Section.StartIndex;
		Packet.IndexCount = Section.IndexCount;
	}
}

// 경로 선택값과 애니메이션 실행체 후처리의 연결
void USkeletalMeshComponent::SetStateMachineAssetPath(const FString& Path)
{
    ApplyPropertyValue(FName("StateMachineAssetPath"), &Path);
}
void USkeletalMeshComponent::SetAnimGraphAssetPath(const FString& Path)
{
    ApplyPropertyValue(FName("AnimGraphAssetPath"), &Path);
}
void USkeletalMeshComponent::SetAnimationScriptPath(const FString& Path)
{
    ApplyPropertyValue(FName("AnimationScriptPath"), &Path);
}
void USkeletalMeshComponent::NormalizeProperties(const FProperty* ChangedProperty)
{
    Super::NormalizeProperties(ChangedProperty);
    if (AnimationMode < 0 || AnimationMode > static_cast<int>(EAnimationMode::AnimGraph))
        AnimationMode = static_cast<int>(StateMachineAssetPath.empty() ? EAnimationMode::Lua : EAnimationMode::StateMachine);
}
void USkeletalMeshComponent::PostApplyProperties(const FProperty* ChangedProperty)
{
    const FName Name = ChangedProperty ? ChangedProperty->Name : FName();
    const bool Reinitialize = !ChangedProperty || Name == FName("SkeletalMesh") || Name == FName("AnimationMode") ||
        (Name == FName("StateMachineAssetPath") && GetAnimationMode() == EAnimationMode::StateMachine) ||
        (Name == FName("AnimGraphAssetPath") && GetAnimationMode() == EAnimationMode::AnimGraph) ||
        (Name == FName("AnimationScriptPath") && GetAnimationMode() == EAnimationMode::Lua);
    if (Reinitialize) ClearAnimInstance();
    Super::PostApplyProperties(ChangedProperty);
    if (Reinitialize) InitializeAnimInstance();
}
void USkeletalMeshComponent::Serialize(FStructuredArchive::FRecord Record)
{
    const bool Loading = Record.GetUnderlyingArchive().IsLoading();
    if (Loading) { ClearAnimInstance(); AnimationMode = -1; }
    Super::Serialize(Record);
    if (Loading) NormalizeProperties();
}
bool USkeletalMeshComponent::UsesAnimationAsset(const FString& Path) const
{
    auto Canonical = [](const FString& P) { return FFileHelper::ToUtf8Path(fs::weakly_canonical(FFileHelper::ToNativePath(P))); };
    const auto Key = Canonical(Path);
    if (GetAnimationMode() == EAnimationMode::AnimGraph && Canonical(AnimGraphAssetPath) == Key) return true;
    if (GetAnimationMode() == EAnimationMode::StateMachine && Canonical(StateMachineAssetPath) == Key) return true;
    if (auto* Graph = Cast<UAnimGraphInstance>(GetAnimInstance()))
        if (auto* Runtime = Graph->GetRuntime())
            for (const auto& Dependency : Runtime->GetDependencies()) if (Canonical(Dependency) == Key) return true;
    if (auto* State = Cast<UAssetAnimInstance>(GetAnimInstance()))
        return State->UsesDependency(Path);
    return false;
}
