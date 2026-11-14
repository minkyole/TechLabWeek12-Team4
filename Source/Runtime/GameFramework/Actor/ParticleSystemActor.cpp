#include "EnginePCH.h"
#include "ParticleSystemActor.h"
#include "Components/BillboardComponent.h"
#include "Asset/AssetManager.h"
#include "Core/ResourcePaths.h"

AParticleSystemActor::AParticleSystemActor()
{
    ParticleSystemComponent = CreateDefaultSubobject<UParticleSystemComponent>("ParticleSystemComponent");
	SetupParticleRoot(ParticleSystemComponent, ResourcePaths::ParticleIcon);
}

void AParticleSystemActor::SetupParticleRoot(
    UParticleSystemComponent* InParticleComponent, const FString& IconPath)
{
    SetRootComponent(InParticleComponent);

    BillboardComponent = CreateDefaultSubobject<UBillboardComponent>("BillboardComponent");
    BillboardComponent->SetEditorIcon(true);
    BillboardComponent->SetIconSize(2.0f);
    BillboardComponent->SetupAttachment(InParticleComponent);

    UMaterial* Base = UAssetManager::GetAssetByPath<UMaterial>("SubUVMaterial");
    UTexture2D* Icon = UAssetManager::Get().LoadTexture(IconPath);
    if (Base && Icon)
    {
        UMaterial* IconMat = UMaterial::CreateInstance(Base);
        IconMat->Textures[0] = Icon;
        BillboardComponent->SetMaterial(0, IconMat);
    }
}