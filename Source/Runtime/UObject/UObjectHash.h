#pragma once
#include "Core/NameTypes.h"

class UObject;
class UClass;

void RegisterClass(UClass* Class);

UClass* FindClass(const FName& Name);
UClass* FindClass(const FString& Name);
UClass* FindClass(const char* Name);

UObject* FindObjectWithOuter(const UObject* outer, const UClass* Class, FName Name);

void GetObjectsOfClass(const UClass* ClassToLookFor, TArray<UObject*>& Results, bool bIsIncludeDerivedClass = true);

void GetObjectsOfClass(const UClass* ClassToLookFor, TArray<UObject*>& Results,
                       bool bIsIncludeDerivedClass, bool bIncludeTemplates);

void HashObject(UObject* Obj, const UClass* Class); 
void UnhashObject(UObject* Obj, const UClass* Class);

void GetDerivedClasses(const UClass* ClassToLookFor, TArray<UClass*>& Results, bool bRecursive = true);