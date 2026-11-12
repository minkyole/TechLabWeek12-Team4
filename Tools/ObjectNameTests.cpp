#include "EnginePCH.h"
#include "Camera/CameraActor.h"
#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Level.h"
#include "Engine/World.h"
#include "UObject/UObjectGlobals.h"
#include <cstdio>
#include <cstdlib>

static void Check(bool Passed, const char* Message) {
	if (!Passed) {
		std::printf("FAIL %s\n", Message);
		std::exit(1);
	}
	std::printf("PASS %s\n", Message);
}

static int32 NextNameIndex(const FString& Name) {
	return std::stoi(Name.substr(Name.find_last_of('_') + 1)) + 1;
}

static void TestExistingNames() {
	UObject* Outer{ NewObject<UObject>() };
	const FString Base{ "SavedObject" };
	const FString First{ MakeUniqueObjectName(Outer, UObject::StaticClass(), FName{ Base }).ToString() };
	const int32 NextIndex{ NextNameIndex(First) };
	TArray<UObject*> Existing{};
	for (int32 Index{}; Index < 3; ++Index) {
		Existing.Add(NewObject<UObject>(Outer, FName{ Base + "_" + std::to_string(NextIndex + Index) }));
	}
	const FName Generated{ MakeUniqueObjectName(Outer, UObject::StaticClass(), FName{ Base }) };
	Check(Generated == FName{ Base + "_" + std::to_string(NextIndex + 3) }, "generated names skip consecutive saved names");
	for (UObject* Object : Existing) {
		delete Object;
	}
	delete Outer;
}

static void TestDifferentClassesAndOuters() {
	UObject* Outer{ NewObject<UObject>() };
	UObject* OtherOuter{ NewObject<UObject>() };
	const FString Base{ "SharedName" };
	const FString First{ MakeUniqueObjectName(Outer, UObject::StaticClass(), FName{ Base }).ToString() };
	const int32 NextIndex{ NextNameIndex(First) };
	UBoxComponent* DifferentClass{ NewObject<UBoxComponent>(Outer, FName{ Base + "_" + std::to_string(NextIndex) }) };
	Check(MakeUniqueObjectName(Outer, UObject::StaticClass(), FName{ Base }) != DifferentClass->GetFName(), "objects of different classes share the same outer name namespace");
	const FString Following{ MakeUniqueObjectName(Outer, UObject::StaticClass(), FName{ Base }).ToString() };
	const FName OtherName{ Base + "_" + std::to_string(NextNameIndex(Following)) };
	UObject* OtherObject{ NewObject<UObject>(OtherOuter, OtherName) };
	Check(MakeUniqueObjectName(Outer, UObject::StaticClass(), FName{ Base }) == OtherName, "objects in another outer do not reserve local names");
	Check(MakeUniqueObjectName(Outer, nullptr).IsNone(), "null classes preserve the empty name result");
	delete DifferentClass;
	delete OtherObject;
	delete Outer;
	delete OtherOuter;
}

static void TestActorPreservation() {
	UWorld* World{ UWorld::CreateWorld(EWorldType::Editor, false) };
	const FString First{ MakeUniqueObjectName(World->GetPersistentLevel(), AActor::StaticClass()).ToString() };
	const FName SavedName{ "AActor_" + std::to_string(NextNameIndex(First)) };
	AActor* SavedActor{ World->SpawnActor<AActor>(SavedName) };
	UStaticMeshComponent* SavedMesh{ Cast<UStaticMeshComponent>(SavedActor->AddComponent(UStaticMeshComponent::StaticClass(), NAME_None)) };
	const uint32 SavedId{ SavedActor->GetUUID() };
	AActor* GeneratedActor{ World->SpawnActor<AActor>() };
	Check(GeneratedActor != SavedActor, "unnamed actors do not replace saved actors");
	Check(SavedActor->GetUUID() == SavedId && SavedActor->GetFName() == SavedName, "saved actor identity remains intact");
	Check(SavedMesh->GetOwner() == SavedActor && SavedMesh->IsRegistered(), "saved components stay registered to the original actor");
	World->Tick(0.016f);
	Check(SavedMesh->GetSceneProxy() != nullptr, "render state updates remain valid after spawning an unnamed actor");
	World->ClearWorld();
	ACameraActor* Camera{ World->GetMainCamera() };
	World->SetMainCamera(nullptr);
	delete Camera;
	delete World->GetPersistentLevel();
	delete World;
}

int main() {
	TestExistingNames();
	TestDifferentClassesAndOuters();
	TestActorPreservation();
	return 0;
}
