#include "EnginePCH.h"
#include "DecalActor.h"

#include "Asset/AssetManager.h"
#include "Components/StaticMeshComponent.h"
#include "Core/ResourcePaths.h"
#include "Components/BillboardComponent.h"

UBillboardComponent* ADecalActor::GetBillboardComponent() const
{
    return BillboardComponent; 
}

UDecalComponent* ADecalActor::GetDecalComponent() const
{
    return DecalComponent; 
}

UStaticMeshComponent* ADecalActor::GetArrowComponent() const
{
    return ArrowComponent;
}

ADecalActor::ADecalActor()
{
    DecalComponent = CreateDefaultSubobject<UDecalComponent>("UDecalComponent");
    SetupDecalRoot(DecalComponent, ResourcePaths::DecalIcon);   

    // Icon 옆에 붙어서 Decal의 방향을 알려주는 기즈모 arrow (로컬 x축)
    ArrowComponent = CreateDefaultSubobject<UStaticMeshComponent>("UArrowComponent");
    ArrowComponent->SetEditorIcon(true);
    ArrowComponent->SetupAttachment(DecalComponent);
    ArrowComponent->SetStaticMesh(UAssetManager::GetAssetByPath<UStaticMesh>("Arrow"));
    ArrowComponent->SetMaterial(0, UAssetManager::GetAssetByPath<UMaterial>("EditorArrowMaterial"));
    ArrowComponent->SetRelativeRotation(FRotator(90.0f, 0.0f, 0.0f));
    ArrowComponent->SetRelativeScale3D(FVector(0.3f, 0.3f, 0.5f));
    ArrowComponent->SetHiddenInDetails(true);
}

void ADecalActor::SetupDecalRoot(UDecalComponent* InDecalComponent, const FString& IconPath)
{
    SetRootComponent(InDecalComponent);

    BillboardComponent = CreateDefaultSubobject<UBillboardComponent>("UBillboardComponent");
    BillboardComponent->SetEditorIcon(true);
    BillboardComponent->SetIconSize(2.0f);
    BillboardComponent->SetupAttachment(InDecalComponent);

    UMaterial* Base = UAssetManager::GetAssetByPath<UMaterial>("SubUVMaterial");
    UTexture2D* Icon = UAssetManager::Get().LoadTexture(IconPath);
    if (Base && Icon)
    {
        UMaterial* IconMat = UMaterial::CreateInstance(Base);
        IconMat->Textures[0] = Icon;
        BillboardComponent->SetMaterial(0, IconMat);
    }
}