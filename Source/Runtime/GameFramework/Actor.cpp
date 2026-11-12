#include "EnginePCH.h"
#include "Actor.h"
#include "Engine/World.h"
#include "Engine/Level.h"
#include "Components/SceneComponent.h"
#include "Components/ParticleSubUVComponent.h"
#include "Components/ExponentialHeightFogComponent.h"
#include "Core/EngineLog.h"
#include "UObject/Object.h"

enum class EAttachmentRule;


AActor::AActor()
{
	PrimaryActorTick.Target = this;
}

AActor::~AActor()
{
	TArray<UActorComponent*> ToDelete = Components;
	Components.Reset();
	RootComponent = nullptr;

	for (UActorComponent* Component : ToDelete)
	{
		delete Component;
	}
}

void AActor::BeginPlay()
{
	//if (UPrimitiveComponent* Primitive = Cast<UPrimitiveComponent>(RootComponent))
	//{
	//	World->AddPrimitive(Cast<UPrimitiveComponent>(RootComponent));
	//}

	for (UActorComponent* Component : Components)
	{
		Component->InitializeComponent();
	}

	for (UActorComponent* Component : Components)
	{
		Component->BeginPlay();
	}

	RegisterAllActorTickFunctions(true);
	bActorHasBegunPlay = true;
}

void AActor::EndPlay()
{
	if (!bActorHasBegunPlay)
		return;

	bActorHasBegunPlay = false;
	RegisterAllActorTickFunctions(false);

	for (UActorComponent* Component : Components)
	{
		if (Component)
			Component->EndPlay();
	}
}

void AActor::RegisterAllActorTickFunctions(bool bRegister)
{
	if (bRegister && !World)
		return;

	// bCanEverTick이 꺼진 함수는 등록하지 않으므로 정적 메시 액터는 매 프레임 순회 대상에서 빠진다.
	auto Apply = [&](FTickFunction& Function)
		{
			if (bRegister)
				Function.RegisterTickFunction(World->GetTickTaskManager());
			else
				Function.UnRegisterTickFunction();
		};

	Apply(PrimaryActorTick);
	for (UActorComponent* Component : Components)
	{
		if (Component)
			Apply(Component->PrimaryComponentTick);
	}
}

void AActor::Serialize(FStructuredArchive::FRecord Record)
{
	Super::Serialize(Record);   // "Properties"

	FArchive& Ar = Record.GetUnderlyingArchive();

	if (Ar.HasAnyPortFlags(EPropertyPortFlags::PPF_Duplicate))
	{
		int32 Num = Components.Num();
		FStructuredArchive::FArray ActorArray = Record.EnterArray("Components", Num);
		if (Ar.IsLoading())
			Components.SetNum(Num);
		for (int32 i = 0; i < Num; ++i)
		{
			UObject* Obj = Components[i];
			ActorArray.EnterElement() << Obj;
			Components[i] = static_cast<UActorComponent*>(Obj);
		}

		UObject* WorldObj = World;
		UObject* LevelObj = Level;
		UObject* RootObj = RootComponent;
		Record << SA_VALUE("OwningWorld", WorldObj);
		Record << SA_VALUE("OwningLevel", LevelObj);
		Record << SA_VALUE("RootComponent", RootObj);
		World = static_cast<UWorld*>(WorldObj);
		Level = static_cast<ULevel*>(LevelObj);
		RootComponent = static_cast<USceneComponent*>(RootObj);
		return;
	}

	TArray<UActorComponent*> SavedComponents;
	if (Ar.IsSaving())
	{
		for (UActorComponent* Component : Components)
		{
			if (Component)
			{
				SavedComponents.Add(Component);
			}

		}
	}

	struct FPendingAttachment
	{
		USceneComponent* Child;
		FString ParentName;
	};
	TArray<FPendingAttachment> PendingAttachments;
	TArray<UActorComponent*> LoadedComponents;
	USceneComponent* PreviousRoot = RootComponent;

	int32 Num = SavedComponents.Num();
	FStructuredArchive::FArray ComponentArray = Record.EnterArray("Components", Num);
	for (int32 i = 0; i < Num; ++i)
	{
		FStructuredArchive::FRecord ComponentRecord = ComponentArray.EnterElement().EnterRecord();
		FString ParentName;

		if (Ar.IsSaving())
		{
			UActorComponent* Component = SavedComponents[i];
			FString Name = Component->GetName();
			FString ClassName = Component->GetClass()->GetName();
			if (USceneComponent* SceneComponent = Cast<USceneComponent>(Component))
			{
				USceneComponent* Parent = SceneComponent->GetAttachParent();
				ParentName = Parent && Parent->GetOwner() == this ? Parent->GetName() : "";
			}
			ComponentRecord << SA_VALUE("Name", Name) << SA_VALUE("Class", ClassName) << SA_VALUE("Parent", ParentName);
			Component->Serialize(ComponentRecord);
			continue;
		}

		FString Name, ClassName;
		ComponentRecord << SA_VALUE("Name", Name) << SA_VALUE("Class", ClassName) << SA_VALUE("Parent", ParentName);

		UActorComponent* Component = FindComponentByName(FName(Name));
		if (!Component)
		{
			UClass* ComponentClass = FindClass(ClassName);

			if (!ComponentClass || !ComponentClass->IsChildOf(UActorComponent::StaticClass()))
			{
				HTR_LOG(Warning, "Load: unknown component class '{}'", ClassName);
				continue;
			}

			Component = AddComponent(ComponentClass, FName(Name));
		}

		if (!Component || Component->GetClass()->GetFName() != FName(ClassName))
		{
			HTR_LOG(Warning, "Load: component {} ({}) failed", Name, ClassName);
			continue;
		}

		Component->Serialize(ComponentRecord);
		LoadedComponents.Add(Component);
		if (USceneComponent* SceneComponent = Cast<USceneComponent>(Component))
		{
			if (!ParentName.empty())
			{
				PendingAttachments.Add({ SceneComponent, ParentName });
			}
		}
	}

	FString RootName = RootComponent ? RootComponent->GetName() : FString();
	if (TOptional<FStructuredArchive::FSlot> RootSlot = Record.TryEnterField("RootComponent", Ar.IsSaving() && RootComponent))
	{
		RootSlot.GetValue() << RootName;
		if (Ar.IsLoading())
		{
			USceneComponent* SavedRoot = Cast<USceneComponent>(FindComponentByName(FName(RootName)));
			if (SavedRoot && SavedRoot != RootComponent)
			{
				SavedRoot->DetachFromComponent(EAttachmentRule::KeepRelative);
				SetRootComponent(SavedRoot);
			}
		}
	}

	for (const FPendingAttachment& Attachment : PendingAttachments)
	{
		USceneComponent* ParentComponent = Cast<USceneComponent>(FindComponentByName(FName(Attachment.ParentName)));
		if (ParentComponent)
		{
			Attachment.Child->AttachToComponent(ParentComponent, EAttachmentRule::KeepRelative);
		}
	}

	if (Ar.IsLoading() && PreviousRoot != RootComponent
		&& IsDefaultSceneRoot(PreviousRoot) && LoadedComponents.Find(PreviousRoot) == -1)
	{
		DestroyComponent(PreviousRoot);
	}
}

bool AActor::SetRootComponent(USceneComponent* NewRootComponent)
{
	if (NewRootComponent && NewRootComponent->GetOwner() != this)
		return false;

	if (RootComponent != NewRootComponent)
	{
		RootComponent = NewRootComponent;
		if (RootComponent)
			RootComponent->MarkTransformDirty();
	}
	return true;
}

void AActor::ReplaceRootComponent(USceneComponent* NewRootComponent)
{
	USceneComponent* OldRoot = RootComponent;
	if (!NewRootComponent || NewRootComponent == OldRoot || NewRootComponent->GetOwner() != this)
		return;

	USceneComponent* ExternalParent = OldRoot ? OldRoot->GetAttachParent() : nullptr;
	if (OldRoot)
		OldRoot->DetachFromComponent(EAttachmentRule::KeepWorld);

	NewRootComponent->DetachFromComponent(EAttachmentRule::KeepWorld);
	SetRootComponent(NewRootComponent);

	if (OldRoot)
		OldRoot->AttachToComponent(NewRootComponent, EAttachmentRule::KeepWorld);
	if (ExternalParent)
		NewRootComponent->AttachToComponent(ExternalParent, EAttachmentRule::KeepWorld);
}

USceneComponent* AActor::CreateDefaultSceneRoot()
{
	return Cast<USceneComponent>(AddComponent(USceneComponent::StaticClass(), FName("DefaultSceneRoot")));
}

bool AActor::IsDefaultSceneRoot(const UActorComponent* Component) const
{
	return Component
		&& Component->GetOwner() == this
		&& Component->GetClass() == USceneComponent::StaticClass()
		&& Component->GetFName() == FName("DefaultSceneRoot");
}

void AActor::AddOwnedComponent(UActorComponent* Component)
{
	if (!Component || Components.Find(Component) != -1)
		return;

	Component->SetOwner(this);
	Components.Add(Component);
}

bool AActor::AttachToComponent(USceneComponent* Parent, EAttachmentRule Rule)
{
	return RootComponent && RootComponent->AttachToComponent(Parent, Rule);
}

bool AActor::AttachToActor(AActor* ParentActor, EAttachmentRule Rule)
{
	return ParentActor && ParentActor != this && AttachToComponent(ParentActor->GetRootComponent(), Rule);
}

void AActor::DetachFromActor(EAttachmentRule Rule)
{
	if (RootComponent && GetAttachParentActor())
		RootComponent->DetachFromComponent(Rule);
}

AActor* AActor::GetAttachParentActor() const
{
	USceneComponent* Parent = RootComponent ? RootComponent->GetAttachParent() : nullptr;
	return Parent ? Parent->GetOwner() : nullptr;
}

void AActor::GetAttachedActors(TArray<AActor*>& OutActors) const
{
	for (UActorComponent* Component : Components)
	{
		USceneComponent* SceneComponent = Cast<USceneComponent>(Component);
		if (!SceneComponent)
			continue;

		for (USceneComponent* Child : SceneComponent->GetAttachChildren())
		{
			AActor* ChildOwner = Child ? Child->GetOwner() : nullptr;
			if (ChildOwner && ChildOwner != this && OutActors.Find(ChildOwner) == -1)
				OutActors.Add(ChildOwner);
		}
	}
}


void AActor::RemoveOwnedComponent(UActorComponent* Component)
{
	for (uint32 i = 0; i < Components.Num(); ++i)
	{
		if (Components[i] == Component)
		{
			Components.RemoveAt(i, 1);
			break;
		}
	}

	if (RootComponent == Component)
	{
		RootComponent = nullptr;
	}
}

FVector AActor::GetActorLocation() const
{
	if (RootComponent)
	{
		return RootComponent->GetWorldLocation();
	}
	return FVector::ZeroVector;
}

FRotator AActor::GetActorRotation() const
{
	if (RootComponent)
	{
		return RootComponent->GetWorldRotation();
	}
	return FRotator();
}

FVector AActor::GetActorScale3D() const
{
	if (RootComponent)
	{
		return RootComponent->GetWorldScale3D();
	}
	return FVector::OneVector;
}

FQuat AActor::GetActorQuat() const
{
	if (RootComponent)
	{
		return RootComponent->GetWorldRotation().Quaternion();
	}
	return FQuat();
}

FTransform AActor::GetActorTransform() const
{
	if (RootComponent)
	{
		return FTransform(
			RootComponent->GetWorldRotation(),
			RootComponent->GetWorldLocation(),
			RootComponent->GetWorldScale3D()
		);

		// return FTransform(RootComponent->GetWorldMatrix());
	}
	return FTransform::Identity;
}

bool AActor::Destroy()
{
	if (!World)
		return false;

	return World->DestroyActor(this);
}

UActorComponent* AActor::AddComponent(UClass* ComponentClass, FName Name)
{
	if (!ComponentClass || !ComponentClass->IsChildOf(UActorComponent::StaticClass()))
	{
		return nullptr;
	}


	if (FName(Name) == FName("None"))
	{
		Name = MakeUniqueObjectName(this, ComponentClass);
	}

	UActorComponent* NewComponent = NewObject<UActorComponent>(this, ComponentClass, Name);
	if (!NewComponent){return nullptr;}

	AddOwnedComponent(NewComponent);

	if (USceneComponent* SceneComponent = Cast<USceneComponent>(NewComponent))
	{
		if (RootComponent)
			SceneComponent->SetupAttachment(RootComponent);
		else
			SetRootComponent(SceneComponent);
	}

	NewComponent->InitializeComponent();
	NewComponent->RegisterComponent();

	// 에디터 월드에도 World는 있으므로, 게임이 시작된 액터에만 BeginPlay를 보낸다 (UE의 HasActorBegunPlay 검사).
	if (World && HasActorBegunPlay())
	{
		NewComponent->BeginPlay();
		RegisterAllActorTickFunctions(true);
	}

	return NewComponent;
}

bool AActor::CanDestroyComponent(const UActorComponent* Component) const
{
	if (!Component || Component->GetOwner() != this)
		return false;

	if (Component != RootComponent || !IsDefaultSceneRoot(Component))
		return true;

	for (USceneComponent* Child : RootComponent->GetAttachChildren())
		if (Child && Child->GetOwner() == this && !Child->IsHiddenInDetails())
			return true;

	return false;
}

bool AActor::DestroyComponent(UActorComponent* Component)
{
	if (!CanDestroyComponent(Component))
	{
		HTR_LOG(Warning, "Component cannot be deleted.");
		return false;
	}

	if (HasActorBegunPlay())
		Component->EndPlay();

	if (USceneComponent* SceneComponent = Cast<USceneComponent>(Component))
	{
		TArray<USceneComponent*> HiddenChildren;
		for (USceneComponent* Child : SceneComponent->GetAttachChildren())
			if (Child && Child->GetOwner() == this && Child->IsHiddenInDetails())
				HiddenChildren.Add(Child);

		for (USceneComponent* Child : HiddenChildren)
			DestroyComponent(Child);

		if (SceneComponent == RootComponent)
		{
			USceneComponent* NewRoot = nullptr;
			for (USceneComponent* Child : SceneComponent->GetAttachChildren())
			{
				if (Child && Child->GetOwner() == this)
				{
					NewRoot = Child;
					break;
				}
			}

			if (!NewRoot)
				NewRoot = CreateDefaultSceneRoot();

			ReplaceRootComponent(NewRoot);
		}

		USceneComponent* Parent = SceneComponent->GetAttachParent();
		TArray<USceneComponent*> Children = SceneComponent->GetAttachChildren();
		for (USceneComponent* Child : Children)
		{
			if (!Child)
				continue;

			if (!Parent || !Child->AttachToComponent(Parent, EAttachmentRule::KeepWorld))
				Child->DetachFromComponent(EAttachmentRule::KeepWorld);
		}
	}

	for (UActorComponent* Other : Components)
		if (Other && Other != Component)
			Other->OnSiblingComponentDestroyed(Component);

	Component->UnregisterComponent();
	RemoveOwnedComponent(Component);
	delete Component;
	return true;
}

void AActor::OnDefaultSubobjectCreated(UObject* Subobject)
{
	AddOwnedComponent(Cast<UActorComponent>(Subobject));
}

UActorComponent* AActor::FindComponentByName(FName Name) const
{
	for (UActorComponent* Component : Components)
		if (Component && Component->GetFName() == Name)
			return Component;
	return nullptr;
}

void AActor::RegisterAllComponents()
{
	if (!World) return;

	for (UActorComponent* Component : Components)
	{
		if (Component && !Component->IsRegistered())
			Component->RegisterComponentWithWorld(World);
	}
}

void AActor::UnregisterAllComponents()
{
	for (UActorComponent* Component : Components)
	{
		if (Component && Component->IsRegistered())
			Component->UnregisterComponent();
	}
}

bool AActor::TryGetActorBounds(FBox& OutBounds) const
{
	bool bHasBounds = false;

	for (UActorComponent* Component : Components)
	{
		UPrimitiveComponent* Primitive = Cast<UPrimitiveComponent>(Component);
		if (!Primitive)
			continue;

		FBox PrimitiveBounds = Primitive->CalcBounds();
		if (bHasBounds)
		{
			OutBounds.Expand(PrimitiveBounds);
		}
		else
		{
			OutBounds = PrimitiveBounds;
			bHasBounds = true;
		}
	}

	return bHasBounds;
}