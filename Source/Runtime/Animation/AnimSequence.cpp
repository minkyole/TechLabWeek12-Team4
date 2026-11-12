#include "EnginePCH.h"
#include "AnimSequence.h"
#include "Animation/AnimNotifies/AnimNotify.h"
#include "Render/Mesh.h"

namespace
{
	int32 ClampKeyIndex(int32 KeyIndex, int32 NumKeys)
	{
		return FMath::Clamp(KeyIndex, 0, NumKeys - 1);
	}
}

UAnimSequenceBase::UAnimSequenceBase()
{
	DataModel = CreateDefaultSubobject<UAnimDataModel>(FName("AnimDataModel"));
}

UAnimSequenceBase::~UAnimSequenceBase()
{
	ClearNotifies();

	// 전역 종료에서 먼저 삭제된 데이터 모델의 중복 해제 방지
	if (GUObjectArray.Find(DataModel) != INDEX_NONE)
	{
		delete DataModel;
	}
	DataModel = nullptr;
}

void UAnimSequenceBase::RemoveNotifyAt(int32 Index)
{
	if (Index < 0 || Index >= Notifies.Num())
	{
		return;
	}
	delete Notifies[Index].Notify;
	Notifies.RemoveAt(Index);
}

void UAnimSequenceBase::ClearNotifies()
{
	for (FAnimNotifyEvent& Event : Notifies)
	{
		delete Event.Notify;
		Event.Notify = nullptr;
	}
	Notifies.Empty();
}

void UAnimSequenceBase::OnNotifyObjectDestroyed(const UAnimNotify* DestroyedNotify)
{
	for (FAnimNotifyEvent& Event : Notifies)
	{
		if (Event.Notify == DestroyedNotify)
		{
			Event.Notify = nullptr;
		}
	}
}

void UAnimSequenceBase::SetNotifyObject(int32 Index, UAnimNotify* NewNotify)
{
	if (Index < 0 || Index >= Notifies.Num())
	{
		delete NewNotify;
		return;
	}
	if (Notifies[Index].Notify != NewNotify)
	{
		delete Notifies[Index].Notify;
		Notifies[Index].Notify = NewNotify;
	}
}

// 
void UAnimSequence::GetAnimationPose(float Time, FAnimationPose& OutPose, bool bRootLock) const
{
	const USkeletalMesh* TargetMesh = OutPose.TargetMesh;
	if (!TargetMesh)
	{
		return;
	}

	OutPose.ResetToRefPose();
	if (CachedMesh != TargetMesh)
	{
		RebuildBoneTrackMap(*TargetMesh);
	}

	// 해당 Time의 Lookup을 구한다
	FAnimKeyLookup Lookup = FindKeyLookup(Time);
	const auto& Tracks = DataModel->GetBoneAnimationTracks();
	for (int32 i = 0; i < TargetMesh->MeshData.Bones.Num(); ++i)
	{
		int32 TrackIndex = BoneToTrackIndex[i];
		if (TrackIndex == -1)
		{
			continue;
		}
		// 본 마다 Track대로 Transform을 업데이트한다 
		SampleTrack(Tracks[TrackIndex].InternalTrack, Lookup, OutPose.LocalTransforms[i]);
	}

	// 루트가 간 만큼은 Actor가 대신 가므로 포즈에서는 빼야 두 번 움직이지 않는다
	// ForceLock 클립은 이동을 넘기지 않으니, 잠그지 않으면 메시가 캡슐 앞으로 걸어 나간다
	if (bRootLock && (bEnableRootMotion || bForceRootLock) && RootBoneIndex != -1)
	{
		// 추출이 위치·회전만 넘기니 잠그는 것도 위치·회전만이다. 스케일은 클립 값을 둔다
		const FTransform& Ref = TargetMesh->MeshData.Bones[RootBoneIndex].RefPoseTransform;
		FAnimBoneTransform& Root = OutPose.LocalTransforms[RootBoneIndex];
		Root.Translation = Ref.Location;
		Root.Rotation = FQuat(Ref.Rotation);
	}
}

// 시간을 입력받아 Lookup(키 2개, 비율 alpha)로 반환
FAnimKeyLookup UAnimSequence::FindKeyLookup(float Time) const
{
	int32 NumKeys = DataModel->GetNumberOfKeys();
	if (NumKeys <= 1)
	{
		return FAnimKeyLookup{ 0, 0, 0.f };
	}
	float ClampedTime = FMath::Clamp(Time, 0.f, GetPlayLength());
	float FrameNum = ClampedTime * DataModel->GetFrameRate().AsDecimal();
	int32 KeyIndex0 = FMath::Clamp(static_cast<int32>(std::floor(FrameNum)), 0, NumKeys - 1);
	int32 KeyIndex1 = FMath::Min(KeyIndex0 + 1, NumKeys - 1);
	float Alpha = FMath::Clamp01(FrameNum - KeyIndex0);

	return FAnimKeyLookup{KeyIndex0, KeyIndex1, Alpha};
}

// Lookup 대로 위치, 회전, 스케일을 두 키사이의 alpha대로 보간한다. 
void UAnimSequence::SampleTrack(const FRawAnimSequenceTrack& Track, const FAnimKeyLookup& Lookup, FAnimBoneTransform& OutTransform)
{
	// Translation
	if (!Track.PosKeys.IsEmpty())
	{
		int32 PosIndex0 = ClampKeyIndex(Lookup.KeyIndex0, Track.PosKeys.Num());
		int32 PosIndex1 = ClampKeyIndex(Lookup.KeyIndex1, Track.PosKeys.Num());

		FVector Pos0 = Track.PosKeys[PosIndex0];
		FVector Pos1 = Track.PosKeys[PosIndex1];

		OutTransform.Translation = Pos0 + (Pos1 - Pos0) * Lookup.Alpha;	
	}

	// Rotation
	if (!Track.RotKeys.IsEmpty())
	{
		int32 RotIndex0 = ClampKeyIndex(Lookup.KeyIndex0, Track.RotKeys.Num());
		int32 RotIndex1 = ClampKeyIndex(Lookup.KeyIndex1, Track.RotKeys.Num());

		FQuat Rot0 = Track.RotKeys[RotIndex0];
		FQuat Rot1 = Track.RotKeys[RotIndex1];

		OutTransform.Rotation = FMath::SlerpShortestPath(Rot0, Rot1, Lookup.Alpha);
	}

	// Scale
	if (!Track.ScaleKeys.IsEmpty())
	{
		int32 ScaleIndex0 = ClampKeyIndex(Lookup.KeyIndex0, Track.ScaleKeys.Num());
		int32 ScaleIndex1 = ClampKeyIndex(Lookup.KeyIndex1, Track.ScaleKeys.Num());

		FVector Scale0 = Track.ScaleKeys[ScaleIndex0];
		FVector Scale1 = Track.ScaleKeys[ScaleIndex1];

		OutTransform.Scale = Scale0 + (Scale1 - Scale0) * Lookup.Alpha;
	}
}

// Mesh의 Bone을 시퀀스의 BoneAnimationTracks에 넣고 그때의 Index를 BoneToTrackIndex에 기록한다.
void UAnimSequence::RebuildBoneTrackMap(const USkeletalMesh& Mesh) const
{
	int32 BonesNum = Mesh.MeshData.Bones.Num();
	BoneToTrackIndex.SetNum(BonesNum);
	RootBoneIndex = -1;

	for (int32 i = 0; i < BonesNum; ++i)
	{
		FName BoneName = FName(Mesh.MeshData.Bones[i].Name);
		int32 FoundIndex = DataModel->FindBoneTrackIndex(BoneName);
		BoneToTrackIndex[i] = FoundIndex;

		// 부모가 없는 첫 본을 루트로 본다. 이 본의 로컬 값이 곧 컴포넌트 공간 값이다
		if (RootBoneIndex == -1 && Mesh.MeshData.Bones[i].ParentIndex == -1)
		{
			RootBoneIndex = i;
		}
	}
	CachedMesh = &Mesh;
}

// Importer 가 만든 키 배열을 AnimDataModel에 옮긴다
void UAnimSequence::BuildFromTracks(const FFrameRate& FrameRate, int32 NumFrames, const TArray<FBoneAnimationTrack>& Tracks)
{
	DataModel->Reset();
	// SetBoneTrackKeys의 키 개수 검사가 NumberOfKeys를 보므로 프레임 정보를 먼저 넣는다.
	DataModel->SetFrameRate(FrameRate);
	DataModel->SetNumberOfFrames(NumFrames);
	for (const FBoneAnimationTrack& Track : Tracks)
	{
		const int32 TrackIndex = DataModel->AddBoneTrack(Track.Name);
		const FRawAnimSequenceTrack& Keys = Track.InternalTrack;
		DataModel->SetBoneTrackKeys(TrackIndex, Keys.PosKeys, Keys.RotKeys, Keys.ScaleKeys);
	}
	// 트랙 순서가 바뀌었으니 같은 메시라도 본→트랙 맵을 다시 만들게 한다.
	CachedMesh = nullptr;
}

FRootMotionParams UAnimSequence::ExtractRootMotion(const USkeletalMesh& Mesh, float StartTime, float DeltaTime, bool bLoop) const
{
	if (!bEnableRootMotion || DeltaTime == 0.0f || !std::isfinite(StartTime) || !std::isfinite(DeltaTime))
	{
		return FRootMotionParams{};
	}

	if (CachedMesh != &Mesh)
	{
		RebuildBoneTrackMap(Mesh);
	}

	const float Length = GetPlayLength();
	if (RootBoneIndex == -1 || BoneToTrackIndex[RootBoneIndex] == -1 || Length <= 0.0f)
	{
		return FRootMotionParams{};
	}

	FRootMotionParams Result;

	// AdvanceTime과 같은 이유로 경계 근처 판정을 double로 한다
	double Cursor = StartTime;
	double Remaining = DeltaTime;
	const bool bForward = DeltaTime > 0.0f;

	while (Remaining != 0.0)
	{
		const double Boundary = bForward ? 1.0 : 0.0;
		const double ToBoundary = Boundary - Cursor;

		// 루프가 아니면 AdvanceTime이 이미 끝에서 잘라 준 값이라 경계를 넘을 일이 없다
		if (!bLoop || std::abs(Remaining) <= std::abs(ToBoundary))
		{
			const double End = std::clamp(Cursor + Remaining, 0.0, 1.0);
			Result.Accumulate(ExtractRootMotionRange(static_cast<float>(Cursor * Length), static_cast<float>(End * Length)));
			break;
		}

		// 끝까지 간 이동을 먼저 담고, 반대쪽 끝에서 다시 출발한다. 여러 바퀴면 한 바퀴씩 이 분기를 탄다
		Result.Accumulate(ExtractRootMotionRange(static_cast<float>(Cursor * Length), static_cast<float>(Boundary * Length)));
		Remaining -= ToBoundary;
		Cursor = bForward ? 0.0 : 1.0;
	}

	return Result;
}

FRootMotionParams UAnimSequence::ExtractRootMotionRange(float FromTime, float ToTime) const
{
	FVector FromPos, ToPos;
	FQuat FromRot, ToRot;
	SampleRootTransform(FromTime, FromPos, FromRot);
	SampleRootTransform(ToTime, ToPos, ToRot);

	return FRootMotionParams::MakeDelta(FromPos, FromRot, ToPos, ToRot);
}

void UAnimSequence::SampleRootTransform(float Time, FVector& OutPos, FQuat& OutRot) const
{
	// 키가 빠진 채널은 GetAnimationPose처럼 RefPose 값을 쓰게 해서 포즈와 이동량이 어긋나지 않게 한다
	const FTransform& Ref = CachedMesh->MeshData.Bones[RootBoneIndex].RefPoseTransform;
	FAnimBoneTransform Root;
	Root.Translation = Ref.Location;
	Root.Rotation = FQuat(Ref.Rotation);
	Root.Scale = Ref.Scale;

	// Lock된 포즈가 아니라 원본 키를 읽어야 이동량이 0이 되지 않는다
	const FRawAnimSequenceTrack& Track = DataModel->GetBoneAnimationTracks()[BoneToTrackIndex[RootBoneIndex]].InternalTrack;
	SampleTrack(Track, FindKeyLookup(Time), Root);

	OutPos = Root.Translation;
	OutRot = Root.Rotation;
}
