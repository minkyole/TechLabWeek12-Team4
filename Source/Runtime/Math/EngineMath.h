#pragma once
#include <cmath>
#include "Vector.h"
#include "Vector2.h"
#include "Vector4.h"
// #include "TVector.h"
#include "Matrix.h"
#include "Quat.h"
#include "Rotator.h"
#include "Collision/Ray.h"
#include "VectorRegister.h"
#include "MatrixRegister.h"

#define PI 3.141592f
#define INDEX_NONE -1

namespace FMath
{
	// static float PI = std::acosf(-1);

	template <typename T>
	static inline T Min(T A, T B)
	{
		return (A >= B ? B : A);
	}

	template <typename T>
	static inline T Max(T A, T B)
	{
		return (A >= B ? A : B);
	}

	template <typename T>
	static inline T Clamp(T InValue, T InMin, T InMax)
	{
		return Max(InMin, Min(InValue, InMax));
	}

	static inline bool IsNearlyZero(float Value, float ErrorTolerance = 1e-20f)
	{
		return std::abs(Value) <= ErrorTolerance;
	}

	static inline bool IsNearlyEqual(float Value1, float Value2, float ErrorTolerance = 1e-4f)
	{
		return std::abs(Value1 - Value2) <= ErrorTolerance;
	}

	// static inline float Clamp(float Value, float Min, float Max)
	//{
	//	if (Value < Min) return Min;
	//	if (Value > Max) return Max;
	//	return Value;
	// }

	// static inline uint32 Clamp(uint32 Value, uint32 Min, uint32 Max)
	//{
	//	if (Value < Min) return Min;
	//	if (Value > Max) return Max;
	//	return Value;
	// }

	static inline float Clamp01(float Value)
	{
		if (Value < 0.0f) return 0.0f;
		if (Value > 1.0f) return 1.0f;
		return Value;
	}

	// T = 0 이면 A, T = 1 이면 B 를 반환한다.
	// 입력은 유요한, 길이가 0이 아닌 Quaternion 이어야 한다.
	static inline FQuat SlerpShortestPath(
	    const FQuat& InA, const FQuat& InB, float T)
	{
		const FQuat A = InA.Normalized();
		FQuat B = InB.Normalized();

		float Dot =
		    A.X * B.X + A.Y * B.Y + A.Z * B.Z + A.W * B.W;

		// q와 -q는 같은 회전이다.
		// 부호를 맞춰 짧은 회전 경로를 선택한다.
		if (Dot < 0.0f)
		{
			B = FQuat(-B.X, -B.Y, -B.Z, -B.W);
			Dot = -Dot;
		}

		// 부동소수점 오차로 acos의 입력 범위를 벗어나지 않게 한다.
		Dot = Clamp01(Dot);

		// 매우 가까운 회전에서는 정규화 선형 보간을 사용한다.
		if (Dot > 0.9995f)
		{
			return FQuat(
			           A.X + T * (B.X - A.X),
			           A.Y + T * (B.Y - A.Y),
			           A.Z + T * (B.Z - A.Z),
			           A.W + T * (B.W - A.W))
			    .Normalized();
		}

		const float Theta = std::acosf(Dot);
		const float SinTheta = std::sinf(Theta);

		const float FactorA =
		    std::sin((1.0f - T) * Theta) / SinTheta;
		const float FactorB =
		    std::sin(T * Theta) / SinTheta;

		return FQuat(
		           A.X * FactorA + B.X * FactorB,
		           A.Y * FactorA + B.Y * FactorB,
		           A.Z * FactorA + B.Z * FactorB,
		           A.W * FactorA + B.W * FactorB)
		    .Normalized();
	}

	static inline float RadiansToDegrees(float Radian)
	{
		return Radian * (180.0f / PI);
	}

	static inline float DegreesToRadians(float Degree)
	{
		return Degree * (PI / 180.0f);
	}
}
