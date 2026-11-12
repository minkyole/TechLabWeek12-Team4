#pragma once 
#include "UObject/Object.h"
#include "UObject/Class.h"
#include "Animation/AnimTypes.h"

struct FFloatCurveData
{
	FName Name;
	TArray<float> Keys;
};

struct FAnimationCurveData
{
	TArray<FFloatCurveData> FloatCurves;
};

class UAnimDataModel : public UObject
{
	DECLARE_CLASS(UAnimDataModel, UObject)

public:
	UAnimDataModel() = default;

	const TArray<FBoneAnimationTrack>& GetBoneAnimationTracks() const { return BoneAnimationTracks; }
	float GetPlayLength() const { return PlayLength; }
	const FFrameRate& GetFrameRate() const { return FrameRate; }
	int32 GetNumberOfFrames() const { return NumberOfFrames; }
	int32 GetNumberOfKeys() const { return NumberOfKeys; }
	const FAnimationCurveData& GetCurveData() const { return CurveData; }

	int32 FindBoneTrackIndex(const FName& BoneName) const;
	const FBoneAnimationTrack* FindBoneTrackByName(const FName& BoneName) const;
	void SetFrameRate(const FFrameRate& InFrameRate);
	void SetNumberOfFrames(int32 InNumberOfFrames);
	int32 AddBoneTrack(const FName& BoneName);
	FRawAnimSequenceTrack& GetBoneTrackChecked(int32 TrackIndex);
	void SetBoneTrackKeys(int32 TrackIndex, const TArray<FVector>& PosKeys, const TArray<FQuat>& RotKeys, const TArray<FVector>& ScaleKeys);

	void Reset();

private: 
	void RecalculateLength();

	TArray<FBoneAnimationTrack> BoneAnimationTracks;
	float PlayLength = 0.f;
	FFrameRate FrameRate;
	int32 NumberOfFrames = 0;
	int32 NumberOfKeys = 0;
	FAnimationCurveData CurveData;
};