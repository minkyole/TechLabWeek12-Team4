#pragma once

#include "UObject/Class.h"
#include "../Components/SceneComponent.h"
#include "../Math/EngineMath.h"
#include "../Math/Rotator.h"
#include "Camera/MinimalViewInfo.h"


class UCameraComponent : public USceneComponent {
	DECLARE_CLASS(UCameraComponent, USceneComponent)
	REFLECT_START(UCameraComponent)
		PROPERTY(mFieldOfView)
		PROPERTY(mAspectRatio)
		PROPERTY(mNearClipPlane)
		PROPERTY(mFarClipPlane)
		PROPERTY(mIsOrthogonal)
		PROPERTY(mOrthoWidth)
		PROPERTY(mMoveSpeed)
		PROPERTY(mMouseSensitivity)
		PROPERTY(mWheelSpeed)
	REFLECT_END()

public:
	UCameraComponent();   // 이동·회전 입력을 매 프레임 처리한다

public:
	// 매 프레임 카메라 입력/이동 처리
	virtual void TickComponent(float DeltaTime) override;
	void GetCameraView(FMinimalViewInfo& OutView) const;

	// Camera 행렬 계산
	FMatrix GetViewMatrix() const;
	FMatrix GetPerspectiveMatrix() const;
	FMatrix GetOrthogonalMatrix() const;
	FMatrix GetProjectionMatrix() const;
	FMatrix GetViewProjectionMatrix() const;
	bool CanSeeActor(const AActor& Target) const;

	// Picking
	FRay DeProjection(FVector2 MousePos, float ScreenW, float ScreenH);

	// Get & Set
	float GetFieldOfView() const;
	void SetFieldOfView(float InFieldOfView);

	float GetAspectRatio() const;
	void SetAspectRatio(float InAspectRatio);

	float GetNearZ() const;
	void SetNearZ(float InNearZ);

	float GetFarZ() const;
	void SetFarZ(float InFarZ);

	bool GetIsOrthogonal() const;
	void SetIsOrthogonal(bool InIsOrthographic);

	float GetOrthoWidth() const;
	void SetOrthoWidth(float InOrthoWidth);

	// 마우스 조작 설정
	float GetMoveSpeed() const;
	void SetMoveSpeed(float InMoveSpeed);

	float GetMouseSensitivity() const;
	void SetMouseSensitivity(float InMouseSensitivity);

	float GetWheelSpeed() const;
	void SetWheelSpeed(float InWheelSpeed);
	// 외부 Adapter가 View별 입력을 처리할 때 기본 카메라의 중복 입력 갱신을 끈다.
	void SetExternalInputManaged(bool InValue);
	bool IsExternalInputManaged() const;

	// 마우스 조작 업데이트
	void UpdateMovement(float DeltaTime); // 마우스 이동
	void UpdateRotation(float DeltaTime); // 마우스 우클릭 회전
	void UpdateZoom(float DeltaTime);     // 마우스 휠 줌

private:
	virtual void OnUnregister() override;

private:
	float mFieldOfView{ 60.0f };
	float mAspectRatio{ 16.0f / 9.0f };
	float mNearClipPlane{ 0.1f };
	float mFarClipPlane{ 10000.0f };

	bool mIsOrthogonal{};
	float mOrthoWidth{ 16.0f };

	float mMoveSpeed{ 10.0f };
	float mMouseSensitivity{ 0.05f };
	float mWheelSpeed{ 0.1f };
	bool mExternalInputManaged{};

};

