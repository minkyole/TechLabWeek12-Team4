#include "EnginePCH.h"
#include "LuaAnimInstance.h"
#include "Components/SkeletalMeshComponent.h"

#include "Animation/AnimSequence.h"
#include "Animation/BlendSpace.h"
#include "Animation/CommonAnimClips.h"
#include "Asset/AssetManager.h"

#include "Engine/Engine.h"
#include "Scripting/LuaVM.h"

#include <cmath>
#include <utility>

ULuaAnimInstance::~ULuaAnimInstance()
{
	ReleaseAnimationScript();
}

void ULuaAnimInstance::NativeInitializeAnimation()
{
	// 이전 그래프와 임시 에셋 정리 이후의 재초기화
	ReleaseAnimationScript();
	AnimationScriptPath = GetSkeletalMeshComponent()->GetAnimationScriptPath();
	LastError.clear();

	const USkeletalMesh* Mesh = GetSkeletalMesh();
	if (!Mesh)
	{
		return;
	}

	const int32 RootBoneIndex = 1;
	UAnimSequence* IdleClip = CreateRootTestClip(RootBoneIndex, 1.0f, FVector(0, 0, 0));
	UAnimSequence* WalkClip = CreateRootTestClip(RootBoneIndex, 1.0f, FVector(0, 0, 0.3f));
	UAnimSequence* RunClip = CreateRootTestClip(RootBoneIndex, 0.5f, FVector(0, 0, 0.4f));
	UAnimSequence* JumpClip = CreateRootTestClip(RootBoneIndex, 0.1f, FVector(0, 0, 0.1f));
	UAnimSequence* AttackClip = CreateRootTestClip(RootBoneIndex, 0.6f, FVector(3.0f, 0, 0));
	UAnimSequence* HitClip = CreateRootTestClip(RootBoneIndex, 0.4f, FVector(-1.5f, 0, 0));
	UBlendSpace1D* TestBlendSpace = NewObject<UBlendSpace1D>(this);
	TemporaryAssets.Add(TestBlendSpace);

	// 부분 생성 실패를 포함한 임시 에셋 회수
	if (!IdleClip || !WalkClip || !RunClip || !JumpClip || !AttackClip || !HitClip || !TestBlendSpace)
	{
		Fail("Failed to create test animation assets.");
		ReleaseAnimationScript();
		return;
	}

	// 동일 메시의 실제 FBX 클립 별칭 우선 등록
	for (const FCommonAnimClip& Entry : CommonAnimClips::Clips)
	{
		const UAnimSequence* FbxClip = UAssetManager::GetAssetByPath<UAnimSequence>(Entry.AssetKey);
		if (FbxClip && FbxClip->GetSkeletalMesh() == Mesh)
		{
			if (!RegisterAssetAlias(Entry.Alias, Entry.AssetKey))
			{
				ReleaseAnimationScript();
				return;
			}
		}
	}

	// 실제 클립 별칭이 없는 이름의 인스턴스별 대체 에셋 등록
	if (!RegisterTemporaryAsset("Idle", IdleClip) ||
	    !RegisterTemporaryAsset("Walk", WalkClip) ||
	    !RegisterTemporaryAsset("Run", RunClip) ||
	    !RegisterTemporaryAsset("Jump", JumpClip) ||
	    !RegisterTemporaryAsset("Attack", AttackClip) ||
	    !RegisterTemporaryAsset("Hit", HitClip))
	{
		ReleaseAnimationScript();
		return;
	}

	// 기존 별칭 해석을 통한 실제 클립 또는 대체 클립 선택
	const auto PickClip = [this](const char* Alias)
	{
		return UAssetManager::GetAssetByPath<UAnimSequence>(ResolveAssetKey(Alias));
	};

	TestBlendSpace->SetSkeletalMesh(Mesh);
	FBlendParameter SpeedAxis;
	SpeedAxis.Name = FName("Speed");
	SpeedAxis.Min = 0.0f;
	SpeedAxis.Max = 10.0f;

	const auto AddSample = [TestBlendSpace](UAnimSequence* Clip, float Speed)
	{
		FBlendSample Sample;
		Sample.Animation = Clip;
		Sample.SampleValue = FVector(Speed, 0.0f, 0.0f);
		return TestBlendSpace->AddSample(Sample);
	};

	// 유효한 샘플 구성을 마친 BlendSpace의 공개
	if (!TestBlendSpace->SetBlendParameter(0, SpeedAxis) ||
	    !AddSample(PickClip("Idle"), 0.0f) ||
	    !AddSample(PickClip("Walk"), 5.0f) ||
	    !AddSample(PickClip("Run"), 10.0f))
	{
		Fail("Failed to configure test BlendSpace.");
		ReleaseAnimationScript();
		return;
	}

	if (!RegisterTemporaryAsset("Locomotion", TestBlendSpace) || !BeginBuild())
	{
		ReleaseAnimationScript();
		return;
	}

	if (!LoadAnimationScript(AnimationScriptPath))
	{
		ReleaseAnimationScript();
		return;
	}

	// Lua 결과 객체의 소멸 이후로 미루는 실패 정리
	const bool bInitialized = [this]()
	{
		sol::protected_function_result Result = Script.Initialize(this);
		if (!Result.valid())
		{
			const sol::error Error = Result;
			HTR_LOG(Error, "[LuaAnim] InitializeAnimation failed: {}", Error.what());
			return Fail(Error.what());
		}
		return true;
	}();

	if (!bInitialized)
	{
		ReleaseAnimationScript();
		return;
	}

	if (!Commit())
	{
		HTR_LOG(Error, "[LuaAnim] Commit failed: {}", GetLastError());
		ReleaseAnimationScript();
		return;
	}
}

void ULuaAnimInstance::NativeUnInitializeAnimation()
{
	ReleaseAnimationScript();
}


void ULuaAnimInstance::NativeUpdateAnimation(float DeltaSeconds)
{
	if (!bGraphReady || !Script.Update.valid())
		return;

	bool bFailed = false;

	{
		sol::protected_function_result Result =
		    Script.Update(this, DeltaSeconds);

		if (!Result.valid())
		{
			const sol::error Error = Result;

			HTR_LOG(
			    Error,
			    "[LuaAnim] UpdateAnimation failed: {}",
			    Error.what());

			Fail(Error.what());
			bFailed = true;
		}
	}

	if (bFailed)
		Script.Update = {};


}

UAnimSequence* ULuaAnimInstance::CreateRootTestClip(
    int32 RootBoneIndex, float DurationSeconds, const FVector& Offset)
{
	const USkeletalMesh* Mesh = GetSkeletalMesh();
	if (!Mesh || !Mesh->MeshData.Bones.IsValidIndex(RootBoneIndex))
		return nullptr;

	UAnimSequence* Clip = NewObject<UAnimSequence>(this);
	// 이후 초기화 실패에도 회수할 수 있는 생성 직후의 소유 기록
	TemporaryAssets.Add(Clip);
	Clip->SetSkeletalMesh(Mesh);
	UAnimDataModel* Model = Clip->GetDataModel();
	constexpr int32 FPS = 30;
	Model->SetFrameRate(FFrameRate(FPS, 1));
	Model->SetNumberOfFrames(std::max(
	    1, static_cast<int32>(std::round(DurationSeconds * FPS))));

	const FSkeletalBone& RootBone = Mesh->MeshData.Bones[RootBoneIndex];
	const int32 TrackIndex = Model->AddBoneTrack(FName(RootBone.Name));
	TArray<FVector> Positions;
	const int32 NumKeys = Model->GetNumberOfKeys();
	Positions.Reserve(NumKeys);
	constexpr float TwoPi = 6.28318530718f;
	for (int32 Key = 0; Key < NumKeys; ++Key)
	{
		const float Phase = static_cast<float>(Key) / (NumKeys - 1);
		// 시작/끝은 기준 위치, 중간은 Offset. 반복 경계에서도 연속적이다.
		const float Weight = (Key == 0 || Key == NumKeys - 1)
		                         ? 0.0f
		                         : 0.5f - 0.5f * std::cos(TwoPi * Phase);
		Positions.Add(RootBone.RefPoseTransform.Location + Offset * Weight);
	}
	// 회전/스케일과 나머지 본의 로컬 변환은 기준 포즈를 유지한다.
	Model->SetBoneTrackKeys(TrackIndex, Positions, {}, {});
	return Clip;
}

bool ULuaAnimInstance::LoadAnimationScript(const FString& Path)
{
 FString Error;
 return Script.Load(Path,true,false,Error) || Fail(Error);
}

bool ULuaAnimInstance::RegisterTemporaryAsset(const FString& Alias, UAnimationAsset* Asset)
{
	// 이미 연결된 실제 FBX 클립 별칭의 유지
	if (ResolveAssetKey(Alias) != Alias)
	{
		return true;
	}

	// 캐릭터와 에디터·PIE 인스턴스 사이의 전역 키 충돌 방지
	const FString Key = "Runtime/LuaAnimation/" + std::to_string(GetUUID()) + "/" + Alias;
	UAssetManager::Get().RegisterAsset(Key, Asset);
	return RegisterAssetAlias(Alias, Key);
}

void ULuaAnimInstance::ReleaseAnimationScript()
{
	// 활성 Notify와 그래프 콜백이 참조하는 에셋의 선행 유지
	ResetGraph();
	Script.Reset();
	AssetAliases.Reset();

	// BlendSpace를 먼저 해제하는 생성 역순의 소유 에셋 정리
	for (int32 Index = TemporaryAssets.Num() - 1; Index >= 0; --Index)
	{
		UAnimationAsset* Asset = TemporaryAssets[Index];
		// 전역 종료에서 먼저 삭제된 자식의 중복 해제 방지
		if (GUObjectArray.Find(Asset) == INDEX_NONE)
		{
			continue;
		}

		if (!Asset->GetPath().empty())
		{
			UAssetManager::Get().UnregisterAsset(Asset->GetPath(), Asset);
		}
		delete Asset;
	}
	TemporaryAssets.Reset();
}
