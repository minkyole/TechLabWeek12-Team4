#include "EnginePCH.h"
#include "FakeSpotLightActor.h"

#include "Asset/AssetManager.h"
#include "Components/BillboardComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Core/ResourcePaths.h"

namespace
{
    constexpr const char* FakeSpotLightTexture = "Textures/Decal/FakeSpotLight.png";

    const FVector DefaultDecalSize(3.0f, 2.0f, 2.0f);
}

AFakeSpotLightActor::AFakeSpotLightActor()
{
    UDecalComponent* Decal = GetDecalComponent();
    Decal->SetAdditive(true);
    Decal->SetDecalSize(DefaultDecalSize);
    Decal->SetDecalColor(FLinearColor(0.6f, 0.52f, 0.4f, 1.0f));
    Decal->SetDecalTexture(UAssetManager::GetAssetByPath<UTexture2D>(FakeSpotLightTexture));
    Decal->SetFadeScreenSize(0.0f);
    Decal->SetRelativeRotation(FRotator(90.0f, 0.0f, 0.0f));

    // Decal은 로컬 +X(아래)로 투영하므로 빛이 나오는 박스 윗면은 로컬 -X 끝이다. 아이콘과 화살표를 그 자리에 둔다.
    const FVector TopCenter(-DefaultDecalSize.X, 0.0f, 0.0f);
    UBillboardComponent* Billboard = GetBillboardComponent();
    Billboard->SetRelativeLocation(TopCenter);
    GetArrowComponent()->SetRelativeLocation(TopCenter);

    // 에디터에서 SpotLight와 같은 아이콘으로 보이게 한다. 아이콘 머티리얼은 액터마다 만든 인스턴스라 바꿔도 다른 Decal에 영향이 없다.
    if (UMaterial* IconMaterial = Billboard->GetMaterial(0))
    {
        if (UTexture2D* Icon = UAssetManager::Get().LoadTexture(ResourcePaths::SpotLightIcon))
        {
            IconMaterial->Textures[0] = Icon;
        }
    }
}
