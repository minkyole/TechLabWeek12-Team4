#pragma once

#include <new>
#include <type_traits>

// 헤더툴이 해석하고 C++ 전처리에서는 제거하는 선언 표식
#define UCLASS(...)
#define UPROPERTY(...)

// 파일 식별자와 행 번호를 결합하는 내부 확장
#define UHT_BODY_JOIN_IMPL(FileId, Line) FileId##_##Line##_GENERATED_BODY

// 인자 매크로 확장을 보장하는 두 번째 결합 단계
#define UHT_BODY_JOIN(FileId, Line) UHT_BODY_JOIN_IMPL(FileId, Line)

// 현재 헤더와 선언 행에 해당하는 생성 본문의 선택
#define GENERATED_BODY() UHT_BODY_JOIN(CURRENT_FILE_ID, __LINE__)