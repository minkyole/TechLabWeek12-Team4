#include "EnginePCH.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "Engine/Level.h"
#include "Engine/World.h"
#include "Scripting/LuaVM.h"
#include "Sequence/SequenceActor.h"
#include "Serialization/JsonArchive.h"

#include <cmath>
#include <cstdlib>
#include <cstring>

static int32 Checks{};

static void Check(bool Passed, const char* Message) {
	++Checks;
	std::cerr << "Check " << Checks << ": " << Message << '\n';
	if (!Passed) {
		std::cerr << "FAIL: " << Message << '\n';
		std::exit(1);
	}
}

static bool Near(float Actual, float Expected) {
	return std::abs(Actual - Expected) < 0.001f;
}

static void DestroyWorld(UWorld* World) {
	World->EndPlay();
	World->ClearWorld();
	ACameraActor* MainCamera{ World->GetMainCamera() };
	World->SetMainCamera(nullptr);
	delete MainCamera;
	delete World->GetPersistentLevel();
	delete World;
}

static void TestScene(FLuaVM& VM, const FString& Path, int32 ExpectedTracks) {
	std::cerr << "Loading " << Path << '\n';
	std::ifstream File{ Path };
	auto Document{ json::parse(File) };
	auto Actors{ json::array() };
	for (auto Actor : Document["World"]["PersistentLevel"]["Actors"]) {
		if (Actor["Class"] == "ASequenceActor" && (Actor["Name"] == "EndingCarFade" || Actor["Name"] == "EndingDoorFade")) {
			auto Components{ json::array() };
			for (const auto& Component : Actor["Components"]) {
				if (Component["Class"] != "UScriptComponent") {
					Components.push_back(Component);
				}
			}
			Actor["Components"] = Components;
			Actors.push_back(Actor);
		}
	}
	Check(Actors.size() == ExpectedTracks, "scene contains the expected route tracks");
	Document["World"]["PersistentLevel"]["Actors"] = Actors;
	Document["World"]["Properties"] = json::object();
	const FString Text{ Document.dump() };
	TArray<uint8> Bytes{};
	Bytes.SetNum(static_cast<int32>(Text.size()));
	std::memcpy(Bytes.GetData(), Text.data(), Text.size());
	UWorld* World{ UWorld::CreateWorld(EWorldType::Game, false) };
	Check(FJsonArchive::LoadWorldFromBytes(World, Bytes), "production fade actors deserialize successfully");
	ACameraActor* View{ World->SpawnActor<ACameraActor>(FName{ "EndingView" }) };
	APlayerCameraManager* Manager{ World->GetPlayerCameraManager() };
	Manager->SetViewTarget(View);
	UCameraComponent* Camera{ Manager->GetViewTargetCamera() };
	sol::state& State{ *VM.GetState() };
	State["PlainActor"] = static_cast<AActor*>(View);
	Check(VM.ExecuteString("assert(not PlainActor:PlaySequence() and not PlainActor:StopSequence() and not PlainActor:IsSequenceFinished())"), "ordinary actors reject sequence operations");
	Check(Near(ASequenceActor::GetWorldFadeAlpha(World), 0.0f), "scene starts without fading");
	for (AActor* Actor : World->GetPersistentLevel()->GetActors()) {
		ASequenceActor* Sequence{ Cast<ASequenceActor>(Actor) };
		if (!Sequence) {
			continue;
		}
		State["Sequence"] = Actor;
		Check(!Sequence->bAutoPlay, "route fade does not autoplay");
		Check(VM.ExecuteString("assert(Sequence:StopSequence() and Sequence:PlaySequence() and not Sequence:IsSequenceFinished())"), "Lua starts native fade playback");
		Sequence->Tick(1.0f);
		Check(Near(ASequenceActor::GetWorldFadeAlpha(World), 0.5f), "native renderer fade reaches half opacity after one second");
		Check(Manager->GetViewTargetCamera() == Camera, "fade preserves the ending camera");
		Check(VM.ExecuteString("assert(not Sequence:IsSequenceFinished())"), "Lua waits while native fade is in progress");
		Sequence->Tick(1.0f);
		Check(VM.ExecuteString("assert(Sequence:IsSequenceFinished())"), "Lua observes completion after two seconds");
		Sequence->Tick(5.0f);
		Check(Near(ASequenceActor::GetWorldFadeAlpha(World), 1.0f), "finished fade holds full black for credits");
		Check(VM.ExecuteString("assert(Sequence:StopSequence() and not Sequence:IsSequenceFinished())"), "stopping resets native completion");
		Check(Near(ASequenceActor::GetWorldFadeAlpha(World), 0.0f), "stopping clears the fade for replay");
	}
	World->ClearWorld();
	Check(VM.ExecuteString("assert(not Sequence:PlaySequence() and not Sequence:StopSequence() and not Sequence:IsSequenceFinished())"), "unloaded actors reject stale Lua sequence calls");
	DestroyWorld(World);
}

int main() {
	std::cerr << "Initializing ending sequence tests\n";
	FLuaVM VM{};
	Check(VM.Initialize(), "Lua VM initializes sequence bindings");
	TestScene(VM, "Assets/Scenes/Demo/06_Game.scene", 2);
	std::cout << "PASS: " << Checks << " ending sequence binding checks\n";
}
