#include "EnginePCH.h"
#include "SphereComponent.h"
#include "Collision/CollisionShape.h"

FCollisionShape USphereComponent::GetCollisionShape() const
{
	return FCollisionShape::MakeSphere(GetScaledSphereRadius());
}

FBox USphereComponent::CalcLocalBounds() const
{
	return FBox{ FVector{ -SphereRadius, -SphereRadius, -SphereRadius },
		         FVector{ SphereRadius, SphereRadius, SphereRadius } };
}

void USphereComponent::NormalizeProperties(const FProperty* ChangedProperty)
{
	Super::NormalizeProperties(ChangedProperty);
	if (!ChangedProperty || ChangedProperty->Name == "SphereRadius")
	{
		SetSphereRadius(SphereRadius);
	}
}

void USphereComponent::SetSphereRadius(float InRadius)
{
	SphereRadius = std::max(0.0f, InRadius);
}

float USphereComponent::GetScaledSphereRadius() const
{
	return SphereRadius * GetShapeScale();
}
