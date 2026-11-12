#include "EnginePCH.h"
#include "Components/DecalComponent.h"
#include "Engine/World.h"
#include "Asset/AssetManager.h"
#include "Render/LineBatcher.h"

UDecalComponent::UDecalComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
}

void UDecalComponent::BeginPlay()
{
	Super::BeginPlay();
	FadeElapsedTime = 0.0f;
	bFadeFinished = false;
}

void UDecalComponent::TickComponent(float DeltaTime)
{
	if (bFadeFinished)
	{
		return;
	}

	float PreviousFadeAlpha = GetFadeAlpha();
	FadeElapsedTime += DeltaTime;

	const float LifeSpan = FadeStartDelay + FadeDuration;
	if (LifeSpan > 0.0f && FadeElapsedTime >= LifeSpan)
	{
		bFadeFinished = true;

		if (bDestroyOwnerAfterFade)
		{
			GetOwner()->Destroy();
		}
		else if (GetOwner()->GetRootComponent() != this)
		{
			GetWorld()->RequestDestroyComponent(this);
		}
		else
		{
			MarkRenderStateDirty();
		}
	}
	else
	{
		if (PreviousFadeAlpha != GetFadeAlpha())
		{
			MarkRenderStateDirty();
		}
	}
}

UTexture2D* UDecalComponent::GetDecalTexture() const
{
	return DecalTexture ? DecalTexture : UAssetManager::GetAssetByPath<UTexture2D>("WhiteTexture");
}

void UDecalComponent::SetDecalTexture(UTexture2D* InDecalTexture)
{
	DecalTexture = InDecalTexture;
	MarkRenderStateDirty();
}

UTexture2D* UDecalComponent::GetDecalNormalTexture() const
{
	return (DecalNormalTexture);
}

void UDecalComponent::SetDecalNormalTexture(UTexture2D* InDecalNormalTexture)
{
	DecalNormalTexture = InDecalNormalTexture;
	MarkRenderStateDirty();
}

void UDecalComponent::SetDecalColor(const FLinearColor& InDecalColor)
{
	DecalColor = InDecalColor;
	MarkRenderStateDirty();
}

void UDecalComponent::SetSortOrder(int32 InSortOrder)
{
	SortOrder = InSortOrder;
	MarkRenderStateDirty();
}

void UDecalComponent::SetDecalSize(const FVector& InDecalSize)
{
	DecalSize = InDecalSize;
	OnTransformDirty();
}

void UDecalComponent::SetAdditive(bool bInAdditive)
{
	bAdditive = bInAdditive;
	MarkRenderStateDirty();
}

void UDecalComponent::SetFadeScreenSize(float InFadeScreenSize)
{
	FadeScreenSize = InFadeScreenSize;
	MarkRenderStateDirty();
}

const FLinearColor& UDecalComponent::GetDecalColor() const
{
	return (DecalColor);
}

int32 UDecalComponent::GetSortOrder() const
{
	return (SortOrder);
}

float	UDecalComponent::GetFadeAlpha() const
{
	float FadeIn = 1.0f;
	float FadeOut = 1.0f;

	if (GetWorld() && GetWorld()->GetWorldType() == EWorldType::Editor)
	{
		return (1.0f);
	}

	if (bFadeFinished)
	{
		return (0.0f);
	}

	if (FadeInDuration > 0.0f)
	{
		FadeIn = std::clamp((FadeElapsedTime - FadeInStartDelay) / FadeInDuration, 0.0f, 1.0f);
	}

	if (FadeDuration > 0.0f)
	{
		FadeOut = std::clamp(1.0f - (FadeElapsedTime - FadeStartDelay) / FadeDuration, 0.0f, 1.0f);
	}

	return (std::min(FadeIn, FadeOut));
}

float	UDecalComponent::GetFadeScreenSize() const
{
	return (FadeScreenSize);
}

bool UDecalComponent::IsAdditive() const
{
	return (bAdditive);
}

FMatrix UDecalComponent::GetTransformIncludingDecalSize() const   // 프록시와 셰이더에서 사용
{
	FTransform SizeTransform;
	SizeTransform.Scale = DecalSize;
	return SizeTransform.GetLocalMatrix() * GetWorldMatrix();
}

void UDecalComponent::DrawDebug(FLineBatcher* LineBatcher) const
{
	if (LineBatcher == nullptr) return;

	const FMatrix BoxMatrix = GetTransformIncludingDecalSize();
	const FVector4 Color(0.0f, 0.97f, 1.0f, 1.0f);

	FVector Corners[8];
	for (int32 Index = 0; Index < 8; ++Index)
	{
		const FVector LocalCorner(
			(Index & 1) ? 1.0f : -1.0f,
			(Index & 2) ? 1.0f : -1.0f,
			(Index & 4) ? 1.0f : -1.0f);
		Corners[Index] = BoxMatrix.TransformPosition(LocalCorner);
	}

	for (int32 Index = 0; Index < 8; ++Index)
	{
		for (int32 Axis = 1; Axis <= 4; Axis <<= 1)
		{
			if ((Index & Axis) == 0)
				LineBatcher->AddLine(Corners[Index], Corners[Index | Axis], Color);
		}
	}
}

FDecalSceneProxy* UDecalComponent::CreateSceneProxy() const
{
	return new FDecalSceneProxy(this);
}

bool UDecalComponent::ShouldCreateRenderState() const
{
	return (true);
}

void UDecalComponent::CreateRenderState()
{
	Super::CreateRenderState();
	GetWorld()->GetScene().AddDecal(this);
}

void UDecalComponent::DestroyRenderState()
{
	GetWorld()->GetScene().RemoveDecal(this);
	Super::DestroyRenderState();
}

// 데칼 크기와 렌더 설정의 변경 또는 전체 복원에 따른 프록시 갱신 예약
void UDecalComponent::PostApplyProperties(const FProperty* ChangedProperty)
{
	Super::PostApplyProperties(ChangedProperty);
	// 데칼 크기 변경에 따른 자신의 투영 범위 갱신 예약
	if (ChangedProperty && ChangedProperty->Name == "DecalSize")
	{
		OnTransformDirty();
	}
	// 전체 복원의 변환 갱신은 부모가 처리하고 렌더 상태는 별도 예약
	if (!ChangedProperty || ChangedProperty->Name == "DecalTexture" || 
		ChangedProperty->Name == "DecalNormalTexture" || ChangedProperty->Name == "DecalColor" 
		|| ChangedProperty->Name == "SortOrder" || ChangedProperty->Name == "bAdditive" 
		|| ChangedProperty->Name == "FadeScreenSize")
	{
		MarkRenderStateDirty();
	}
}

bool UDecalComponent::IsPropertyVisibleInDetails(const FProperty& Property) const
{
	// Additive는 조명 계산 뒤에 더해져 법선을 쓰지 않으므로 노말 슬롯을 숨긴다. 넣어 둔 값은 그대로 둔다.
	if (bAdditive && Property.Name == "DecalNormalTexture")
	{
		return false;
	}
	return Super::IsPropertyVisibleInDetails(Property);
}

void UDecalComponent::OnTransformDirty()
{
	if (SceneProxy)
		SceneProxy->GetScene()->MarkDecalDirty(SceneProxy);
}

void UDecalComponent::MarkRenderStateDirty()
{
	if (SceneProxy)
		SceneProxy->GetScene()->MarkDecalRenderStateDirty(SceneProxy);
}