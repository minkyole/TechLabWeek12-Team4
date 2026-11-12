#include "EnginePCH.h"
#include "SkeletalMeshBake.h"

#include "Asset/FBXImporter/FbxImportData.h"

#include <cmath>
#include <fstream>
#include <limits>
#include <type_traits>

namespace
{
	constexpr uint32 BakeMagic = 0x4B424B53; // SKBK
	// 바이너리 형식이나 FBX 임포트·쿠킹 규칙이 바뀌면 버전을 올린다.
	constexpr uint32 BakeVersion = 4;

	class FBakeReader
	{
	public:
		explicit FBakeReader(std::ifstream& In, uintmax_t Size) : Stream(In), Remaining(Size) {}

		template<typename T>
		bool Pod(T& Value)
		{
			static_assert(std::is_trivially_copyable_v<T>);
			return ReadBytes(&Value, sizeof(T));
		}

		template<typename T>
		bool Array(TArray<T>& Values)
		{
			static_assert(std::is_trivially_copyable_v<T>);
			uint32 Count = 0;
			if (!ReadCount(Count, sizeof(T))) return false;
			Values.SetNum(static_cast<int32>(Count));
			return ReadBytes(Values.GetData(), sizeof(T) * static_cast<uintmax_t>(Count));
		}

		template<typename T, typename F>
		bool Records(TArray<T>& Values, F Transfer)
		{
			uint32 Count = 0;
			if (!ReadCount(Count, sizeof(uint32))) return false;
			for (uint32 Index = 0; Index < Count; ++Index)
			{
				T Value{};
				if (!Transfer(*this, Value)) return false;
				Values.Add(std::move(Value));
			}
			return true;
		}

		bool String(FString& Value)
		{
			uint32 Length = 0;
			if (!ReadCount(Length, 1)) return false;
			Value.resize(Length);
			return ReadBytes(Value.data(), Length);
		}

		bool AtEnd() const { return Remaining == 0; }

	private:
		bool ReadCount(uint32& Count, uintmax_t ElementSize)
		{
			return Pod(Count) && Count <= (std::numeric_limits<int32>::max)() &&
				Count <= Remaining / ElementSize;
		}

		bool ReadBytes(void* Data, uintmax_t Size)
		{
			if (Size == 0) return true;
			if (Size > Remaining || Size > (std::numeric_limits<std::streamsize>::max)()) return false;
			if (!Stream.read(static_cast<char*>(Data), static_cast<std::streamsize>(Size))) return false;
			Remaining -= Size;
			return true;
		}

		std::ifstream& Stream;
		uintmax_t Remaining;
	};

	class FBakeWriter
	{
	public:
		explicit FBakeWriter(std::ofstream& Out) : Stream(Out) {}

		template<typename T>
		bool Pod(const T& Value)
		{
			static_assert(std::is_trivially_copyable_v<T>);
			return WriteBytes(&Value, sizeof(T));
		}

		template<typename T>
		bool Array(const TArray<T>& Values)
		{
			static_assert(std::is_trivially_copyable_v<T>);
			return Pod(static_cast<uint32>(Values.Num())) &&
				WriteBytes(Values.GetData(), sizeof(T) * static_cast<size_t>(Values.Num()));
		}

		template<typename T, typename F>
		bool Records(const TArray<T>& Values, F Transfer)
		{
			if (!Pod(static_cast<uint32>(Values.Num()))) return false;
			for (const T& Value : Values)
			{
				if (!Transfer(*this, Value)) return false;
			}
			return true;
		}

		bool String(const FString& Value)
		{
			return Value.size() <= (std::numeric_limits<int32>::max)() &&
				Pod(static_cast<uint32>(Value.size())) && WriteBytes(Value.data(), Value.size());
		}

	private:
		bool WriteBytes(const void* Data, size_t Size)
		{
			return Size == 0 || static_cast<bool>(
				Stream.write(static_cast<const char*>(Data), static_cast<std::streamsize>(Size)));
		}

		std::ofstream& Stream;
	};

	// 읽기와 쓰기에서 같은 필드 순서를 사용한다.
	template<typename A, typename T>
	bool TransferSkeletalMesh(A& Archive, T& Data)
	{
		return Archive.Records(Data.Bones, [](auto& Ar, auto& Bone)
		{
			return Ar.String(Bone.Name) && Ar.Pod(Bone.ParentIndex) &&
				Ar.Pod(Bone.RefPoseTransform.Location) && Ar.Pod(Bone.RefPoseTransform.Scale) &&
				Ar.Pod(Bone.RefPoseTransform.Rotation.V);
		}) && Archive.Records(Data.Meshes, [](auto& Ar, auto& Mesh)
		{
			return Ar.Pod(Mesh.MeshTransform.M) &&
				Ar.Records(Mesh.InverseBindPoseMat, [](auto& MatrixAr, auto& Matrix)
				{
					return MatrixAr.Pod(Matrix.M);
				}) &&
				Ar.Array(Mesh.Vertices) && Ar.Array(Mesh.Indices) && Ar.Array(Mesh.Sections) &&
				Ar.Records(Mesh.MaterialSlots, [](auto& SlotAr, auto& Slot)
				{
					return SlotAr.String(Slot.Name) && SlotAr.Pod(Slot.BaseColor) &&
						SlotAr.String(Slot.BaseColorTexturePath) && SlotAr.String(Slot.NormalTexturePath);
				});
		});
	}

	// FName은 실행마다 내부 인덱스가 달라서 문자열로 저장한다.
	bool TransferName(FBakeWriter& Writer, const FName& Name) { return Writer.String(Name.ToString()); }
	bool TransferName(FBakeReader& Reader, FName& Name)
	{
		FString Value;
		if (!Reader.String(Value)) return false;
		Name = FName(Value);
		return true;
	}

	template<typename A, typename T>
	bool TransferAnims(A& Archive, T& Anims)
	{
		return Archive.Records(Anims, [](auto& Ar, auto& Clip)
		{
			return Ar.String(Clip.Name) && Ar.Pod(Clip.FrameRate) && Ar.Pod(Clip.NumFrames) &&
				Ar.Records(Clip.Tracks, [](auto& TrackAr, auto& Track)
				{
					auto& Keys = Track.InternalTrack;
					return TransferName(TrackAr, Track.Name) &&
						TrackAr.Array(Keys.PosKeys) && TrackAr.Array(Keys.RotKeys) && TrackAr.Array(Keys.ScaleKeys);
				});
		});
	}

	template<typename A, typename T>
	bool TransferMeshData(A& Archive, T& Data)
	{
		return TransferSkeletalMesh(Archive, Data.SkeletalMesh) && TransferAnims(Archive, Data.Anims) &&
			Archive.Records(Data.StaticMeshes, [](auto& Ar, auto& Part)
			{
				auto& Mesh = Part.MeshData;
				return Ar.String(Part.NodeName) && Ar.Pod(Part.NodeTransform.M) &&
					Ar.Array(Mesh.Vertices) && Ar.Array(Mesh.Indices) && Ar.Array(Mesh.Sections) &&
					Ar.Pod(Mesh.AABB) && Ar.Records(Mesh.MaterialSlots, [](auto& SlotAr, auto& Slot)
					{
						return SlotAr.String(Slot.Name) && SlotAr.Pod(Slot.BaseColor) &&
							SlotAr.String(Slot.DiffuseTexturePath) && SlotAr.String(Slot.NormalTexturePath);
					});
			});
	}

	bool ValidateSkeletalMesh(const FSkeletalMeshData& Data)
	{
		for (int32 Index = 0; Index < Data.Bones.Num(); ++Index)
		{
			const int32 Parent = Data.Bones[Index].ParentIndex;
			if (Parent < -1 || Parent >= Index) return false;
		}

		bool HasTriangles = false;
		for (const FSkeletalMesh& Mesh : Data.Meshes)
		{
			if (Mesh.InverseBindPoseMat.Num() != Data.Bones.Num() || Mesh.Indices.Num() % 3 != 0)
				return false;

			const uint32 VertexCount = static_cast<uint32>(Mesh.Vertices.Num());
			const uint32 IndexCount = static_cast<uint32>(Mesh.Indices.Num());
			for (uint32 Index : Mesh.Indices)
			{
				if (Index >= VertexCount) return false;
			}
			for (const FSkeletalMeshSection& Section : Mesh.Sections)
			{
				if (Section.StartIndex > IndexCount || Section.IndexCount > IndexCount - Section.StartIndex ||
					Section.MaterialSlotIndex >= static_cast<uint32>(Mesh.MaterialSlots.Num()))
					return false;
			}
			for (const FSkeletalVertex& Vertex : Mesh.Vertices)
			{
				for (int32 Index = 0; Index < 4; ++Index)
				{
					const float Weight = Vertex.BoneWeights[Index];
					if (!std::isfinite(Weight) || Weight < 0.0f ||
						(Weight > 0.0f && Vertex.BoneIndices[Index] >= static_cast<uint32>(Data.Bones.Num())))
						return false;
				}
			}
			HasTriangles |= IndexCount != 0;
		}
		return HasTriangles;
	}

	bool ValidateAnims(const TArray<FFbxAnimClip>& Anims)
	{
		for (const FFbxAnimClip& Clip : Anims)
		{
			// 분모가 0이면 AsDecimal에서 0으로 나누게 된다.
			if (Clip.FrameRate.Numerator <= 0 || Clip.FrameRate.Denominator <= 0 || Clip.NumFrames < 0)
				return false;
			for (const FBoneAnimationTrack& Track : Clip.Tracks)
			{
				if (!Track.InternalTrack.IsValid()) return false;
			}
		}
		return true;
	}

	bool Validate(const FFbxCookedData& Data)
	{
		if (!Data.SkeletalMesh.Meshes.IsEmpty() && !ValidateSkeletalMesh(Data.SkeletalMesh))
			return false;
		if (!ValidateAnims(Data.Anims))
			return false;
		for (const FFbxStaticMeshPart& Part : Data.StaticMeshes)
		{
			FString Error;
			if (!Part.MeshData.Validate(Error)) return false;
		}
		return !Data.SkeletalMesh.Meshes.IsEmpty() || !Data.StaticMeshes.IsEmpty();
	}

	std::filesystem::path SourceFile(const FString& Path)
	{
		return std::filesystem::path(std::u8string(Path.begin(), Path.end()));
	}
}

bool FSkeletalMeshBake::ReadBaked(const FString& SourcePath, std::optional<std::filesystem::file_time_type> ExpectedSourceTime, FFbxCookedData& OutData)
{
	auto BinPath = SourceFile(SourcePath);
	BinPath += ".bin";
	std::ifstream In(BinPath, std::ios::binary | std::ios::ate);
	if (!In) return false;

	const auto Size = In.tellg();
	if (Size < 0 || !In.seekg(0)) return false;

	FBakeReader Reader(In, static_cast<uintmax_t>(static_cast<std::streamoff>(Size)));
	uint32 Magic = 0;
	uint32 Version = 0;
	int64 Stamp = 0;
	FFbxCookedData Data;
	if (!Reader.Pod(Magic) || Magic != BakeMagic ||
		!Reader.Pod(Version) || Version != BakeVersion ||
		!Reader.Pod(Stamp) || (ExpectedSourceTime && Stamp != ExpectedSourceTime->time_since_epoch().count()) ||
		!TransferMeshData(Reader, Data) || !Reader.AtEnd() || !Validate(Data))
		return false;

	OutData = std::move(Data);
	HTR_LOG(Info, "[FBX BAKE] {}.bin: loaded", SourcePath);
	return true;
}

bool FSkeletalMeshBake::WriteBakedDirect(const std::filesystem::path& TargetBinPath, const FFbxCookedData& Data, int64 Stamp)
{
	if (!Validate(Data))
		return false;

	auto TempPath = TargetBinPath;
	TempPath += ".tmp." + std::to_string(GetCurrentProcessId()) + "." + std::to_string(GetCurrentThreadId());

	std::ofstream Out(TempPath, std::ios::binary | std::ios::trunc);
	FBakeWriter Writer(Out);
	const bool Written = Out && Writer.Pod(BakeMagic) && Writer.Pod(BakeVersion) &&
		Writer.Pod(Stamp) && TransferMeshData(Writer, Data);
	Out.close();

	std::error_code Error;
	if (Written && Out)
	{
		std::filesystem::rename(TempPath, TargetBinPath, Error);
		if (!Error)
		{
			HTR_LOG(Info, "[FBX BAKE] {}: successfully written", TargetBinPath.string());
			return true;
		}
	}

	std::filesystem::remove(TempPath, Error);
	HTR_LOG(Warning, "[FBX BAKE] {}: cache not written", TargetBinPath.string());
	return false;
}

void FSkeletalMeshBake::WriteBaked(const FString& SourcePath, std::filesystem::file_time_type SourceTime, const FFbxCookedData& Data)
{
	const auto Source = SourceFile(SourcePath);
	std::error_code Error;
	if (std::filesystem::last_write_time(Source, Error) != SourceTime || Error)
		return;

	auto BinPath = Source;
	BinPath += ".bin";
	const int64 Stamp = SourceTime.time_since_epoch().count();
	WriteBakedDirect(BinPath, Data, Stamp);
}
