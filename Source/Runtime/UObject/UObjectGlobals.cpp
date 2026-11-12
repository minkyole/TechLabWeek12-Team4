#include "EnginePCH.h"
#include "UObjectGlobals.h"

#include "UObjectHash.h"

#include "Class.h"

#include "Core/HAL/UnrealMemory.h"

#include "Serialization/DuplicatedObject.h"
#include "Serialization/LargeMemoryData.h"
#include "Serialization/DuplicateDataWriter.h"
#include "Serialization/DuplicateDataReader.h"


namespace
{
	FObjectDuplicationParameters InitStaticDuplicateObjectParams(UObject const* SourceObject, UObject* DestOuter,
		const FName DestName, EObjectFlags FlagMask, UClass* DestClass)
	{
		FObjectDuplicationParameters Parameters(const_cast<UObject*>(SourceObject), DestOuter);
		if (!DestName.IsNone())
		{
			Parameters.DestName = DestName;
		}
		else if (SourceObject->GetOuter() != DestOuter)
		{
			// try to keep the object name consistent if possible
			if (FindObjectFast<UObject>(DestOuter, SourceObject->GetFName()) == nullptr)
			{
				Parameters.DestName = SourceObject->GetFName();
			}
		}

		Parameters.DestClass = DestClass ? DestClass : SourceObject->GetClass();
		Parameters.FlagMask = FlagMask;

		Parameters.PortFlags = EPropertyPortFlags::PPF_DuplicateForPIE;

		return Parameters;
	}
}

FObjectInitializer* FObjectInitializer::Current = nullptr;

FObjectInitializer::FObjectInitializer(UObject* InObject, const FStaticConstructObjectParameters& InParameters)
    : Previous(Current), Object(InObject), Parameters(InParameters)
{ Current = this; }

FObjectInitializer::~FObjectInitializer()
{
	// 생성 종료와 예외 종료의 부모 컨텍스트 복원
	Current = Previous;

	if (!bCompleted)
	{
		// 소유자 소멸자가 이미 삭제한 객체를 제외한 실패 생성 정리
		for (int32 Index = CreatedSubobjects.Num() - 1; Index >= 0; --Index)
		{
			UObject* Child = CreatedSubobjects[Index];
			if (GUObjectArray.Find(Child) != -1)
			{
				delete Child;
			}
		}
	}
}

const FObjectInitializer* FObjectInitializer::GetForObject(const UObject* InObject)
{
	return Current && Current->Object == InObject ? Current : nullptr;
}

void FObjectInitializer::Complete()
{
	// 실패 복구를 위한 생성 중인 소유자들의 기본 서브오브젝트 기록
	if (Object->HasAnyFlags(EObjectFlags::RF_DefaultSubObject))
	{
		const UObject* Child = Object;
		for (FObjectInitializer* Owner = Previous;
		     Owner && Child->GetOuter() == Owner->Object;
		     Owner = Owner->Previous)
		{
			Owner->CreatedSubobjects.Add(Object);
			Child = Owner->Object;
		}
	}
	bCompleted = true;
}

// 기본값 복사와 객체 참조 재연결 이후의 생성 후처리
bool FObjectInitializer::InitProperties(const UObject* DefaultData) const
{
	if (DefaultData)
	{
		// 자신 복사/클래스 불일치
		if (DefaultData == Object || DefaultData->GetClass() != Object->GetClass())
		{
			HTR_LOG(Error, "InitProperties: incompatible default source for '{}'.", Object->GetName());
			return false;
		}
		// CDO 생성중 여부
		for (const UObject* Owner = DefaultData; Owner; Owner = Owner->GetOuter())
		{
			if (Owner->HasAnyFlags(EObjectFlags::RF_ClassDefaultObject) && Owner->GetClass()->IsCreatingDefaultObject())
			{
				HTR_LOG(Error, "InitProperties: default object '{}' is still being created.", Owner->GetName());
				return false;
			}
		}
	}

	// 프로퍼티 등록 여부와 무관한 목적지 기본 서브오브젝트 수집
	TArray<UObject*> Destinations;
	Destinations.Add(Object);
	for (int32 Index = 0; Index < Destinations.Num(); ++Index)
	{
		for (UObject* Child : GUObjectArray)
		{
			if (Child->GetOuter() == Destinations[Index] && Child->HasAnyFlags(EObjectFlags::RF_DefaultSubObject))
			{
				Destinations.Add(Child);
			}
		}
	}

	if (DefaultData)
	{
		// 값 복사 이전의 전체 원본·인스턴스 대응표 구성
		TMap<const UObject*, UObject*> Instances;
		TArray<const UObject*> Sources;
		Instances.Add(DefaultData, Object);
		Sources.Add(DefaultData);
		for (int32 Index = 0; Index < Sources.Num(); ++Index)
		{
			const UObject* SourceOuter = Sources[Index];
			UObject* DestinationOuter = Instances.FindRef(SourceOuter);
			for (UObject* Source : GUObjectArray)
			{
				if (Source->GetOuter() != SourceOuter 
					|| !Source->HasAnyFlags(EObjectFlags::RF_DefaultSubObject))
				{
					continue;
				}
				UObject* Destination = FindObjectWithOuter(DestinationOuter, Source->GetClass(), Source->GetFName());
				if (!Destination || !Destination->HasAnyFlags(EObjectFlags::RF_DefaultSubObject))
				{
					HTR_LOG(Error, "InitProperties: unmatched default subobject '{}' in '{}'.",
					        Source->GetName(), Object->GetName());
					return false;
				}
				Instances.Add(Source, Destination);
				Sources.Add(Source);
			}
		}

		// 상속 프로퍼티 복사와 실제 멤버 타입을 보존한 참조 치환
		for (const UObject* Source : Sources)
		{
			UObject* Destination = Instances.FindRef(Source);
			for (UClass* Class = Source->GetClass(); Class; Class = Class->Super)
			{
				for (const FProperty& Property : Class->GetProperties())
				{
					const void* SourceValue = Property.ContainerPtrToValuePtr(Source);
					void* DestinationValue = Property.ContainerPtrToValuePtr(Destination);
					if (!Property.GetObjectFunction)
					{
						Property.CopyCompleteValue(DestinationValue, SourceValue);
						continue;
					}

					UObject* Reference = Property.GetObjectPropertyValue(SourceValue);
					if (UObject* Instance = Instances.FindRef(Reference))
					{
						Reference = Instance;
					}
					else
					{
						// 대응되지 않은 원본 소유 객체 참조의 유출 차단
						for (const UObject* Owner = Reference; Owner; Owner = Owner->GetOuter())
						{
							if (Owner == DefaultData)
							{
								HTR_LOG(Error, "InitProperties: unmapped owned reference '{}' in '{}'.",
								        Property.Name.ToString(), Object->GetName());
								return false;
							}
						}
					}
					Property.SetObjectPropertyValue(DestinationValue, Reference);
				}
			}
		}
	}

	// 부모 생성자의 전체 그래프 설정 이후로 미루는 서브오브젝트 후처리
	if (Previous && Object->HasAnyFlags(EObjectFlags::RF_DefaultSubObject) && Object->GetOuter() == Previous->Object)
	{
		return true;
	}

	// 전체 값과 참조가 확정된 이후의 객체별 생성 후처리
	for (UObject* Destination : Destinations)
	{
		Destination->NormalizeProperties(nullptr);
	}
	for (UObject* Destination : Destinations)
	{
		Destination->PostInitProperties();
	}

	return true;
}

UObject* StaticConstructObject_Internal(const FStaticConstructObjectParameters& Params)
{
	const UClass* Class = Params.Class;
	if (!Class || !Class->Constructor || (!Params.bCreateClassDefaultObject && !Class->CanCreateInstance()))
	{
		return nullptr;
	}

	// CDO 역할과 일반 객체 플래그의 독립적인 결정
	FStaticConstructObjectParameters Effective = Params;
	Effective.SetFlags &= ~EObjectFlags::RF_ClassDefaultObject;
	if (Effective.bCreateClassDefaultObject)
	{
		Effective.SetFlags |= EObjectFlags::RF_ClassDefaultObject;
	}

	// 생성자 소유 기본 서브오브젝트의 부모 단위 초기화 판별
	const bool bDeferDefaultInitialization = HasFlag(Effective.SetFlags, EObjectFlags::RF_DefaultSubObject)
		&& FObjectInitializer::GetForObject(Effective.Outer) != nullptr;

	// 일반 객체에 사용할 완성된 CDO 확보
	const UObject* DefaultData = nullptr;
	if (!Effective.bCreateClassDefaultObject && !bDeferDefaultInitialization)
	{
		DefaultData = const_cast<UClass*>(Class)->GetDefaultObject();
		if (!DefaultData || Class->IsCreatingDefaultObject())
		{
			HTR_LOG(Error, "Cannot initialize '{}' from an unfinished class default object.", Class->GetName());
			return nullptr;
		}
	}


	if (Effective.Name.IsNone())
	{
		Effective.Name = MakeUniqueObjectName(Effective.Outer, Class);
	}

	bool bRecycled = false;
	UObject* Object = StaticAllocateObject(Class, Effective.Outer, Effective.Name, Effective.SetFlags, true, &bRecycled);
	if (!Object || bRecycled)
	{
		return Object;
	}

	bool bConstructorCompleted = false;
	try
	{
		FObjectInitializer Initializer(Object, Effective);
		Class->Constructor(Object);
		bConstructorCompleted = true;

		TUniquePtr<UObject> CompletedObject(Object);
		if (!Initializer.InitProperties(DefaultData))
		{
			return nullptr;
		}

		HashObject(Object, Class);
		Initializer.Complete();
		return CompletedObject.release();
	}
	catch (...)
	{
		// 생성자가 완료되지 않은 원시 할당의 반환
		if (!bConstructorCompleted)
		{
			UObject::operator delete(Object, Class->ClassSize);
		}
		throw;
	}
}

FName MakeUniqueObjectName(UObject* Outer, const UClass* Class, FName BaseName) {
	if (!Class) {
		return FName{};
	}

	// 이름을 따로 안 줬으면 Class 이름을 기본 이름으로 사용
	if (BaseName == NAME_None) {
		BaseName = FName{ Class->GetFName() };
	}

	const FString Base{ BaseName.ToString() };
	FName Candidate{};
	do {
		Candidate = FName{ Base + "_" + std::to_string(Class->ClassUnique++) };
	} while (StaticFindObjectFast(nullptr, Outer, Candidate) != nullptr);
	return Candidate;
}

UObject* StaticDuplicateObject(UObject const* SourceObject, UObject* DestOuter, const FName DestName, EObjectFlags FlagMask, UClass* DestClass)
{
	FObjectDuplicationParameters Parameters = InitStaticDuplicateObjectParams(SourceObject, DestOuter, DestName, FlagMask, DestClass);
	return StaticDuplicateObjectEx(Parameters);
}

UObject* StaticDuplicateObjectEx(FObjectDuplicationParameters& Parameters)
{
	UObject* Source = Parameters.SourceObject;
	if (!Source) return nullptr;

	UObject* DupRootObject = Parameters.DuplicationSeed.FindRef(Parameters.SourceObject);
	if (DupRootObject == nullptr)
	{
		UClass* DestClass = Parameters.DestClass ? Parameters.DestClass : Parameters.SourceObject->GetClass();
		FStaticConstructObjectParameters Params(DestClass);
		Params.Outer = Parameters.DestOuter;
		Params.Name = Parameters.DestName;
		Params.SetFlags = Parameters.ApplyFlags | Parameters.SourceObject->GetMaskedFlags(Parameters.FlagMask);
		DupRootObject = StaticConstructObject_Internal(Params);
	}
	if (!DupRootObject) { return nullptr; }
	FDuplicatedObjectAnnotation DuplicatedObjectAnnotation;
	FLargeMemoryData ObjectData;
	TArray<UObject*> SerializedObjects;   // Reader가 같은 순서로 읽도록 기록

	{
		FDuplicateDataWriter Writer(DuplicatedObjectAnnotation, ObjectData,
			Source, DupRootObject, Parameters.FlagMask, Parameters.ApplyFlags, Parameters.PortFlags);

		// Seed는 대응표에만 등록 (직렬화하지 않음). 루트는 Writer 생성자에서 이미 등록됨
		for (const auto& [SeedSource, SeedDup] : Parameters.DuplicationSeed)
			if (SeedSource != Source)
				DuplicatedObjectAnnotation.AddAnnotation(SeedSource, FDuplicatedObject(SeedDup));

		// 직렬화 중에 GetDuplicatedObject가 큐에 객체를 계속 추가하므로 Num()을 매번 확인
		for (int32 i = 0; i < Writer.UnserializedObjects.Num(); ++i)
		{
			UObject* Obj = Writer.UnserializedObjects[i];
			SerializedObjects.Add(Obj);
			Obj->Serialize(Writer);
		}
	}

	// 3. 읽기
	bool bReadSucceeded = false;
	{
		FDuplicateDataReader Reader(DuplicatedObjectAnnotation, ObjectData, Parameters.PortFlags, Parameters.DestOuter);
		for (UObject* Src : SerializedObjects)
		{
			UObject* Dup = DuplicatedObjectAnnotation.GetAnnotation(Src).DuplicatedObject;
			Dup->Serialize(Reader);
		}
		bReadSucceeded = !Reader.IsError();
		if (!bReadSucceeded)
			HTR_LOG(Error, "StaticDuplicateObjectEx: read failed for {}", Source->GetName());
	}
	
	if (bReadSucceeded)
	{
		// 복제 전용 후처리 이전의 전체 목적지 값 보정
		for (UObject* Src : SerializedObjects)
		{
			if (UObject* Dup = DuplicatedObjectAnnotation.GetAnnotation(Src).DuplicatedObject)
			{
				Dup->NormalizeProperties(nullptr);
			}
		}

		// 전체 값 보정 완료 이후의 공통 상태 갱신
		for (UObject* Src : SerializedObjects)
		{
			if (UObject* Dup = DuplicatedObjectAnnotation.GetAnnotation(Src).DuplicatedObject)
			{
				Dup->PostApplyProperties(nullptr);
			}
		}

		const bool bDuplicateForPIE = HasFlag(Parameters.PortFlags, EPropertyPortFlags::PPF_DuplicateForPIE);
		for (UObject* Src : SerializedObjects)
		{
			if (UObject* Dup = DuplicatedObjectAnnotation.GetAnnotation(Src).DuplicatedObject)
				Dup->PostDuplicate(bDuplicateForPIE);
		}
	}

	// 4. 결과
	if (Parameters.CreatedObjects)
	{
		for (UObject* Src : SerializedObjects)
			Parameters.CreatedObjects->Add(Src, DuplicatedObjectAnnotation.GetAnnotation(Src).DuplicatedObject);
	}

	return DupRootObject;
}

UObject* StaticFindObjectFast(UClass* Class, UObject* InOuter, FName InName, EFindObjectFlags Flags, EObjectFlags ExclusiveFlags)
{
	const bool bExactClass = HasFlag(Flags, EFindObjectFlags::ExactClass);

	for (UObject* Obj : GUObjectArray)            
	{
		// Outer와 이름이 같은지: 이 두 개가 같은 객체인가 조건
		if (!Obj || Obj->GetOuter() != InOuter || Obj->GetFName() != InName)
			continue;

		// 제외 플래그: 이 플래그를 가진 객체는 못 찾은 것으로 친다
		if (Obj->HasAnyFlags(ExclusiveFlags))
			continue;

		// 클래스 검사: Class가 nullptr이면 클래스 무관
		if (Class && (bExactClass ? Obj->GetClass() != Class : !Obj->GetClass()->IsChildOf(Class)))
			continue;

		return Obj;                               // 처음 찾은 객체 반환
	}
	return nullptr;
}

UObject* StaticAllocateObject(const UClass* InClass, UObject* InOuter, FName InName, EObjectFlags InFlags, bool bCanRecycleSubobjects, bool* bOutRecycledSubobject)
{
	assert(InClass && InClass->Constructor);

	UObject* Obj = nullptr;
	if (InName == NAME_None)
	{
		InName = MakeUniqueObjectName(InOuter, InClass);
	}
	else
	{
		Obj = StaticFindObjectFast(nullptr, InOuter, InName, EFindObjectFlags::ExactClass);
		if (Obj && (Obj->HasAnyFlags(EObjectFlags::RF_ClassDefaultObject) 
			|| HasFlag(InFlags, EObjectFlags::RF_ClassDefaultObject)))
		{
			HTR_LOG(Error, "Cannot replace class default object '{}'.", InName.ToString());
			return nullptr;

		}
		if (Obj && !Obj->GetClass()->IsChildOf(InClass))
		{
			HTR_LOG(Error, "Cannot replace existing object {} of a different class", InName.ToString());
			return nullptr;
		}
	}
	bool bSubObject = false;
	if (!Obj)
	{
		//GUObjectAllocator.AllocateUObject
		Obj = static_cast<UObject*>(UObject::operator new(InClass->ClassSize));
	}
	else if (bCanRecycleSubobjects && Obj->HasAnyFlags(EObjectFlags::RF_DefaultSubObject))
	{
		bSubObject = true;                    // 재활용: 파괴하지 않음
		Obj->SetFlags(InFlags);
	}
	else
	{
		Obj->~UObject();                      // 교체: 같은 메모리에 다시 생성
	}

	if (!bSubObject)
		FMemory::Memzero(Obj, InClass->ClassSize);

	if (bOutRecycledSubobject)
		*bOutRecycledSubobject = bSubObject;

	return Obj;
}

FStaticConstructObjectParameters::FStaticConstructObjectParameters(const UClass* InClass)
	: Class(InClass)
{
}

FObjectDuplicationParameters::FObjectDuplicationParameters(UObject* InSourceObject, UObject* InDestOuter)
	: SourceObject(InSourceObject)
	, DestOuter(InDestOuter)
	, DestName(NAME_None)
	, FlagMask(EObjectFlags::RF_AllFlags)
	, ApplyFlags(EObjectFlags::RF_NoFlags)
	, PortFlags(EPropertyPortFlags::PPF_None)
{
}
