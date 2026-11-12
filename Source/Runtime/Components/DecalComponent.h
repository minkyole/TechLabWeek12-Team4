#pragma once

#include "SceneComponent.h"
#include "Render/Texture2D.h"

class FDecalSceneProxy;
class FLineBatcher;

class UDecalComponent : public USceneComponent
{
    DECLARE_CLASS(UDecalComponent, USceneComponent)
    REFLECT_START(UDecalComponent)
        PROPERTY(DecalTexture)
        PROPERTY(DecalNormalTexture)
        PROPERTY(DecalSize)
        PROPERTY(DecalColor)
        PROPERTY(SortOrder)
        PROPERTY(bAdditive)
        PROPERTY(FadeScreenSize)
        PROPERTY(FadeStartDelay)
        PROPERTY(FadeDuration)
        PROPERTY(FadeInDuration)
        PROPERTY(FadeInStartDelay)
        PROPERTY(bDestroyOwnerAfterFade)
    REFLECT_END()

public:
    UDecalComponent();

    virtual void BeginPlay() override;
    virtual void TickComponent(float DeltaTime) override;


    bool ShouldCreateRenderState() const override;
    void CreateRenderState() override;
    void DestroyRenderState() override;
	void PostApplyProperties(const FProperty* ChangedProperty = nullptr) override;
	bool IsPropertyVisibleInDetails(const FProperty& Property) const override;
    void OnTransformDirty() override;
    void MarkRenderStateDirty();
    FDecalSceneProxy* CreateSceneProxy() const;

    UTexture2D* GetDecalTexture() const;
    void SetDecalTexture(UTexture2D* InDecalTexture);
    // 없으면 nullptr. 이때 Decal은 법선을 바꾸지 않는다.
    UTexture2D* GetDecalNormalTexture() const;
    void SetDecalNormalTexture(UTexture2D* InDecalNormalTexture);
    void SetDecalColor(const FLinearColor& InDecalColor);
    void SetSortOrder(int32 InSortOrder);
    void SetDecalSize(const FVector& InDecalSize);
    void SetAdditive(bool bInAdditive);
    void SetFadeScreenSize(float InFadeScreenSize);

    const FLinearColor& GetDecalColor() const;
    int32 GetSortOrder() const;
    float GetFadeAlpha() const;
    float GetFadeScreenSize() const;
    bool IsAdditive() const;
    
    FMatrix GetTransformIncludingDecalSize() const;   // 프록시와 셰이더에서 사용
    void DrawDebug(FLineBatcher* LineBatcher) const;

    FDecalSceneProxy* SceneProxy = nullptr;
protected:
    UTexture2D* DecalTexture = nullptr;
    UTexture2D* DecalNormalTexture = nullptr;

private:
    
    FVector             DecalSize = FVector(1.0f, 1.0f, 1.0f);
    FLinearColor        DecalColor = FLinearColor(1, 1, 1, 1);
    int32               SortOrder = 0;
    bool                bAdditive = false;

    // Fade 관련 설정
    float   FadeScreenSize = 0.01f; // 화면 비율
    float   FadeStartDelay = 0.0f; // 기준시간(초)
    float   FadeDuration = 0.0f;
    float   FadeInDuration = 0.0f;
    float   FadeInStartDelay = 0.0f;
    float   FadeElapsedTime = 0.0f;
    bool    bDestroyOwnerAfterFade = true;
    bool    bFadeFinished = false;

    

};