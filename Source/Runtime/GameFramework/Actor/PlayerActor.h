#pragma once

#include "EnginePCH.h"
#include "GameFramework/Actor.h"
#include "UObject/Class.h"

#include "Components/ShapeComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/ScriptComponent.h"


class APlayerActor : public AActor
{
	DECLARE_CLASS(APlayerActor, AActor)

public:
	APlayerActor();
	virtual void BeginPlay() override;

	UShapeComponent* GetShape() const { return ShapeComponent; }
	UStaticMeshComponent* GetStaticMesh() const { return StaticMeshComponent; }
	UScriptComponent* GetScript() const { return ScriptComponent; }

private:
	// 실제 충돌 모양은 에디터에서 추가한 루트 Shape로 결정한다.
	UShapeComponent* ShapeComponent = nullptr;
	UStaticMeshComponent* StaticMeshComponent = nullptr;
	UScriptComponent* ScriptComponent = nullptr;
};
