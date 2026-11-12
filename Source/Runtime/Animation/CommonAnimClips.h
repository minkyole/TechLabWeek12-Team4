#pragma once

#include <cstring>

// RM FBX는 모든 클립에 루트 이동이 들어 있어서, 클립마다 루트를 어떻게 다룰지 정해 둔다
enum class ECommonClipRootMode : unsigned char
{
	RootMotion, // 루트 이동을 Actor로 넘긴다 (Attack/Hit)
	ForceLock,  // 루트만 잠그고 이동은 입력이 맡는다 (Locomotion/Jump)
};

// 논리 이름(Lua에서 쓰는 이름)과 실제 FBX 클립 키를 한 곳에 고정해 팀원 모두 같은 클립을 보게 한다.
struct FCommonAnimClip
{
	const char* Alias;
	const char* AssetKey; // AssetManager의 FBX 키 + "#Anim/" + AnimStack 이름
	ECommonClipRootMode RootMode;
};

namespace CommonAnimClips
{
	// 클립의 메시가 캐릭터 메시와 같아야 별칭이 걸리므로, 캐릭터 메시도 UAL1_Standard_RM.fbx로 둔다
	inline constexpr FCommonAnimClip Clips[] =
	{
		{ "Idle", "Models/UAL1_Standard_RM.fbx#Anim/Armature|Idle_Loop", ECommonClipRootMode::ForceLock },
		{ "Walk", "Models/UAL1_Standard_RM.fbx#Anim/Armature|Walk_Loop", ECommonClipRootMode::ForceLock },
		{ "Run", "Models/UAL1_Standard_RM.fbx#Anim/Armature|Jog_Fwd_Loop", ECommonClipRootMode::ForceLock },
		{ "Attack", "Models/UAL1_Standard_RM.fbx#Anim/Armature|Sword_Attack", ECommonClipRootMode::RootMotion },
		{ "Hit", "Models/UAL1_Standard_RM.fbx#Anim/Armature|Hit_Chest", ECommonClipRootMode::RootMotion },
		{ "JumpStart", "Models/UAL1_Standard_RM.fbx#Anim/Armature|Jump_Start", ECommonClipRootMode::ForceLock },
		{ "JumpLoop", "Models/UAL1_Standard_RM.fbx#Anim/Armature|Jump_Loop", ECommonClipRootMode::ForceLock },
		{ "JumpLand", "Models/UAL1_Standard_RM.fbx#Anim/Armature|Jump_Land", ECommonClipRootMode::ForceLock },
		{ "Jump", "Models/UAL1_Standard_RM.fbx#Anim/Armature|Jump_Loop", ECommonClipRootMode::ForceLock },
		{ "Death", "Models/UAL1_Standard_RM.fbx#Anim/Armature|Death01", ECommonClipRootMode::ForceLock },
		{ "Roll", "Models/UAL1_Standard_RM.fbx#Anim/Armature|Roll", ECommonClipRootMode::RootMotion },
	};

	// AssetManager가 클립을 등록할 때 부른다. Lua·animsm·뷰어 어느 경로로 재생해도 같은 설정이 되게 한다
	inline const FCommonAnimClip* FindByAssetKey(const char* AssetKey)
	{
		for (const FCommonAnimClip& Clip : Clips)
		{
			if (std::strcmp(Clip.AssetKey, AssetKey) == 0)
			{
				return &Clip;
			}
		}
		return nullptr;
	}
}
