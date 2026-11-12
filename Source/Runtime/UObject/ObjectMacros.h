#pragma once

#include "Core/Types.h"

enum class EObjectFlags : uint32
{
	RF_NoFlags = 0x00000000, // 플래그 없음
	RF_ClassDefaultObject = 0x00000010, // CDO.
	RF_Transactional = 0x00000008, // Undo Redo 관리 대상
	RF_Transient = 0x00000040, // 직렬화 제외 플래그
	RF_DefaultSubObject = 0x00040000,
	RF_AllFlags = 0xffffffff, // 모든 플래그
};

DEFINE_ENUM_OPERATORS(EObjectFlags)

// 클래스 저장 플래그
enum class EClassFlags : uint32
{
	CLASS_None = 0x00000000,			// 일반 클래스
	CLASS_Abstract = 0x00000001,		// 추상 클래스, 일반 인스턴스 생성 금지
	CLASS_NotPlaceable = 0x00000200		// 에디터 배치 금지
};

DEFINE_ENUM_OPERATORS(EClassFlags)

// 선언에 쓰는 입력값
enum class EClassSpecifiers : uint32
{
	None = 0,				// 일반 클래스
	Abstract = 1 << 0,		// 추상 클래스
	NotPlaceable = 1 << 1,  // 에디터 배치 금지
	Placeable = 1 << 2		// 에디터 배치 금지 해제
};

DEFINE_ENUM_OPERATORS(EClassSpecifiers)