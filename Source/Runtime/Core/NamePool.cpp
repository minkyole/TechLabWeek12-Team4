#include "EnginePCH.h"
#include "NamePool.h"
#include <charconv>
#include <limits>

namespace UE::Core::Private
{
	FNamePool::FNamePool()
	{
		FNameEntryId NoneId(0);

		Entries.Add({ "None", NoneId });
		DisplayLookup.Add("None", NoneId);
		ComparisonLookup.Add("NONE", NoneId);
	}

	FNamePool& GetNamePool()
	{
		static FNamePool NamePool;
		return NamePool;
	}

	void ParseNumberFromName(FString& Name, int32& OutInternalNumber)
	{
		// 번호 없는 이름의 기본 결과
		OutInternalNumber = 0;

		// 1. '_' 위치를 특정.
		size_t UnderscorePosition = Name.find_last_of('_');

		if (UnderscorePosition == FString::npos || UnderscorePosition == 0 || 
			UnderscorePosition + 1 == Name.size())
			return;

		// 2. '_' 뒤쪽 숫자를 파싱.
		
		const char* NumberBegin = Name.data() + UnderscorePosition + 1;
		const char* NumberEnd = Name.data() + Name.size();

		// 숫자 변환으로 소실되는 선행 0 보존
		if (*NumberBegin == '0' && NumberEnd - NumberBegin > 1)
		{
			return;
		}

		 // 내부 번호의 증가분을 제외한 외부 번호 상한
		constexpr uint32 MaxExternalNumber = static_cast<uint32>(std::numeric_limits<int32>::max()) - 1;
		uint32 ExternalNumber = 0;
		const auto Result = std::from_chars(NumberBegin, NumberEnd, ExternalNumber);
		if (Result.ec != std::errc{} || Result.ptr != NumberEnd || ExternalNumber > MaxExternalNumber)
		{
			return;
		}

		// 전체 검사 성공 후 이름과 내부 번호 확정
		Name.resize(UnderscorePosition);
		OutInternalNumber = static_cast<int32>(ExternalNumber) + 1;

	}

	FString FNamePool::MakeComparisonKey(FStringView Name)
	{
		FString Result(Name.data(), Name.size());

		for (char& Ch : Result)
			Ch = static_cast<char>(std::toupper(static_cast<unsigned char>(Ch)));

		return Result;
	}

	FNameEntryId FNamePool::Store(FStringView Name)
	{
		if (Name.empty())
			return FNameEntryId();

		FString DisplayString(Name.data(), Name.size());

		// 대소문자까지 완전히 같은 이름이 이미 존재
		if (FNameEntryId* ExistingDisplayId = DisplayLookup.Find(DisplayString))
			return *ExistingDisplayId;

		// 대소문자를 무시하기 위한 비교용 문자열
		FString ComparisonString = MakeComparisonKey(Name);

		FNameEntryId ComparisonId;

		// 같은 이름이 이미 존재하면 기존 ComparisonId 재사용
		if (FNameEntryId* ExistingComparisonId = ComparisonLookup.Find(ComparisonString))
		{
			ComparisonId = *ExistingComparisonId;
		}
		else
		{
			// 처음 등장한 이름이면 지금 생성될 Entry가 Comparison 기준 Entry
			ComparisonId = FNameEntryId(static_cast<uint32>(Entries.Num()));
			ComparisonLookup.Add(ComparisonString, ComparisonId);
		}

		// 현재 표기법에 대한 DisplayId 생성
		FNameEntryId DisplayId(static_cast<uint32>(Entries.Num()));

		DisplayLookup.Add(DisplayString, DisplayId);
		Entries.Add({ DisplayString, ComparisonId });

		return DisplayId;
	}

	const FNameEntry& FNamePool::Resolve(FNameEntryId Id) const
	{
		return Entries[Id.ToUnstableInt()];
	}

}