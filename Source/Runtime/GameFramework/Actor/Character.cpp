#include "EnginePCH.h"
#include "Character.h"

#include "Animation/LuaAnimInstance.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/CharacterMovementComponent.h"
#include "Components/ScriptComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Animation/AnimNotifyEvent.h"

ACharacter::ACharacter()
{
	// ----- 충돌 및 이동 기준 -----

	CapsuleComponent =
	    CreateDefaultSubobject<UCapsuleComponent>(
	        "CapsuleComponent");

	SetRootComponent(CapsuleComponent);

	// 현재 프로젝트 기본 크기 기준의 예시 값.
	// 실제 메시 크기에 맞게 조정한다.
	CapsuleComponent->SetCapsuleSize(
	    0.5f,  // Radius
	    1.0f); // HalfHeight, 반구 포함

	CapsuleComponent->SetBlockComponent(true);

	// 겹침 이벤트를 사용할 계획이면 활성화.
	CapsuleComponent->SetGenerateOverlapEvents(true);


	// ----- 캐릭터 외형 -----

	SkeletalMeshComponent =
	    CreateDefaultSubobject<USkeletalMeshComponent>(
	        "SkeletalMeshComponent");

	SkeletalMeshComponent->AttachToComponent(
	    CapsuleComponent,
	    EAttachmentRule::KeepRelative);

	// 충돌은 Capsule이 담당한다.
	SkeletalMeshComponent->SetBlockComponent(false);
	SkeletalMeshComponent->SetGenerateOverlapEvents(false);

	SkeletalMeshComponent->SetAnimationMode(
	    EAnimationMode::Lua);


	SkeletalMeshComponent->PrimaryComponentTick.TickGroup =
	    ETickingGroup::PostPhysics;


	// ----- 이동 -----

	MovementComponent =
	    CreateDefaultSubobject<UCharacterMovementComponent>(
	        "CharacterMovementComponent");

	MovementComponent->SetMaxSpeed(5.0f);

	// Movement 생성자에서 이미 지정했다면 생략 가능.
	MovementComponent->PrimaryComponentTick.TickGroup =
	    ETickingGroup::DuringPhysics;


	// ----- Lua 입력 -----

	ScriptComponent =
	    CreateDefaultSubobject<UScriptComponent>(
	        "ScriptComponent");

	ScriptComponent->PrimaryComponentTick.TickGroup =
	    ETickingGroup::PrePhysics;


	// ----- 카메라 -----

	CameraComponent =
	    CreateDefaultSubobject<UCameraComponent>(
	        "CameraComponent");

	CameraComponent->AttachToComponent(
	    CapsuleComponent,
	    EAttachmentRule::KeepRelative);

	// 3인칭 배치 예시. 실제 모델 크기에 맞게 조정한다.
	CameraComponent->SetRelativeLocation(
	    FVector(-3.0f, 0.0f, 1.5f));

	// 기본 카메라 자체의 WASD/마우스 입력을 막고
	// 캐릭터 Lua에서 시점을 제어한다.
	CameraComponent->SetExternalInputManaged(true);
}

void ACharacter::BeginPlay()
{
	if (GetRootComponent() != CapsuleComponent)
	{
		HTR_LOG(
		    Warning,
		    "[Character] {}: CapsuleComponent should be the root.",
		    GetName());
	}

	if (!SkeletalMeshComponent->GetSkeletalMesh())
	{
		HTR_LOG(
		    Warning,
		    "[Character] {}: Skeletal mesh is not assigned.",
		    GetName());
	}

	if (ScriptComponent->GetScriptPath().empty())
	{
		HTR_LOG(
		    Warning,
		    "[Character] {}: Input script is not assigned.",
		    GetName());
	}

	// Movement가 Root를 UpdatedComponent로 선택하고,
	// Mesh가 AnimInstance를 초기화하며,
	// Script가 Lua BeginPlay를 호출한다.
	Super::BeginPlay();

	if (UStateMachineAnimInstance* Anim = GetStateMachineAnimInstance())
	{
		if (!Anim->IsGraphReady())
		{
			HTR_LOG(
			    Error,
			    "[Character] {}: Animation graph initialization failed: {}",
			    GetName(),
			    Anim->GetLastError());
		}
	}
}

ULuaAnimInstance* ACharacter::GetLuaAnimInstance() const
{
	if (!SkeletalMeshComponent)
		return nullptr;

	return Cast<ULuaAnimInstance>(
	    SkeletalMeshComponent->GetAnimInstance());
}

void ACharacter::HandleAnimNotify(const FAnimNotifyEvent& Notify)
{
	if (UScriptComponent* Script = ScriptComponent)
	{
		Script->OnAnimNotify(Notify.NotifyName.ToString());
	}
}

void ACharacter::HandleAnimNotifyStateBegin(const FAnimNotifyEvent& Notify)
{
	if (UScriptComponent* Script = ScriptComponent)
	{
		Script->OnAnimNotifyBegin(Notify.NotifyName.ToString());
	}
}

void ACharacter::HandleAnimNotifyStateEnd(const FAnimNotifyEvent& Notify)
{
	if (UScriptComponent* Script = ScriptComponent)
	{
		Script->OnAnimNotifyEnd(Notify.NotifyName.ToString());
	}
}

UStateMachineAnimInstance* ACharacter::GetStateMachineAnimInstance() const { return SkeletalMeshComponent ? Cast<UStateMachineAnimInstance>(SkeletalMeshComponent->GetAnimInstance()) : nullptr; }
