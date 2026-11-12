#include "EnginePCH.h"
#include "TestAnimInstance.h"

#include "Animation/AnimSequence.h"
#include "Animation/BlendSpace.h"
#include "Render/Mesh.h"
#include "UObject/UObjectGlobals.h"

#include <algorithm>
#include <cmath>

UTestAnimInstance::~UTestAnimInstance()
{
	ReleaseTestAssets();
}

UAnimSequence* UTestAnimInstance::CreateRotationClip(
	int32 BoneIndex,
	const FVector& LocalAxis,
	float DurationSeconds,
	bool bStationary)
{
	const USkeletalMesh* Mesh = GetSkeletalMesh();

	if (!Mesh || !Mesh->MeshData.Bones.IsValidIndex(BoneIndex))
	{
		return nullptr;
	}

	UAnimSequence* Clip = NewObject<UAnimSequence>(this);
	Clip->SetSkeletalMesh(Mesh);

	UAnimDataModel* Model = Clip->GetDataModel();

	constexpr int32 FPS = 30;
	const int32 NumFrames = std::max(
	    1,
	    static_cast<int32>(std::round(DurationSeconds * FPS)));

	Model->SetFrameRate(FFrameRate(FPS, 1));
	Model->SetNumberOfFrames(NumFrames);

	const FSkeletalBone& Bone = Mesh->MeshData.Bones[BoneIndex];

	const int32 TrackIndex =
	    Model->AddBoneTrack(FName(Bone.Name));

	const FQuat RefRotation =
	    FQuat(Bone.RefPoseTransform.Rotation).Normalized();

	TArray<FQuat> RotationKeys;

	const int32 NumKeys = Model->GetNumberOfKeys();
	RotationKeys.Reserve(NumKeys);

	constexpr float TwoPi = 6.28318530718f;

	for (int32 Key = 0; Key < NumKeys; ++Key)
	{
		const float Phase =
		    static_cast<float>(Key) / static_cast<float>(NumKeys - 1);

		const float Angle = bStationary ? 0.0f : TwoPi * Phase;

		const FQuat DeltaRotation =
		    FQuat::MakeFromAxisAngle(LocalAxis, Angle);

		RotationKeys.Add(
		    (RefRotation * DeltaRotation).Normalized());
	}

	// 위치와 스케일은 키가 없으므로 기준 포즈를 유지한다.
	Model->SetBoneTrackKeys(
	    TrackIndex,
	    {},
	    RotationKeys,
	    {});

	return Clip;
}
void UTestAnimInstance::NativeInitializeAnimation()
{
	Super::NativeInitializeAnimation();

	const USkeletalMesh* Mesh = GetSkeletalMesh();

	if (!Mesh)
	{
		return;
	}

	// 기존 샘플링 테스트와 동일한 본.
	// 실제 메시에서 움직임이 잘 보이는 본 번호로 바꿔도 된다.
	constexpr int32 TestBoneIndex = 1;

	if (!Mesh->MeshData.Bones.IsValidIndex(TestBoneIndex))
	{
		return;
	}

	IdleClip = CreateRotationClip(
	    TestBoneIndex,
	    FVector(0.0f, 0.0f, 1.0f),
	    4.0f,
	    true);

	SlowClip = CreateRotationClip(
	    TestBoneIndex,
	    FVector(0.0f, 0.0f, 1.0f),
	    4.0f,
	    false);

	FastClip = CreateRotationClip(
	    TestBoneIndex,
	    FVector(0.0f, 1.0f, 0.0f),
	    1.0f,
	    false);

	if (!IdleClip || !SlowClip || !FastClip)
	{
		ReleaseTestAssets();
		return;
	}

	TestBlendSpace = NewObject<UBlendSpace1D>(this);
	TestBlendSpace->SetSkeletalMesh(Mesh);

	FBlendParameter SpeedAxis;
	SpeedAxis.Name = FName("Speed");
	SpeedAxis.Min = 0.0f;
	SpeedAxis.Max = 600.0f;

	if (!TestBlendSpace->SetBlendParameter(0, SpeedAxis))
	{
		ReleaseTestAssets();
		return;
	}

	FBlendSample IdleSample;
	IdleSample.Animation = IdleClip;
	IdleSample.SampleValue = FVector(0.0f, 0.0f, 0.0f);

	FBlendSample SlowSample;
	SlowSample.Animation = SlowClip;
	SlowSample.SampleValue = FVector(300.0f, 0.0f, 0.0f);

	FBlendSample FastSample;
	FastSample.Animation = FastClip;
	FastSample.SampleValue = FVector(600.0f, 0.0f, 0.0f);

	const bool bConfigured =
	    TestBlendSpace->AddSample(IdleSample) && TestBlendSpace->AddSample(SlowSample) && TestBlendSpace->AddSample(FastSample);

	if (!bConfigured)
	{
		ReleaseTestAssets();
		return;
	}

	BlendSpacePlayer.BlendSpace = TestBlendSpace;
	BlendSpacePlayer.X = 0.0f;
	BlendSpacePlayer.PlayRate = 1.0f;
	BlendSpacePlayer.bPlaying = true;
	BlendSpacePlayer.bLoop = true;
	BlendSpacePlayer.ResetPlayback();

	RootNode = &BlendSpacePlayer;

	// 시간 진행 없이 최초 가중치를 계산한다.
	// Initialize 직후 Evaluate만 호출해도 샘플이 준비되어 있게 한다.
	FAnimationUpdateContext Context;
	Context.AnimInstance = this;
	Context.DeltaSeconds = 0.0f;

	BlendSpacePlayer.Update(Context);
}

void UTestAnimInstance::SetSpeed(float InSpeed)
{
	if (!std::isfinite(InSpeed))
	{
		return;
	}

	BlendSpacePlayer.X = std::clamp(InSpeed, 0.0f, 600.0f);
}

void UTestAnimInstance::NativeUnInitializeAnimation()
{
	ReleaseTestAssets();

	Super::NativeUnInitializeAnimation();
}

void UTestAnimInstance::ReleaseTestAssets()
{
	// 먼저 삭제할 에셋을 참조하는 노드를 끊는다.
	RootNode = nullptr;
	BlendSpacePlayer.BlendSpace = nullptr;
	BlendSpacePlayer.ResetPlayback();

	// 이전 샘플 가중치도 비운다.
	FAnimationUpdateContext Context;
	Context.AnimInstance = this;
	Context.DeltaSeconds = 0.0f;
	BlendSpacePlayer.Update(Context);

	delete TestBlendSpace;
	TestBlendSpace = nullptr;

	const auto DeleteClip = [](UAnimSequence*& Clip)
	{
		// DataModel 정리를 소유 시퀀스에 맡기는 단일 해제
		delete Clip;
		Clip = nullptr;
	};

	DeleteClip(IdleClip);
	DeleteClip(SlowClip);
	DeleteClip(FastClip);
}