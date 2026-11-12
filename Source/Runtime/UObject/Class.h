#pragma once

#include "Property.h"
#include "Object.h"
#include "Serialization/MathSerialization.h"
#include "Core/EngineLog.h"

using ClassConstructor = UObject * (*)(void* Memory);
using FRegisterPropertiesFunction = void (*)(UClass* Class);

class UClass : public UObject
{
public:
	UClass();

	// 클래스 기본 정보와 등록 함수의 보관
	UClass(const FName& InName, UClass* InSuper, uint64 InClassSize,
	       ClassConstructor InConstructor, FRegisterPropertiesFunction InRegisterProperties,
	       EClassSpecifiers InSpecifiers = EClassSpecifiers::None);
	
	void EnsurePropertiesRegistered();

	bool ArePropertiesRegistered() const { return PropertyRegistrationState == EPropertyRegistrationState::Registered; }

	EClassFlags GetClassFlags() const { return ClassFlags; }
	bool HasAnyClassFlags(EClassFlags InFlags) const { return HasFlag(ClassFlags, InFlags); }
	bool HasAllClassFlags(EClassFlags InFlags) const { return (ClassFlags & InFlags) == InFlags; }
	bool CanCreateInstance() const{ return Constructor != nullptr && !HasAnyClassFlags(EClassFlags::CLASS_Abstract); }

	UClass* Super = nullptr;
	uint64 ClassSize = 0;
	ClassConstructor Constructor = nullptr;

	TArray<FProperty> Properties;
	UObject* DefaultObject = nullptr;

	bool IsChildOf(const UClass* BaseClass) const;
	UObject* GetDefaultObject();
	bool IsCreatingDefaultObject() const { return bCreatingDefaultObject; }

	const TArray<FProperty>& GetProperties() const
	{
		const_cast<UClass*>(this)->EnsurePropertiesRegistered();
		return Properties;
	}

	const FProperty* FindPropertyByName(const FName& InName) const;

	template<class T>
	T* GetDefaultObject()
	{
		UObject* Object = GetDefaultObject();
		return Object ? CastChecked<T>(Object) : nullptr;
	}

	template <typename T>
	void AddProperty(const FString& InName, uint64 InOffset, uint32 InFlags = CPF_Default)
	{
		AddProperty<T>(InName, InOffset, GetPropertyType<T>(), InFlags);
	}

	template <typename T>
	void AddProperty(const FString& InName, uint64 InOffset, EPropertyType InType, uint32 InFlags = CPF_Default)
	{
		const FName PropertyName(InName);
		if (FindPropertyByName(PropertyName))
		{
			HTR_LOG(Error, "AddProperty: '{}' is already registered in '{}' or its superclass.",
			        InName, GetName());
			return;
		}
		UClass* ObjectClass = nullptr;
		if constexpr (std::is_pointer_v<T> && std::is_base_of_v<UObject, std::remove_pointer_t<T>>)
		{
			ObjectClass = std::remove_pointer_t<T>::GetPrivateStaticClass();
		}

		// 메타데이터 생성 후 실제 타입의 값 연산 연결
		FProperty Property{ PropertyName, InType, InOffset, sizeof(T), ObjectClass, InFlags };
		Property.InitializeValueOperations<T>();

		    // 실제 선언 타입에 맞는 슬롯 직렬화 연결
		Property.SerializeFunction = [](FStructuredArchive::FSlot Slot, void* ValueAddress)
		{
			T& Value = *static_cast<T*>(ValueAddress);

			if constexpr (std::is_pointer_v<T> && std::is_base_of_v<UObject, std::remove_pointer_t<T>>)
			{
				// 공통 객체 참조 직렬화 경로로 전달
				UObject* Reference = Value;
				Slot << Reference;

				if (Slot.GetUnderlyingArchive().IsLoading())
				{
					if (!Reference || Reference->IsA<std::remove_pointer_t<T>>())
					{
						Value = static_cast<T>(Reference);
					}
					else
					{
						HTR_LOG(Warning, "Load: expected {}, got {}, keeping default",
						        std::remove_pointer_t<T>::GetPrivateStaticClass()->GetName(),
						        Reference->GetClass()->GetName());
					}
				}
			}
			else
			{
				Slot << Value;
			}
		};

		Properties.Add(Property);
	}

private:
	// 기본 객체 생성 중의 재진입 상태
	bool bCreatingDefaultObject = false;
	EClassFlags ClassFlags = EClassFlags::CLASS_None;
	enum class EPropertyRegistrationState : uint8
	{
		Unregistered,
		Registering,
		Registered
	};
	FRegisterPropertiesFunction RegisterPropertiesFunction = nullptr;
	EPropertyRegistrationState PropertyRegistrationState = EPropertyRegistrationState::Unregistered;

	mutable int32 ClassUnique = 0;
	friend FName MakeUniqueObjectName(UObject*, const UClass*, FName);
};

// 클래스 계층에 등록된 프로퍼티 값 복사
inline void CopyProperties(UObject* Src, UObject* Dst, UClass* FromClass)
{
	for (UClass* Class = FromClass; Class; Class = Class->Super)
	{
		for (const FProperty& Property : Class->GetProperties())
		{
			void* Destination = Property.ContainerPtrToValuePtr(Dst);
			const void* Source = Property.ContainerPtrToValuePtr(Src);
			Property.CopyCompleteValue(Destination, Source);
		}
	}
}