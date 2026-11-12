#pragma once

#include "UObject/Object.h"
#include "Components/PrimitiveComponent.h"

#include "UObject/Class.h"
#include "UObject/UObjectGlobals.h"
#include "Containers/Set.h"
#include "Engine/EngineBaseTypes.h"

class UWorld;
class ULevel;

class AActor : public UObject
{
	DECLARE_CLASS(AActor, UObject)
	REFLECT_START(ClassName)
		REFLECT_END()
public:
	AActor();
	virtual ~AActor();

	virtual void BeginPlay(); // xx World->AddPrimitive 책임이동 필요
	// 액터 자신의 로직. 컴포넌트는 각자의 PrimaryComponentTick으로 따로 실행된다.
	virtual void Tick(float DeltaTime) {}

	// 게임 중이던 액터가 파괴되거나 PIE가 끝날 때 한 번 불린다. 컴포넌트의 EndPlay도 여기서 부른다.
	virtual void EndPlay();
	bool HasActorBegunPlay() const { return bActorHasBegunPlay; }
	bool IsActorBeingDestroyed() const { return bActorIsBeingDestroyed; }
	// FActorTickFunction이 호출하는 진입점
	void TickActor(float DeltaTime) { Tick(DeltaTime); }

	UWorld* GetWorld() const { return World; }
	ULevel* GetLevel() const { return Level; }

	const TArray<UActorComponent*>& GetComponents() const { return Components; }
	USceneComponent* GetRootComponent() const { return RootComponent; }
	bool SetRootComponent(USceneComponent* NewRootComponent);
	void ReplaceRootComponent(USceneComponent* NewRootComponent);
	USceneComponent* CreateDefaultSceneRoot();
	bool IsDefaultSceneRoot(const UActorComponent* Component) const;

	void AddOwnedComponent(UActorComponent* Component);
	void RemoveOwnedComponent(UActorComponent* Component);

	bool AttachToComponent(USceneComponent* Parent, EAttachmentRule Rule);
	bool AttachToActor(AActor* ParentActor, EAttachmentRule Rule);
	void DetachFromActor(EAttachmentRule Rule);
	AActor* GetAttachParentActor() const;
	void GetAttachedActors(TArray<AActor*>& OutActors) const;
	
	FVector GetActorLocation() const;
	FRotator GetActorRotation() const;
	FVector GetActorScale3D() const;
	FQuat GetActorQuat() const;           //xx 타입명변경
	FTransform GetActorTransform() const;
	bool TryGetActorBounds(FBox& OutBounds) const;

	bool Destroy();
	
	//xx삭제 예정
	friend class UWorld;

	UActorComponent* AddComponent(UClass* ComponentClass, FName Name);
	bool CanDestroyComponent(const UActorComponent* Component) const;
	bool DestroyComponent(UActorComponent* Component);

	// bCanEverTick이 켜진 액터·컴포넌트의 Tick 함수만 World의 FTickTaskManager에 등록하거나 해제한다.
	void RegisterAllActorTickFunctions(bool bRegister);

	// UE와 같이 기본값은 bCanEverTick = false. 생성자에서 Target = this
	FActorTickFunction PrimaryActorTick;

	using Super::Serialize;
	virtual void Serialize(FStructuredArchive::FRecord Record) override;
	UActorComponent* FindComponentByName(FName Name) const;

	virtual void RegisterAllComponents();
	// Parameter : 바로 다시 Register 할껀지
	virtual void UnregisterAllComponents();
protected:
	virtual void OnDefaultSubobjectCreated(UObject* Subobject) override;

	//TSet<TObjectPtr<UActorComponent>> OwnedComponents;
	TArray<UActorComponent*> Components;
	USceneComponent* RootComponent = nullptr;

	UWorld* World = nullptr;
	ULevel* Level = nullptr;
private:
	bool bActorHasBegunPlay = false;
	bool bActorIsBeingDestroyed = false;
	

};