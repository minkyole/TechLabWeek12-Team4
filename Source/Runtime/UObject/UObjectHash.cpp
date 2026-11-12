#include "EnginePCH.h"
#include "UObjectHash.h"
#include "Core/EngineLog.h"
#include "UObject/Object.h"
#include "UObject/Class.h"

//클래스에 해당하는 UObject들의 리스트(O(1)에 바로 가져옴)
static TMap<const UClass*, TSet<UObject*>>& GetClassToObjects() { static TMap<const UClass*, TSet<UObject*>> M; return M; }

//bIsIncludeDerivedClass가 켜졌을 때 자식 클래스 목록이 필요
static TMap<const UClass*, TSet<UClass*>>& GetClassToChildren() { static TMap<const UClass*, TSet<UClass*>> M; return M; }

static TMap<FName, UClass*, FNameHash>& GetClassMap()
{
	static TMap<FName, UClass*, FNameHash> Map;
	return Map;
}

void RegisterClass(UClass* Class)
{
	if (!Class || Class->GetFName().IsNone())
	{
		HTR_LOG(Error, "RegisterClass: a class with a valid name is required.");
		return;
	}
	TMap<FName, UClass*, FNameHash>& ClassMap = GetClassMap();
	const FName Name = Class->GetFName();
	if (UClass** Existing = ClassMap.Find(Name))
	{
		if (*Existing != Class)
		{
			HTR_LOG(Error, "RegisterClass: '{}' is already registered by another class.", Name.ToString());
		}
		return;
	}

	// 이름 검색과 상속 목록의 부모 식별자 일치 보장
	if (Class->Super)
	{
		UClass** RegisteredSuper = ClassMap.Find(Class->Super->GetFName());
		if (!RegisteredSuper || *RegisteredSuper != Class->Super)
		{
			HTR_LOG(Error, "RegisterClass: '{}' requires its superclass '{}' to be registered first.",
			        Name.ToString(), Class->Super->GetName());
			return;
		}
	}

	// 검증을 통과한 클래스의 이름 맵과 상속 목록 등록
	ClassMap.Add(Name, Class);
	if (Class->Super)
	{
		GetClassToChildren()[Class->Super].Add(Class);
	}
}

UClass* FindClass(const FName& Name)
{
	UClass** Found = GetClassMap().Find(Name);
	return Found ? *Found : nullptr;
}

UClass* FindClass(const FString& Name)
{
	return FindClass(FName(Name));
}

UClass* FindClass(const char* Name)
{
	if (!Name)
	{
		return nullptr;
	}
	return FindClass(FName(Name));
}

UObject* FindObjectWithOuter(const UObject* outer, const UClass* Class, FName Name)
{
	for (UObject* Obj : GUObjectArray)
	{
		if (Obj && Obj->GetOuter() == outer && Obj->GetFName() == Name && Obj->GetClass() == Class)
			return Obj;
	}
	return nullptr;
}

void GetObjectsOfClass(const UClass* ClassToLookFor, TArray<UObject*>& Results, bool bIsIncludeDerivedClass)
{
	GetObjectsOfClass(ClassToLookFor, Results, bIsIncludeDerivedClass, false);
}

void GetObjectsOfClass(const UClass* ClassToLookFor, TArray<UObject*>& Results, bool bIsIncludeDerivedClass, bool bIncludeTemplates)
{
	if (!ClassToLookFor) return;

	Results.Reserve(Results.Num() + GUObjectArray.Num());
	// Class에 해당하는 Objects 리스트를 가져온다.
	if (const TSet<UObject*>* Objects = GetClassToObjects().Find(ClassToLookFor))
	{
		for (UObject* Object : *Objects)
		{
			if (bIncludeTemplates || !Object->IsTemplate()){ Results.Add(Object); }
		}
	}

	if (!bIsIncludeDerivedClass) return;

	// 자식 클래스인 목록을 가져온다
	if (const TSet<UClass*>* Children = GetClassToChildren().Find(ClassToLookFor))
	{
		for (UClass* Child : *Children)
		{
			GetObjectsOfClass(Child, Results, true, bIncludeTemplates);
		}
	}	
}

void HashObject(UObject* Obj, const UClass* Class)
{
	GetClassToObjects()[Class].Add(Obj);
}

void UnhashObject(UObject* Obj, const UClass* Class)
{
	if (TSet<UObject*>* Objects = GetClassToObjects().Find(Class))
		Objects->Remove(Obj);
}

void GetDerivedClasses(const UClass* ClassToLookFor, TArray<UClass*>& Results, bool bRecursive)
{
	if(const TSet<UClass*>* Children = GetClassToChildren().Find(ClassToLookFor))
		for (UClass* Child : *Children)
		{
			Results.Add(Child);
			if (bRecursive) GetDerivedClasses(Child, Results, true);
		}
}
