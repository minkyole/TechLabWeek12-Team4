#pragma once

#include "Core/NameTypes.h"

class UAnimSequenceBase;
class UAnimNotify;

struct FAnimNotifyEvent
{
	float TriggerTime = 0.0f;
	float Duration = 0.0f;      // 0 이면 한 번, 0 보다 크면 NotifyState
	FName NotifyName = "";
	UAnimNotify* Notify = nullptr;

	bool IsStateEvent() const;
};

struct FQueuedAnimNotify
{
	FAnimNotifyEvent Event;
	UAnimSequenceBase* Sequence = nullptr;
	float Weight = 1.0f;
};
