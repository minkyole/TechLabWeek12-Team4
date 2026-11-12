#pragma once

#include "Core/EngineString.h"
#include "Core/NameTypes.h"
#include "Math/EngineMath.h"
#include "Math/Color.h"
#include "Math/Transform.h"
#include <type_traits>
#include "Serialization/StructuredArchive.h"
#include "Serialization/StructuredArchiveSlots.h"

class UMaterial;
class UFont;
class UObject;
class UClass;

enum class EPropertyType
{
	Unknown,
	Float,
	Int,
	String,
	Bool,
	Vector,
	Vector4,
	Color,
	LinearColor,
	Rotator,
	Transform,
	Object,
	UInt32,
	Name
};

template <typename T>
constexpr EPropertyType GetPropertyType()
{
    if constexpr (std::is_pointer_v<T> &&
        std::is_base_of_v<UObject, std::remove_pointer_t<T>>)
    {
        return EPropertyType::Object;
    }
    else
    {
        static_assert(sizeof(T) == 0, "Not Valid Type");
        return EPropertyType::Unknown;
    }
}

#define DEFINE_PROPERTY_TYPE(CppType, EnumValue)                    \
    template <> constexpr EPropertyType GetPropertyType<CppType>()  \
    { return EPropertyType::EnumValue; }

DEFINE_PROPERTY_TYPE(int, Int)
DEFINE_PROPERTY_TYPE(uint32, UInt32)
DEFINE_PROPERTY_TYPE(float, Float)
DEFINE_PROPERTY_TYPE(bool, Bool)
DEFINE_PROPERTY_TYPE(FString, String)
DEFINE_PROPERTY_TYPE(FVector, Vector)
DEFINE_PROPERTY_TYPE(FVector4, Vector4)
DEFINE_PROPERTY_TYPE(FTransform, Transform)
DEFINE_PROPERTY_TYPE(FRotator, Rotator)
DEFINE_PROPERTY_TYPE(FColor, Color)
DEFINE_PROPERTY_TYPE(FLinearColor, LinearColor)
DEFINE_PROPERTY_TYPE(FName, Name)

// 프로퍼티 지정자 (UE의 UPROPERTY 지정자에 대응)
enum EPropertyFlags : uint32
{
    CPF_None      = 0,
    CPF_Edit      = 1 << 0, // 디테일 패널에 노출
    CPF_EditConst = 1 << 1, // 노출하되 수정 불가
    CPF_Transient = 1 << 2, // 직렬화에서 제외

    CPF_EditAnywhere    = CPF_Edit,
    CPF_VisibleAnywhere = CPF_Edit | CPF_EditConst,
    CPF_Default         = CPF_EditAnywhere,
};

struct FProperty
{
	// 복사함수
	using FCopyValueFunction = void (*)(void* Destination, const void* Source);
	// 비교함수
	using FIdenticalValueFunction = bool (*)(const void* Left, const void* Right);
	// 직렬화 함수
	using FSerializeValueFunction = void (*)(FStructuredArchive::FSlot Slot, void* ValueAddress);
    // 객체 읽기 함수
	using FGetObjectValueFunction = UObject* (*)(const void* ValueAddress);
	// 객체 쓰기 함수
	using FSetObjectValueFunction = void (*)(void* ValueAddress, UObject* Object);
	FName Name;
    EPropertyType Type;
    size_t Offset;
    size_t Size;
    UClass* Class = nullptr;
    uint32 Flags = CPF_Default;

    bool HasAnyFlags(uint32 InFlags) const { return (Flags & InFlags) != 0; }

	FCopyValueFunction CopyFunction = nullptr;
	FIdenticalValueFunction IdenticalFunction = nullptr;
	FSerializeValueFunction SerializeFunction = nullptr;
	FGetObjectValueFunction GetObjectFunction = nullptr;
	FSetObjectValueFunction SetObjectFunction = nullptr;

	// 객체 내부의 프로퍼티 값 주소 조회
	template <typename TValueType = void>
	TValueType* ContainerPtrToValuePtr(void* Container) const
	{
		return reinterpret_cast<TValueType*>(static_cast<uint8*>(Container) + Offset);
	}

	// 읽기 전용 객체 내부의 프로퍼티 값 주소 조회
	template <typename TValueType = void>
	const TValueType* ContainerPtrToValuePtr(const void* Container) const
	{
		return reinterpret_cast<const TValueType*>(static_cast<const uint8*>(Container) + Offset);
	}

	// 생성된 목적지 값에 프로퍼티 복사
	void CopyCompleteValue(void* Destination, const void* Source) const
	{
		CopyFunction(Destination, Source);
	}

	// 두 프로퍼티 값의 동등 비교
	bool Identical(const void* Left, const void* Right) const
	{
		return IdenticalFunction(Left, Right);
	}

	// 등록된 함수에 프로퍼티 값 직렬화 요청
	void SerializeItem(FStructuredArchive::FSlot Slot, void* ValueAddress) const
	{
		SerializeFunction(Slot, ValueAddress);
	}

	// 타입을 보존한 객체 프로퍼티 참조 조회
	UObject* GetObjectPropertyValue(const void* ValueAddress) const
	{
		return GetObjectFunction(ValueAddress);
	}

	// 실제 선언 타입에 맞춘 객체 프로퍼티 참조 교체
	void SetObjectPropertyValue(void* ValueAddress, UObject* Object) const
	{
		SetObjectFunction(ValueAddress, Object);
	}

	// 실제 C++ 타입의 값 연산 연결
	template <typename T>
	void InitializeValueOperations()
	{
		CopyFunction = [](void* Destination, const void* Source)
		{
			*static_cast<T*>(Destination) = *static_cast<const T*>(Source);
		};

		IdenticalFunction = [](const void* Left, const void* Right)
		{
			return *static_cast<const T*>(Left) == *static_cast<const T*>(Right);
		};

		//객체 포인터 타입일때만 추가
		if constexpr (std::is_pointer_v<T> 
			&& std::is_base_of_v<UObject, std::remove_pointer_t<T>>)
		{
			GetObjectFunction = [](const void* ValueAddress) -> UObject*
			{
				return *static_cast<T const*>(ValueAddress);
			};

			SetObjectFunction = [](void* ValueAddress, UObject* Object)
			{
				*static_cast<T*>(ValueAddress) = static_cast<T>(Object);
			};
		}
	}
};