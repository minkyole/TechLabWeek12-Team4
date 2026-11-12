#pragma once
#include "AnimationAsset.h"
#include "AnimDataModel.h"
#include "AnimationPose.h" 
#include "AnimNotifyEvent.h"

class UAnimSequenceBase : public UAnimationAsset
{
	DECLARE_CLASS(UAnimSequenceBase, UAnimationAsset)

public: 
	UAnimSequenceBase();
	~UAnimSequenceBase() override;

	void RemoveNotifyAt(int32 Index);
	void ClearNotifies();
	void SetNotifyObject(int32 Index, UAnimNotify* NewNotify);
	void OnNotifyObjectDestroyed(const UAnimNotify* DestroyedNotify);

	UAnimDataModel* GetDataModel() const { return DataModel; }
	float GetPlayLength() const override { return DataModel->GetPlayLength(); }


	TArray<FAnimNotifyEvent> Notifies;
protected:
	UAnimDataModel* DataModel = nullptr;
};

class UAnimSequence : public UAnimSequenceBase
{
	DECLARE_CLASS(UAnimSequence, UAnimSequenceBase)

public:
	UAnimSequence() = default;
	~UAnimSequence() override = default;

	// bRootLock은 "이 재생이 루트 이동을 Actor로 넘기는가"다. 에셋 플래그까지 켜져 있어야 실제로 잠근다
	void GetAnimationPose(float Time, FAnimationPose& OutPose, bool bRootLock = false) const;

	void BuildFromTracks(const FFrameRate& FrameRate, int32 NumFrames, const TArray<FBoneAnimationTrack>& Tracks);

	// In-place 클립(Idle/Walk/Run)은 꺼 두고, 내딛는 클립(Attack/Hit)만 켠다
	void SetEnableRootMotion(bool bEnable) { bEnableRootMotion = bEnable; }
	bool IsRootMotionEnabled() const { return bEnableRootMotion; }

	// 루트가 앞으로 가는 클립을 In-place로 쓰고 싶을 때 켠다. 이동은 넘기지 않고 잠그기만 한다
	void SetForceRootLock(bool bForce) { bForceRootLock = bForce; }
	bool IsForceRootLock() const { return bForceRootLock; }

	// 정규화 시간 StartTime에서 Delta만큼 지나간 동안의 루트 이동량. 인자는 FDeltaTimeRecord와 같은 단위다.
	FRootMotionParams ExtractRootMotion(const USkeletalMesh& Mesh, float StartTime, float DeltaTime, bool bLoop) const;

private:
	FAnimKeyLookup FindKeyLookup(float Time) const;

	// 루프 경계를 넘지 않는 한 구간(초 단위). 역방향이면 FromTime > ToTime이다.
	FRootMotionParams ExtractRootMotionRange(float FromTime, float ToTime) const;

	void SampleRootTransform(float Time, FVector& OutPos, FQuat& OutRot) const;

	static void SampleTrack(const FRawAnimSequenceTrack& Track, const FAnimKeyLookup& Lookup, FAnimBoneTransform& OutTransform);

	void RebuildBoneTrackMap(const USkeletalMesh& Mesh) const;

	mutable const USkeletalMesh* CachedMesh = nullptr;
	mutable TArray<int32> BoneToTrackIndex;
	mutable int32 RootBoneIndex = -1;

	bool bEnableRootMotion = false;
	bool bForceRootLock = false;
};