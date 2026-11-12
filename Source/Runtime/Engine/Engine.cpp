#include "EnginePCH.h"
#include "Engine.h"

#include "Components/ScriptComponent.h"
#include "Launch/LaunchEngineLoop.h"
#include "Core/Window.h"
#include "Input/MouseCapture.h"

UEngine* GEngine = nullptr;
UEngine::~UEngine() = default;

bool UEngine::Init()
{
	UScriptComponent::StaticClass();

	AssetManager = NewObject<UAssetManager>();
	/*World = FObjectFactory::ConstructObject<UWorld>();

	if (!World || !World->Init()) return false;*/

	SceneTransitionManager = MakeUnique<FSceneTransitionManager>();
	AudioSystem = MakeUnique<FAudioSystem>();

	if (!AudioSystem->Initialize())
	{
		HTR_LOG(Warning, "[Startup] Audio is unavailable; sounds will be skipped");
	}

	// Lua 파일을 실행한다.
	LuaVM = MakeUnique<FLuaVM>();

	// 초기 연결 확인용입니다.
	if (!LuaVM->Initialize())
	{
		HTR_LOG(Error, "[Startup] Lua VM initialization failed");
		return false;
	}

	return true;
}

FWorldContext& UEngine::CreateNewWorldContext(EWorldType WorldType)
{
	FWorldContext* NewWorldContext = new FWorldContext();
	WorldList.Add(NewWorldContext);
	NewWorldContext->WorldType = WorldType;
	NewWorldContext->ContextHandle = FName();

	return *NewWorldContext;
}

FWorldContext* UEngine::GetWorldContextFromWorld(const UWorld* InWorld)
{
	for (FWorldContext& WorldContext : WorldList)
	{
		if (WorldContext.World() == InWorld)
		{
			return &WorldContext;
		}
	}
	return nullptr;
}

void UEngine::DestroyWorldContext(UWorld* InWorld)
{
	for (int32 idx = 0; idx < WorldList.Num(); ++idx)
	{
		if (WorldList[idx].World() == InWorld)
		{
			//WorldContextDestroyedEvent.Broadcast(WorldList[idx]);

			WorldList[idx].SetCurrentWorld(NULL);
			WorldList.RemoveAt(idx);
			break;
		}
	}
}

void FWorldContext::SetCurrentWorld(UWorld* World)
{
	ThisCurrentWorld = World;
}

FGameViewportInfo UEngine::GetGameViewportInfo(const UWorld* InWorld) const
{
    FGameViewportInfo Info;
    if (!InWorld || InWorld->GetWorldType() == EWorldType::Editor)
        return Info;

    FWindow* Window = GetEngineLoop().GetMainWindow();
    if (!Window)
        return Info;

    Info.Size = FVector2(static_cast<float>(Window->GetWidth()), static_cast<float>(Window->GetHeight()));
    POINT Mouse{};
    if (::GetCursorPos(&Mouse) && ::ScreenToClient(Window->GetHandle(), &Mouse))
    {
        Info.MousePosition = FVector2(static_cast<float>(Mouse.x), static_cast<float>(Mouse.y));
        Info.bAcceptsMouseInput = ::GetForegroundWindow() == Window->GetHandle()
            && Mouse.x >= 0 && Mouse.y >= 0 && Mouse.x < Info.Size.X && Mouse.y < Info.Size.Y;
    }
    return Info;
}

void UEngine::RequestQuitGame(const UWorld* InWorld)
{
    if (InWorld && InWorld->GetWorldType() == EWorldType::Game)
        GetEngineLoop().RequestExit();
}

bool UEngine::SetGameMouseCapture(const UWorld* InWorld, bool Enabled) {
#if defined(EXECUTABLE)
	FWindow* Window{ EngineLoop ? EngineLoop->GetMainWindow() : nullptr };
	if (!Window || !InWorld || InWorld->GetWorldType() != EWorldType::Game) {
		return false;
	}
	RECT Bounds{};
	::GetClientRect(Window->GetHandle(), &Bounds);
	::MapWindowPoints(Window->GetHandle(), nullptr, reinterpret_cast<POINT*>(&Bounds), 2);
	return FMouseCapture::Update(Window->GetHandle(), Bounds, Enabled);
#else
	return false;
#endif
}

void UEngine::SetCursorMode(ECursorMode Mode) {
#if defined(EXECUTABLE)
	mCursorMode = Mode;
	for (FWorldContext& Context : WorldList) {
		UWorld* World{ Context.World() };
		if (World && World->GetWorldType() == EWorldType::Game) {
			SetGameMouseCapture(World, Mode == ECursorMode::Locked);
		}
	}
	if (Mode == ECursorMode::Free) {
		FMouseCapture::Release();
	}
#endif
}

ECursorMode UEngine::GetCursorMode() const {
#if defined(EXECUTABLE)
	return mCursorMode;
#else
	return ECursorMode::Free;
#endif
}
