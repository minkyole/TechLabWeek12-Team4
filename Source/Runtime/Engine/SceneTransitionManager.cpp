#include "EnginePCH.h"
#include "Engine/SceneTransitionManager.h"

#include "Engine/World.h"
#include "Serialization/JsonArchive.h"
#include "Core/Misc/FileHelper.h"
#include <system_error>


void FSceneTransitionManager::RequestLoadScene(UWorld* TargetWorld, const FString& ScenePath)
{
	if (!TargetWorld || TargetWorld->GetWorldType() == EWorldType::Editor || ScenePath.empty()
		|| ScenePath.find('\0') != FString::npos)
	{
		HTR_LOG(Warning, "[Scene] Ignored invalid scene transition request");
		return;
	}
	try
	{
		const std::filesystem::path NativePath = FFileHelper::ToNativePath(ScenePath).lexically_normal();
		const FString NormalizedPath = FFileHelper::ToUtf8Path(NativePath);

		// 변환 완료 이후의 대기 요청 교체
		PendingTransition.ScenePath = NormalizedPath;
		PendingTransition.TargetWorld = TargetWorld;
	}
	catch (const std::system_error&)
	{
		HTR_LOG(Warning, "[Scene] Ignored scene transition request: path conversion failed");
	}
}

void FSceneTransitionManager::RequestRestartScene(UWorld* TargetWorld)
{
	if (!TargetWorld)
	{
		HTR_LOG(Warning, "[Scene] Cannot restart a null world");
		return;
	}

	RequestLoadScene(TargetWorld, TargetWorld->GetScenePath());
}

bool FSceneTransitionManager::ProcessPendingTransition()
{
	if (!PendingTransition.IsPending())
		return false;

	// EndPlay에서 발생할 수 있는 요청이 현재 요청에 덮이지 않게 먼저 비운다.
	FPendingTransition Request = PendingTransition;
	PendingTransition.Reset();

	UWorld* World = Request.TargetWorld;
	if (!World || World->GetWorldType() == EWorldType::Editor)
		return false;

	const std::filesystem::path NativePath = FFileHelper::ToNativePath(Request.ScenePath);
	std::error_code ErrorCode;
	if (!std::filesystem::is_regular_file(NativePath, ErrorCode) || ErrorCode)
	{
		HTR_LOG(Error, "[Scene] Scene file does not exist: {}", Request.ScenePath);
		return false;
	}

	// 실행 중인 Lua 함수와 충돌 처리가 모두 끝난 프레임 경계에서만 호출된다.
	World->EndPlay();
	if (!FJsonArchive::LoadWorld(World, Request.ScenePath))
	{
		HTR_LOG(Error, "[Scene] Failed to load scene: {}", Request.ScenePath);
		return false;
	}

	World->SetScenePath(Request.ScenePath);
	World->BeginPlay();
	HTR_LOG(Info, "[Scene] Loaded scene: {}", Request.ScenePath);
	return true;
}

void FSceneTransitionManager::CancelForWorld(const UWorld* World)
{
	if (PendingTransition.TargetWorld == World)
		PendingTransition.Reset();
}
