#include "EnginePCH.h"
#include "AnimDataModel.h"

// 이름으로 트렉 인덱스를 찾는다
int32 UAnimDataModel::FindBoneTrackIndex(const FName& BoneName) const
{
	for (int32 i = 0; i < BoneAnimationTracks.Num(); ++i)
	{
		if (BoneAnimationTracks[i].Name == BoneName)
		{
			return i;
		}
	}
	return -1;
}

// 트렉 포인터를 반환한다
const FBoneAnimationTrack* UAnimDataModel::FindBoneTrackByName(const FName& BoneName) const
{
	int32 Index = FindBoneTrackIndex(BoneName);
	if (Index == -1)
	{
		return nullptr;
	}
		
	return &BoneAnimationTracks[Index];
}

void UAnimDataModel::SetFrameRate(const FFrameRate& InFrameRate)
{
	FrameRate = InFrameRate;
	RecalculateLength();
}

void UAnimDataModel::SetNumberOfFrames(int32 InNumberOfFrames)
{
	NumberOfFrames = InNumberOfFrames;
	RecalculateLength();
}

int32 UAnimDataModel::AddBoneTrack(const FName& BoneName)
{
	int32 Index = FindBoneTrackIndex(BoneName);
	if (Index != -1)
	{
		return Index;
	}

	FBoneAnimationTrack NewTrack{};
	NewTrack.Name = BoneName;
	BoneAnimationTracks.Add(NewTrack);
	return BoneAnimationTracks.Num() - 1;
}

FRawAnimSequenceTrack& UAnimDataModel::GetBoneTrackChecked(int32 TrackIndex)
{
	return BoneAnimationTracks[TrackIndex].InternalTrack;
}

void UAnimDataModel::SetBoneTrackKeys(int32 TrackIndex, const TArray<FVector>& PosKeys, const TArray<FQuat>& RotKeys, const TArray<FVector>& ScaleKeys)
{
	int32 PosNum = PosKeys.Num();
	int32 RotNum = RotKeys.Num();
	int32 ScaleNum = ScaleKeys.Num();
	assert(PosNum == 0 || PosNum == 1 || PosNum == NumberOfKeys);
	assert(RotNum == 0 || RotNum == 1 || RotNum == NumberOfKeys);
	assert(ScaleNum == 0 || ScaleNum == 1 || ScaleNum == NumberOfKeys);

	assert(TrackIndex >= 0 && TrackIndex < BoneAnimationTracks.Num());
	FRawAnimSequenceTrack& Track = GetBoneTrackChecked(TrackIndex);
	Track.PosKeys = PosKeys;
	Track.RotKeys = RotKeys;
	Track.ScaleKeys = ScaleKeys;
}

void UAnimDataModel::Reset()
{
	BoneAnimationTracks.Empty();
	CurveData.FloatCurves.Empty();
	PlayLength = 0.f;
	NumberOfFrames = 0;
	NumberOfKeys = 0;
	FrameRate.Numerator = 30;
	FrameRate.Denominator = 1;
}


void UAnimDataModel::RecalculateLength()
{
	PlayLength = NumberOfFrames * FrameRate.AsInterval();
	NumberOfKeys = NumberOfFrames + 1;
}

