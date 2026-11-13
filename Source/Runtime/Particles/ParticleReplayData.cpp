#include "EnginePCH.h"
#include "ParticleReplayData.h"

#include <cassert>
#include <cstddef>
#include <memory>

FParticleDataContainer::~FParticleDataContainer()
{
	Reset();
}

FParticleDataContainer::FParticleDataContainer(
    FParticleDataContainer&& Other) noexcept
{
	*this = std::move(Other);
}

FParticleDataContainer& FParticleDataContainer::operator=(
    FParticleDataContainer&& Other) noexcept
{
	if (this == &Other)
		return *this;

	Reset();

	MemBlockSize = std::exchange(Other.MemBlockSize, 0);
	ParticleDataNumBytes = std::exchange(Other.ParticleDataNumBytes, 0);
	ParticleIndicesNumShorts = std::exchange(Other.ParticleIndicesNumShorts, 0);
	ParticleData = std::exchange(Other.ParticleData, nullptr);
	ParticleIndices = std::exchange(Other.ParticleIndices, nullptr);

	return *this;
}

void FParticleDataContainer::CopyFrom(
    const uint8* InParticleData,
    int32 InParticleDataNumBytes,
    const uint16* InParticleIndices,
    int32 InParticleIndicesNumShorts)
{
	assert(InParticleDataNumBytes >= 0);
	assert(InParticleIndicesNumShorts >= 0);

	assert(InParticleDataNumBytes == 0 || InParticleData != nullptr);
	assert(InParticleIndicesNumShorts == 0 || InParticleIndices != nullptr);

	const size_t DataBytes =
	    static_cast<size_t>(InParticleDataNumBytes);

	const size_t IndexCount =
	    static_cast<size_t>(InParticleIndicesNumShorts);

	// 인덱스 영역의 시작 위치를 uint16 정렬 경계에 맞춘다.
	constexpr size_t IndexAlignment = alignof(uint16);

	const size_t IndexOffset =
	    (DataBytes + IndexAlignment - 1) &
	    ~(IndexAlignment - 1);

	const size_t TotalBytes =
	    IndexOffset + IndexCount * sizeof(uint16);

	assert(TotalBytes <=
	       static_cast<size_t>(std::numeric_limits<int32>::max()));

	if (TotalBytes == 0)
	{
		Reset();
		return;
	}

	// 기존 데이터는 복사를 끝낸 뒤 해제한다.
	auto NewBlock = MakeUnique<uint8[]>(TotalBytes);

	if (DataBytes > 0)
	{
		std::memcpy(
		    NewBlock.get(),
		    InParticleData,
		    DataBytes);
	}

	uint16* NewIndices = nullptr;

	if (IndexCount > 0)
	{
		NewIndices = reinterpret_cast<uint16*>(
		    NewBlock.get() + IndexOffset);

		std::memcpy(
		    NewIndices,
		    InParticleIndices,
		    IndexCount * sizeof(uint16));
	}

	Reset();

	MemBlockSize = static_cast<int32>(TotalBytes);
	ParticleDataNumBytes = InParticleDataNumBytes;
	ParticleIndicesNumShorts = InParticleIndicesNumShorts;

	ParticleData = NewBlock.release();
	ParticleIndices = NewIndices;
}

void FParticleDataContainer::Reset()
{
	// ParticleIndices는 같은 블록 내부를 가리키므로 별도 해제하지 않는다.
	delete[] ParticleData;

	ParticleData = nullptr;
	ParticleIndices = nullptr;

	MemBlockSize = 0;
	ParticleDataNumBytes = 0;
	ParticleIndicesNumShorts = 0;
}

const uint8* FParticleDataContainer::GetParticleData() const
{
	return ParticleData;
}

const uint16* FParticleDataContainer::GetParticleIndices() const
{
	return ParticleIndices;
}

int32 FParticleDataContainer::GetParticleDataNumBytes() const
{
	return ParticleDataNumBytes;
}

int32 FParticleDataContainer::GetParticleIndicesNumShorts() const
{
	return ParticleIndicesNumShorts;
}

uint16 FDynamicEmitterReplayDataBase::GetParticleIndex(
    int32 ActiveIndex) const
{
	assert(ActiveParticleCount >= 0);
	assert(ActiveParticleCount <= DataContainer.GetParticleIndicesNumShorts());

	assert(ActiveIndex >= 0);
	assert(ActiveIndex < ActiveParticleCount);

	assert(ParticleStride >= static_cast<int32>(sizeof(FBaseParticle)));

	assert(ParticleStride % alignof(FBaseParticle) == 0);

	const uint16* Indices = DataContainer.GetParticleIndices();

	assert(Indices != nullptr);

	const uint16 SlotIndex = Indices[ActiveIndex];

	const size_t SlotEnd =
	    (static_cast<size_t>(SlotIndex) + 1) * static_cast<size_t>(ParticleStride);

	assert(SlotEnd <= static_cast<size_t>(
	                      DataContainer.GetParticleDataNumBytes()));
	return SlotIndex;
}

const FBaseParticle& FDynamicEmitterReplayDataBase::GetParticle(
    int32 ActiveIndex) const
{
	const uint16 SlotIndex = GetParticleIndex(ActiveIndex);

	const uint8* Data = DataContainer.GetParticleData();
	assert(Data != nullptr);

	const size_t Offset =
	    static_cast<size_t>(SlotIndex) * static_cast<size_t>(ParticleStride);

	return *reinterpret_cast<const FBaseParticle*>(Data + Offset);
}

const uint8* FDynamicEmitterReplayDataBase::GetParticlePayload(
    int32 ActiveIndex,
    int32 PayloadOffset) const
{
	if (PayloadOffset == InvalidPayloadOffset)
	{
		return nullptr;
	}

	const uint16 SlotIndex = GetParticleIndex(ActiveIndex);

	assert(PayloadOffset >= static_cast<int32>(sizeof(FBaseParticle)));

	assert(PayloadOffset < ParticleStride);

	const uint8* Data = DataContainer.GetParticleData();
	assert(Data != nullptr);

	const size_t Offset =
	    static_cast<size_t>(SlotIndex) *
	    static_cast<size_t>(ParticleStride) *
	    static_cast<size_t>(PayloadOffset);

	return Data + Offset;
}

FDynamicMeshEmitterReplayData::FDynamicMeshEmitterReplayData()
{
	eEmitterType = EDynamicEmitterType::Mesh;
}
