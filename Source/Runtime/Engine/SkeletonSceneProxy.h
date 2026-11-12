#pragma once

#include "EnginePCH.h"
#include "Engine/PrimitiveSceneProxy.h"

class USkeletalMesh;
class USkeletalMeshComponent;

class FSkeletalMeshSceneProxy : public FPrimitiveSceneProxy
{
public:
	FSkeletalMeshSceneProxy(USkeletalMeshComponent* InComponent);
	~FSkeletalMeshSceneProxy() override;
	void UpdateRenderState() override;
	void OnSkeletalMeshDestroyed();

private:
	USkeletalMesh* SkeletalMesh = nullptr;
};
