#include "EnginePCH.h"
#include "PlayerActor.h"

APlayerActor::APlayerActor()
{
	StaticMeshComponent = CreateDefaultSubobject<UStaticMeshComponent>("StaticMeshComponent");
	ScriptComponent = CreateDefaultSubobject<UScriptComponent>("ScriptComponent");
	SetRootComponent(StaticMeshComponent);	// Shape를 선택하기 전에는 메시를 임시 루트로 지정
}

void APlayerActor::BeginPlay()
{
	// 컴포넌트의 초기화 및 Lua BeginPlay 전 Shape로 RootComponent 지정
	ShapeComponent = Cast<UShapeComponent>(GetRootComponent());
	if (!ShapeComponent)
	{
		HTR_LOG(Warning, "[Player] {}: 플레이 시작 전에 Box/Sphere/Capsule을 추가하고 루트로 지정하세요. 충돌체 참조가 없습니다.", GetName());
	}

	Super::BeginPlay();
}
