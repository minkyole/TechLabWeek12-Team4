#pragma once

#include "UObject/Object.h"
#include "UObject/Class.h"

class USkeletalMeshComponent;
class UAnimSequenceBase;
struct FProperty;

class UAnimNotify : public UObject
{
	DECLARE_CLASS(UAnimNotify, UObject)

public:
	UAnimNotify() = default;
	~UAnimNotify() override;

	virtual void Notify(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation) {}
	virtual FString GetNotifyName() const;

	virtual void NormalizeProperties(const FProperty* ChangedProperty = nullptr) {}

	// 문자열 프로퍼티를 이 폴더의 파일 목록에서 고르게 할 때 재정의한다. nullptr이면 일반 텍스트 입력.
	virtual const char* GetPropertyAssetFolder(const FProperty& Property) const { return nullptr; }

	static FString GetDisplayNameForClass(const UClass* Class);
};
