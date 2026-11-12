#pragma once

#include "Core/Types.h"
#include "Core/EngineStatics.h"
#include "Core/NameTypes.h"
#include "UObject/ObjectMacros.h"
#include "UObject/UObjectHash.h"
#include "UObject/UObjectGlobals.h"
#include "Serialization/Archive.h"
#include "Serialization/StructuredArchive.h"
#include "Serialization/StructuredArchiveSlots.h"
#include "UObject/ReflectionMacros.h"


class UClass;
struct FProperty;
// Property Reflection

#define INVALID_OBJECT ((UObject*)-1)

#define REFLECT_START(ClassName) \
public: \
	inline static void RegisterProperties(UClass* InClass) \
	{

#define PROPERTY(PropertyName) \
    InClass->AddProperty<decltype(ThisClass::PropertyName)>(#PropertyName, offsetof(ThisClass, PropertyName));

#define PROPERTY_TYPE(PropertyName, PropertyType) \
    InClass->AddProperty<decltype(ThisClass::PropertyName)>(#PropertyName, offsetof(ThisClass, PropertyName), EPropertyType::##PropertyType);

// Flags: EPropertyFlags 조합 (예: CPF_VisibleAnywhere, CPF_EditAnywhere | CPF_Transient)
#define PROPERTY_FLAGS(PropertyName, Flags) \
    InClass->AddProperty<decltype(ThisClass::PropertyName)>(#PropertyName, offsetof(ThisClass, PropertyName), Flags);

#define PROPERTY_TYPE_FLAGS(PropertyName, PropertyType, Flags) \
    InClass->AddProperty<decltype(ThisClass::PropertyName)>(#PropertyName, offsetof(ThisClass, PropertyName), EPropertyType::##PropertyType, Flags);

#define REFLECT_END()\
	};\
private:

// 추상 클래스는 생성자를 등록하지 않는다.
// 템플릿이어야 버려진 if constexpr 분기의 new T()가 컴파일되지 않고,
// 멤버여야 private 생성자(싱글턴 등)에 접근할 수 있다.
// C++ 추상 타입의 생성자 제외와 선택적인 클래스 지정자 연결
// TODO: GENERATED_BODY 전체 전환 완료 후 레거시 DECLARE_CLASS 제거
#define DECLARE_CLASS_BODY(ClassName, SuperClassName, ...)                                   \
public:                                                                                      \
	using Super = SuperClassName;                                                            \
	using ThisClass = ClassName;                                                             \
	template <typename T = ClassName>                                                        \
	static UObject* InternalConstructInstance(void* Memory)                                  \
	{                                                                                        \
		if constexpr (std::is_abstract_v<T>)                                                 \
		{                                                                                    \
			return nullptr;                                                                  \
		}                                                                                    \
		else                                                                                 \
		{                                                                                    \
			return new (Memory) T();                                                         \
		}                                                                                    \
	}                                                                                        \
	static UClass* GetPrivateStaticClass()                                                   \
	{                                                                                        \
		constexpr EClassSpecifiers Specifiers{ __VA_ARGS__ };                                \
		static_assert(!(HasFlag(Specifiers, EClassSpecifiers::Placeable) &&                  \
		                HasFlag(Specifiers, EClassSpecifiers::NotPlaceable)),                \
		              "Placeable and NotPlaceable cannot be combined.");                     \
		static UClass c(                                                                     \
		    FName(#ClassName), Super::GetPrivateStaticClass(), sizeof(ClassName),            \
		    std::is_abstract_v<ClassName> ? nullptr : &InternalConstructInstance<ClassName>, \
		    &ClassName::RegisterProperties != &Super::RegisterProperties                     \
		        ? &ClassName::RegisterProperties                                             \
		        : nullptr,                                                                   \
		    Specifiers | (std::is_abstract_v<ClassName>                                      \
		                      ? EClassSpecifiers::Abstract                                   \
		                      : EClassSpecifiers::None));                                    \
		return &c;                                                                           \
	}                                                                                        \
	static UClass* StaticClass()                                                             \
	{                                                                                        \
		UClass* c = GetPrivateStaticClass();                                                 \
		c->EnsurePropertiesRegistered();                                                     \
		return c;                                                                            \
	}                                                                                        \
	struct FAutoRegister                                                                     \
	{                                                                                        \
		FAutoRegister() { ThisClass::StaticClass(); }                                        \
	};                                                                                       \
	inline static FAutoRegister AutoRegister;                                                \
                                                                                             \
private:

// TODO: GENERATED_BODY 전체 전환 완료 후 레거시 DECLARE_CLASS 제거
// 수동 선언과 생성 본문이 공유하는 호환 진입점
#define DECLARE_CLASS(ClassName, SuperClassName, ...) \
	DECLARE_CLASS_BODY(ClassName, SuperClassName, __VA_ARGS__)


class UObject
{
	friend UObject* StaticConstructObject_Internal(const struct FStaticConstructObjectParameters& Params);
public:
	UObject();
	UObject(bool bRegister);
	virtual ~UObject();
	
	static UClass* GetPrivateStaticClass();
	static UClass* StaticClass();
	UClass* GetClass() const { return ClassPrivate; }

	template <typename T>
	bool IsA() { return IsA(T::StaticClass()); }
	bool IsA(const UClass* Class);

	uint32 GetUUID() const { return ObjectUUID; }
	void SetUUID(uint32 Uid) { ObjectUUID = Uid; }

	const FName& GetFName() const { return Name; }         // FName 비교용
	FString GetName() const { return Name.ToString(); }    // Name 출력용
	
	FString GetObjectLabel() const { return ObjectLabel.empty() ? GetName() : ObjectLabel; }
	void SetObjectLabel(const FString& InLabel) { ObjectLabel = InLabel; }

	UObject* GetOuter() const { return Outer; }
	void SetOuter(UObject* InOuter) { Outer = InOuter; }

	void SetName(const FName& InName) { Name = FName(InName); }

	inline static void RegisterProperties(UClass* InClass) {};

	inline void SetFlags(EObjectFlags NewFlags) { Flags |= NewFlags; }
	inline void ClearFlags(EObjectFlags FlagsToClear) { Flags &= ~FlagsToClear; }
	inline bool HasAnyFlags(EObjectFlags FlagsToCheck) const { return HasFlag(Flags, FlagsToCheck); }
	inline bool HasAllFlags(EObjectFlags FlagsToCheck) const { return (Flags & FlagsToCheck) == FlagsToCheck; }
	inline EObjectFlags GetFlags() const { return Flags; }
	EObjectFlags GetMaskedFlags(EObjectFlags Mask = EObjectFlags::RF_AllFlags) const{return EObjectFlags(GetFlags() & Mask);}

	virtual void Serialize(FArchive& Ar);
	virtual void Serialize(FStructuredArchive::FRecord Record);
	
	bool ApplyPropertyValue(const FName& PropertyName, const void* NewValue);
	bool ApplyObjectPropertyValue(const FName& PropertyName, UObject* NewValue);
	// 지정 프로퍼티 또는 전체 복원값의 보정
	// ChangedProperty가 nullptr이면 전체 값 보정 대상
	virtual void NormalizeProperties(const FProperty* ChangedProperty = nullptr) {}

	// 보정된 최종값에 따른 객체·Scene 상태의 갱신
	// ChangedProperty가 nullptr이면 전체 상태 갱신 대상
	virtual void PostApplyProperties(const FProperty* ChangedProperty = nullptr) {}

	// 기존 편집 통지와 공통 보정·후처리의 연결
	virtual void PostEditChangeProperty(const FProperty& Property);

	// 디테일 창에 이 프로퍼티를 보여 줄지 정한다. 다른 값에 따라 숨길 때 재정의한다.
	virtual bool IsPropertyVisibleInDetails(const FProperty& Property) const { return true; }

	void* operator new(uint64 Size)
	{
		void* Ptr = malloc(Size);
		if (!Ptr)
			throw std::bad_alloc();

		FEngineStatics::TotalAllocationBytes += static_cast<uint64>(Size);
		FEngineStatics::TotalAllocationCount += 1;
		return Ptr;
	}

	void operator delete(void* Ptr, uint64 Size)
	{
		FEngineStatics::TotalAllocationBytes -= static_cast<uint64>(Size);
		FEngineStatics::TotalAllocationCount -= 1;
		free(Ptr);
	}

	void* operator new(uint64, void* Ptr) { return Ptr; }
	void operator delete(void*, void*) {}

	template <typename T>
	T* CreateDefaultSubobject(FName SubobjectName)
	{
		T* Subobject = NewObject<T>(this, SubobjectName, EObjectFlags::RF_DefaultSubObject);
		OnDefaultSubobjectCreated(Subobject);
		return Subobject;
	}
	bool IsTemplate() const;
	virtual void PostDuplicate(bool bDuplicateForPIE) {}
	virtual void PostInitProperties();
	virtual void PostLoad();

protected:

	virtual void OnDefaultSubobjectCreated(UObject* Subobject) {}

private:
	void SerializeScriptProperties(FStructuredArchive::FSlot Slot);

	FName Name = FName("None");
	FString ObjectLabel;
	uint32 ObjectUUID;
	uint32 InternalIndex;

	UObject* Outer = nullptr;
	UClass* ClassPrivate = nullptr;

	EObjectFlags Flags = EObjectFlags::RF_NoFlags;

	bool bIsRegistered = true;
};

extern TArray<UObject*> GUObjectArray;

inline bool IsValid(const UObject* Test)
{
	return Test != nullptr;
}