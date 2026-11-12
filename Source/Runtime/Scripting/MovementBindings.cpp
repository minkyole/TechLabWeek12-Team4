#include "EnginePCH.h"
#include "Scripting/MovementBindings.h"

#include "Animation/LuaAnimInstance.h"
#include "Components/CharacterMovementComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/Actor.h"

namespace
{
	UCharacterMovementComponent* FindCharacterMovement(
	    const AActor* Actor)
	{
		if (!Actor || Actor->IsActorBeingDestroyed())
			return nullptr;

		for (UActorComponent* Component : Actor->GetComponents())
		{
			if (UCharacterMovementComponent* Movement =
			        Cast<UCharacterMovementComponent>(Component))
			{
				return Movement;
			}
		}

		return nullptr;
	}
}

void BindMovementAPI(sol::state& LuaState)
{
	LuaState.new_usertype<UCharacterMovementComponent>(
	    "CharacterMovementComponent",
	    sol::no_constructor,

	    "SetMoveInput",
	    &UCharacterMovementComponent::SetMoveInput,

	    "SetMaxSpeed",
	    &UCharacterMovementComponent::SetMaxSpeed,

	    "SetAcceleration",
	    &UCharacterMovementComponent::SetAcceleration,

	    "SetBrakingDeceleration",
	    &UCharacterMovementComponent::SetBrakingDeceleration,

	    "GetVelocity",
	    &UCharacterMovementComponent::GetVelocity,

	    "GetGroundSpeed",
	    &UCharacterMovementComponent::GetGroundSpeed,

	    "IsGrounded",
	    &UCharacterMovementComponent::IsGrounded,

		"SetVerticalVelocity",
	    &UCharacterMovementComponent::SetVerticalVelocity,

	    "GetVerticalVelocity",
	    &UCharacterMovementComponent::GetVerticalVelocity,

	    "SetGravityAcceleration",
	    &UCharacterMovementComponent::SetGravityAcceleration,

	    "SetGravityScale",
	    &UCharacterMovementComponent::SetGravityScale,

	    "StopMovementImmediately",
	    &UCharacterMovementComponent::StopMovementImmediately);

	// 캐릭터 입력 스크립트:
	// self:GetMovementComponent()
	sol::usertype<AActor> ActorType =
	    LuaState.get<sol::usertype<AActor>>("AActor");

	ActorType.set_function(
	    "GetMovementComponent",
	    [](const AActor& Actor) -> UCharacterMovementComponent*
	    {
		    return FindCharacterMovement(&Actor);
	    });

	// 애니메이션 스크립트:
	// anim:GetMovementComponent()
 auto Base = LuaState.get<sol::usertype<UAnimInstance>>("AnimInstance");
 Base.set_function("GetMovementComponent", [](const UAnimInstance& Anim)->UCharacterMovementComponent* { auto* Mesh=Anim.GetSkeletalMeshComponent();return Mesh?FindCharacterMovement(Mesh->GetOwner()):nullptr; });
 auto Common = LuaState.get<sol::usertype<UStateMachineAnimInstance>>("StateMachineAnimInstance");
 Common.set_function("GetMovementComponent", [](const UStateMachineAnimInstance& Anim)->UCharacterMovementComponent* { auto* Mesh=Anim.GetSkeletalMeshComponent();return Mesh?FindCharacterMovement(Mesh->GetOwner()):nullptr; });
	sol::usertype<ULuaAnimInstance> AnimType =
	    LuaState.get<sol::usertype<ULuaAnimInstance>>(
	        "LuaAnimInstance");

	AnimType.set_function(
	    "GetMovementComponent",
	    [](const ULuaAnimInstance& Anim)
	        -> UCharacterMovementComponent*
	    {
		    USkeletalMeshComponent* Mesh =
		        Anim.GetSkeletalMeshComponent();

		    return Mesh
		               ? FindCharacterMovement(Mesh->GetOwner())
		               : nullptr;
	    });
}