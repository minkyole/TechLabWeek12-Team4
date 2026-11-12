#include "EnginePCH.h"
#include "Object.h"

#include "Core/EngineStatics.h"
#include "UObject/Class.h"

#include "Serialization/TypeSerializer.h"
#include "Serialization/MathSerialization.h"
#include "Serialization/Formatter/BinaryArchiveFormatter.h"
#include "Asset/AssetManager.h" //??? 의존성 제거 필요



TArray<UObject*> GUObjectArray;

UObject::UObject()
{
	if (const FObjectInitializer* Initializer = FObjectInitializer::GetForObject(this))
	{
		const FStaticConstructObjectParameters& Parameters = Initializer->GetParameters();
		ClassPrivate = const_cast<UClass*>(Parameters.Class);
		Outer = Parameters.Outer;
		Name = Parameters.Name;
		Flags = Parameters.SetFlags;
		ObjectLabel = Name.ToString();
	}
	ObjectUUID = FEngineStatics::GetUUID();
	InternalIndex = GUObjectArray.Num();
	GUObjectArray.Add(this);

	// CDO와 등록을 마친 생성 중 기본 객체의 공개
	if (ClassPrivate && HasAnyFlags(EObjectFlags::RF_ClassDefaultObject) 
		&& ClassPrivate->IsCreatingDefaultObject() && !ClassPrivate->DefaultObject)
	{
		ClassPrivate->DefaultObject = this;
	}
}

UObject::UObject(bool bRegister)
{
	bIsRegistered = bRegister;
}

UObject::~UObject()
{
	if (ClassPrivate && ClassPrivate->DefaultObject == this)
	{
		ClassPrivate->DefaultObject = nullptr;
	}

	if (bIsRegistered)
	{
		if (ClassPrivate)
		{
			UnhashObject(this, ClassPrivate);
		}
		int32 LastIndex = GUObjectArray.Num() - 1;
		if (InternalIndex != LastIndex)
		{
			UObject* MovedObject = GUObjectArray[LastIndex];
			GUObjectArray[InternalIndex] = MovedObject;
			MovedObject->InternalIndex = InternalIndex; // 옮겨간 오브젝트 인덱스 갱신
		}
		GUObjectArray.RemoveLast();
	}
}

bool UObject::IsTemplate() const
{
	// 기본 객체에 속한 전체 소유 경로의 판별
	for (const UObject* Object = this; Object; Object = Object->GetOuter())
	{
		if (Object->HasAnyFlags(EObjectFlags::RF_ClassDefaultObject))
		{
			return true;
		}
	}
	return false;
}

UClass* UObject::GetPrivateStaticClass()
{
	static UClass c(
	    FName("Object"), nullptr, sizeof(UObject),
	    [](void* Memory) -> UObject*
	    {
		    return ::new (Memory) UObject();
	    },
	    nullptr);
	return &c;
}

UClass* UObject::StaticClass()
{
	UClass* c = GetPrivateStaticClass();
	c->EnsurePropertiesRegistered();
	return c;
}

bool UObject::IsA(const UClass* Class)
{
	const UClass* ThisClass = GetClass();
	return ThisClass && ThisClass->IsChildOf(Class);
}

void UObject::Serialize(FArchive& Ar)
{
	FBinaryArchiveFormatter Formatter(Ar);
	FStructuredArchive StructuredAr(Formatter);
	Serialize(StructuredAr.Open().EnterRecord());
	StructuredAr.Close();
}

// 이름 기반 일반 프로퍼티 조회와 편집값 적용
bool UObject::ApplyPropertyValue(const FName& PropertyName, const void* NewValue)
{
	const UClass* Class = GetClass();
	const FProperty* Property = Class ? Class->FindPropertyByName(PropertyName) : nullptr;

	// 편집 권한과 일반 값 입력 경로의 확인
	if (!Property || !NewValue || !Property->HasAnyFlags(CPF_Edit) || Property->HasAnyFlags(CPF_EditConst) || Property->GetObjectFunction)
	{
		return false;
	}

	// 기존 타입별 비교를 통한 동일 입력의 생략
	void* ValueAddress = Property->ContainerPtrToValuePtr(this);
	if (Property->Identical(ValueAddress, NewValue))
	{
		return false;
	}

	// 실제 타입의 복사 이후 공통 편집 보정·후처리 연결
	Property->CopyCompleteValue(ValueAddress, NewValue);
	PostEditChangeProperty(*Property);
	return true;
}

// 이름 기반 객체 프로퍼티 조회와 호환 참조 적용
bool UObject::ApplyObjectPropertyValue(const FName& PropertyName, UObject* NewValue)
{
	const UClass* Class = GetClass();
	const FProperty* Property = Class ? Class->FindPropertyByName(PropertyName) : nullptr;

	// 편집 권한과 객체 참조 연산의 확인
	if (!Property || !Property->HasAnyFlags(CPF_Edit) || Property->HasAnyFlags(CPF_EditConst) || !Property->GetObjectFunction || !Property->SetObjectFunction || !Property->Class)
	{
		return false;
	}

	// 선언된 클래스와 호환되지 않는 객체 참조의 차단
	if (NewValue && !NewValue->IsA(Property->Class))
	{
		return false;
	}

	// 실제 선언 타입의 저장 공간을 통한 현재 참조 조회
	void* ValueAddress = Property->ContainerPtrToValuePtr(this);
	if (Property->GetObjectPropertyValue(ValueAddress) == NewValue)
	{
		return false;
	}

	// 타입별 참조 대입 이후 공통 편집 보정·후처리 연결
	Property->SetObjectPropertyValue(ValueAddress, NewValue);
	PostEditChangeProperty(*Property);
	return true;
}

// 변경 프로퍼티의 값 보정 이후 외부 상태 갱신
void UObject::PostEditChangeProperty(const FProperty& Property)
{
	NormalizeProperties(&Property);
	PostApplyProperties(&Property);
}

void UObject::PostInitProperties()
{
    PostApplyProperties(nullptr);
}

// 전체 로드 대상의 값 보정 완료 이후 호출하는 후처리
void UObject::PostLoad()
{
	PostApplyProperties(nullptr);
}

void UObject::Serialize(FStructuredArchive::FRecord Record)
{
	// 인스턴스 라벨의 필수 필드 직렬화
	Record << SA_VALUE("ObjectLabel", ObjectLabel);

	// 등록된 프로퍼티의 직렬화
	SerializeScriptProperties(Record.EnterField("Properties"));
}

void UObject::SerializeScriptProperties(FStructuredArchive::FSlot Slot)
{
	// 등록된 프로퍼티의 저장 레코드
	FStructuredArchive::FRecord Properties = Slot.EnterRecord();

	for (UClass* Class = GetClass(); Class; Class = Class->Super)
	{
		for (const FProperty& Property : Class->GetProperties())
		{
			if (Property.HasAnyFlags(CPF_Transient))
			{
				continue;
			}
			// 프로퍼티 이름에 대응하는 슬롯 준비
			const FString PropertyName = Property.Name.ToString();
			TOptional<FStructuredArchive::FSlot> ValueSlot = Properties.TryEnterField(PropertyName.c_str(), true);

			if (!ValueSlot.IsSet())	{ continue; }

			// 실제 멤버 주소를 등록된 타입 연산에 전달
			void* ValueAddress = Property.ContainerPtrToValuePtr(this);
			Property.SerializeItem(ValueSlot.GetValue(), ValueAddress);
		}
	}
}



