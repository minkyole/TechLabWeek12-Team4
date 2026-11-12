#include "EnginePCH.h"
#include "Class.h"

#include "Object.h"

UClass::UClass()
    : UObject(false), PropertyRegistrationState(EPropertyRegistrationState::Registered)
{
	//HTR_LOG(Info, "Create RTTI UClass");
}

UClass::UClass(const FName& InName, UClass* InSuper, uint64 InClassSize,
               ClassConstructor InConstructor, FRegisterPropertiesFunction InRegisterProperties,
               EClassSpecifiers InSpecifiers)
    : UObject(false), Super(InSuper), ClassSize(InClassSize), Constructor(InConstructor),
      RegisterPropertiesFunction(InRegisterProperties)

{
	SetName(InName);
	ClassFlags = Super ? Super->GetClassFlags() & EClassFlags::CLASS_NotPlaceable : EClassFlags::CLASS_None;
	if (HasFlag(InSpecifiers, EClassSpecifiers::Placeable)){ ClassFlags &= ~EClassFlags::CLASS_NotPlaceable; }
	if (HasFlag(InSpecifiers, EClassSpecifiers::NotPlaceable)){ ClassFlags |= EClassFlags::CLASS_NotPlaceable; }
	if (HasFlag(InSpecifiers, EClassSpecifiers::Abstract)){	ClassFlags |= EClassFlags::CLASS_Abstract; }
}

void UClass::EnsurePropertiesRegistered()
{ 
	if (PropertyRegistrationState != EPropertyRegistrationState::Unregistered)
	{
		return;
	}

	// 부모 등록 호출 이전의 재진입 차단
	PropertyRegistrationState = EPropertyRegistrationState::Registering;

	if (Super)
	{
		Super->EnsurePropertiesRegistered();
	}

	if (RegisterPropertiesFunction)
	{
		RegisterPropertiesFunction(this);
	}

	// 프로퍼티 구성 완료 후의 클래스 검색 등록
	RegisterClass(this);
	PropertyRegistrationState = EPropertyRegistrationState::Registered;
}

const FProperty* UClass::FindPropertyByName(const FName& InName) const
{
	// 프로퍼티 등록을 보장하는 현재 클래스 우선 조회
	for (const UClass* Class = this; Class; Class = Class->Super)
	{
		for (const FProperty& Property : Class->GetProperties())
		{
			if (Property.Name == InName)
			{
				return &Property;
			}
		}
	}
	return nullptr;
}

bool UClass::IsChildOf(const UClass* BaseClass) const
{
	if (!BaseClass) return false;
	for (const UClass* c = this; c; c = c->Super)
	{
		if (c == BaseClass) return true;
	}
	return false;
}

UObject* UClass::GetDefaultObject() 
{
	// 캐시된 주소 반환과 공개 이전 재진입 차단
	if (DefaultObject || !Constructor || bCreatingDefaultObject)
	{
		return DefaultObject;
	}

	EnsurePropertiesRegistered();
	if (!ArePropertiesRegistered())
	{
		return nullptr;
	}

	FStaticConstructObjectParameters Parameters(this);
	Parameters.Outer = GetOuter();
	Parameters.Name = FName("Default__" + GetName());
	Parameters.bCreateClassDefaultObject = true;

	// 생성 완료와 실패 경로의 상태 복원
	bCreatingDefaultObject = true;
	try
	{
		DefaultObject = StaticConstructObject_Internal(Parameters);
	}
	catch (...)
	{
		DefaultObject = nullptr;
		bCreatingDefaultObject = false;
		throw;
	}

	bCreatingDefaultObject = false;
	return DefaultObject;
}


