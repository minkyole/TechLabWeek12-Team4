#include "EnginePCH.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Camera/CameraModifier_CameraShake.h"
#include "Camera/PlayerCameraManager.h"
#include "Engine/Level.h"
#include "Engine/World.h"
#include "Scripting/LuaVM.h"

#include <cmath>
#include <cstdlib>

static int32 Checks{};

static void Check(bool Passed, const char* Message) {
	++Checks;
	std::cerr << "Check " << Checks << ": " << Message << '\n';
	if (!Passed) {
		std::cerr << "FAIL: " << Message << '\n';
		std::exit(1);
	}
}

static void CheckLua(sol::protected_function_result& Result) {
	if (!Result.valid()) {
		const sol::error Error{ Result.get<sol::error>() };
		std::cerr << Error.what() << '\n';
	}
	Check(Result.valid(), "Lua assertions pass with native camera bindings");
}

static void RunPhase(const sol::table& Tests, const char* Name) {
	sol::protected_function Function{ Tests.get<sol::protected_function>(Name) };
	sol::protected_function_result Result{ Function() };
	CheckLua(Result);
}

int main() {
	FLuaVM VM{};
	Check(VM.Initialize(), "Lua VM initializes");
	UWorld* World{ UWorld::CreateWorld(EWorldType::Game, false) };
	ACameraActor* Player{ World->SpawnActor<ACameraActor>(FName{ "HorrorCameraPlayer" }) };
	APlayerCameraManager* Manager{ World->GetPlayerCameraManager() };
	Check(Manager->SetViewTarget(Player), "native camera manager targets the player");
	sol::state& State{ *VM.GetState() };
	State["HorrorCameraTestActor"] = static_cast<AActor*>(Player);
	sol::protected_function_result Loaded{ State.safe_script_file("Assets/Scripts/Tests/HorrorCameraBindingsTests.lua", sol::script_pass_on_error) };
	CheckLua(Loaded);
	const sol::table Tests{ Loaded.get<sol::table>() };
	RunPhase(Tests, "Initialize");
	Manager->UpdateCamera(0.04f);
	Check(Manager->GetCameraModifiers().Num() == 1, "heartbeat creates a native camera shake modifier");
	Check(Manager->GetCameraView().GetFieldOfView() - Player->GetCameraComponent()->GetFieldOfView() > 1.0f, "loud heartbeat visibly pulses the rendered FOV");
	Check(Manager->GetCameraView().GetLocation().Z - Player->GetCameraComponent()->GetWorldLocation().Z > 0.01f, "loud heartbeat visibly lifts the rendered camera");
	Check(std::abs(Manager->GetCameraView().GetRotation().Pitch) > 0.5f, "loud heartbeat has a visible camera pitch impulse");
	Manager->UpdateCamera(0.3f);
	Check(Manager->GetCameraModifiers().IsEmpty(), "native heartbeat modifier expires automatically");
	RunPhase(Tests, "RunAndRecover");
	Manager->UpdateCamera(0.25f);
	Check(Manager->GetCameraModifiers().Num() == 1, "recovery creates one native breathing modifier");
	const UCameraModifier_CameraShake* Breathing{ Cast<UCameraModifier_CameraShake>(Manager->GetCameraModifiers()[0]) };
	Check(Breathing && Breathing->GetShakeScale() > 0.0f && dynamic_cast<FWaveOscillatorCameraShakePattern*>(Breathing->GetRootPattern()), "breathing uses a scaled native wave pattern");
	Check(std::abs(Manager->GetCameraView().GetRotation().Pitch) > 0.3f, "a ten metre run creates a visible breathing rotation");
	Check(std::abs(Manager->GetCameraView().GetLocation().Z - Player->GetCameraComponent()->GetWorldLocation().Z) > 0.01f, "a ten metre run creates visible breathing height movement");
	Check(std::abs(Player->GetCameraComponent()->GetWorldRotation().Pitch) < 0.001f, "effects preserve the gameplay camera's observation direction");
	RunPhase(Tests, "Deactivate");
	Check(Manager->GetCameraModifiers().IsEmpty(), "inactive gameplay removes native effects immediately");
	RunPhase(Tests, "Stop");
	World->EndPlay();
	World->ClearWorld();
	ACameraActor* MainCamera{ World->GetMainCamera() };
	World->SetMainCamera(nullptr);
	delete MainCamera;
	delete World->GetPersistentLevel();
	delete World;
	std::cout << "HorrorCameraBindingsTests: " << Checks << " checks passed\n";
	return 0;
}
