#include "EnginePCH.h"
#include "AnimNotify.h"

#include "Animation/AnimSequence.h"

UAnimNotify::~UAnimNotify()
{
	// 엔진 종료 시 전역 목록이 Notify를 시퀀스보다 먼저 지울 수 있으므로 소유 시퀀스의 포인터를 비운다.
	if (UAnimSequenceBase* OwnerSequence = Cast<UAnimSequenceBase>(GetOuter()))
	{
		OwnerSequence->OnNotifyObjectDestroyed(this);
	}
}

FString UAnimNotify::GetNotifyName() const
{
	return GetDisplayNameForClass(GetClass());
}

FString UAnimNotify::GetDisplayNameForClass(const UClass* Class)
{
	static const FString StatePrefix = "UAnimNotifyState_";
	static const FString Prefix = "UAnimNotify_";

	FString Name = Class ? Class->GetName() : FString("UAnimNotify");
	if (Name.starts_with(StatePrefix))
	{
		Name = Name.substr(StatePrefix.size()) + " (State)";
	}
	else if (Name.starts_with(Prefix))
	{
		Name = Name.substr(Prefix.size());
	}
	return Name;
}

bool FAnimNotifyEvent::IsStateEvent() const
{
	if (Duration <= 0.0f)
	{
		return false;
	}
	return Notify == nullptr || Cast<UAnimNotifyState>(Notify) != nullptr;
}
