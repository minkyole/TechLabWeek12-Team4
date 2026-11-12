#include "EnginePCH.h"
#include "Camera/CameraActor.h"
#include "Components/PointLightComponent.h"
#include "Components/SpotLightComponent.h"
#include "Engine/Level.h"
#include "Engine/World.h"
#include "Scripting/LuaVM.h"

#include <cstdlib>

static void Check(bool Passed, const char* Message) {
	if (!Passed) {
		std::cerr << "FAIL: " << Message << '\n';
		std::exit(1);
	}
	std::cout << "PASS: " << Message << '\n';
}

int main() {
	FLuaVM VM{};
	Check(VM.Initialize(), "Lua VM initializes");
	UWorld* World{ UWorld::CreateWorld(EWorldType::Game, false) };
	AActor* Fixture{ World->SpawnActor<AActor>(FName{ "CeilingFixture" }) };
	UPointLightComponent* Light{ Cast<UPointLightComponent>(Fixture->AddComponent(UPointLightComponent::StaticClass(), FName{ "CeilingLight" })) };
	Check(Light && Fixture->SetRootComponent(Light), "native fixture owns a point light");
	Light->SetIntensity(0.256f);
	Light->SetWorldLocation(FVector{ 2.0f, 0.0f, 3.5f }, false);
	AActor* Flashlight{ World->SpawnActor<AActor>(FName{ "FlashlightFixture" }) };
	USpotLightComponent* Spot{ Cast<USpotLightComponent>(Flashlight->AddComponent(USpotLightComponent::StaticClass(), FName{ "Flashlight" })) };
	Check(Spot && Flashlight->SetRootComponent(Spot), "native flashlight owns a spot light");
	sol::state& State{ *VM.GetState() };
	State["Fixture"] = Fixture;
	State["Flashlight"] = Flashlight;
	Check(VM.ExecuteString(R"lua(
PlayerLocation = { X = 0, Y = 0, Z = 0 }
package.loaded.GameManager = { IsPlaying = function() return true end, GetPlayerLocation = function() return PlayerLocation end }
SoundPlays = 0
function PlaySound(Path, Loop, Volume)
    assert(Path == "Assets/Sounds/CeilingLightFlicker.wav" and not Loop and Volume > 0 and Volume <= 0.14)
    SoundPlays = SoundPlays + 1
    return SoundPlays
end
function SetSoundPitch(Handle, Pitch) assert(Handle > 0 and Pitch >= 0.94 and Pitch <= 1.06) end
function StopSound(Handle) end
assert(Flashlight:GetPointLight() == nil)
assert(Flashlight:GetSpotLight() ~= nil)
assert(Fixture:GetPointLight():IsVisible())
local Location = Fixture:GetPointLight():GetWorldLocation()
assert(math.abs(Location.X - 2) < 0.001 and math.abs(Location.Z - 3.5) < 0.001)
Flicker = require("CeilingLightFlicker")
Flicker.Config.InitialMinDelay = 0
Flicker.Config.InitialMaxDelay = 0
Flicker.Config.MinPulseCount = 1
Flicker.Config.MaxPulseCount = 1
Flicker.Config.LongDimChance = 0
Flicker.Start(Fixture)
)lua"), "native point light bindings resolve locations and exclude the flashlight");
	VM.UpdateCoroutine(0.01f);
	Check(Light->GetIntensity() >= 0.256f * 0.015f && Light->GetIntensity() <= 0.256f * 0.12f, "engine coroutine makes the native fluorescent light visibly drop out");
	Check(VM.ExecuteString("assert(SoundPlays == 1)"), "native coroutine synchronizes the quiet contact sound");
	VM.UpdateCoroutine(0.2f);
	Check(std::abs(Light->GetIntensity() - 0.256f) < 0.0001f, "engine coroutine restores the reduced map brightness");
	Check(std::abs(Spot->GetIntensity() - 1.0f) < 0.0001f, "flashlight brightness stays unchanged");
	Check(VM.ExecuteString("PlayerLocation.X = 20; Flicker.Stop(Fixture); Flicker.Start(Fixture)"), "native player moves away from the fixture");
	VM.UpdateCoroutine(0.01f);
	Check(std::abs(Light->GetIntensity() - 0.256f) < 0.0001f, "a native light outside the nearby radius remains steady");
	Check(VM.ExecuteString("assert(SoundPlays == 1); PlayerLocation.X = 0"), "distant lights do not consume flicker audio events");
	VM.UpdateCoroutine(0.3f);
	Check(Light->GetIntensity() <= 0.256f * 0.12f, "the native light becomes eligible when the player returns");
	Check(VM.ExecuteString("PlayerLocation.X = 20; Flicker.Tick(Fixture)"), "the player leaves during an active native flicker");
	Check(std::abs(Light->GetIntensity() - 0.256f) < 0.0001f, "moving away immediately restores the native light");
	Check(VM.ExecuteString("Flicker.Stop(Fixture)"), "flicker stops before world cleanup");
	World->EndPlay();
	World->ClearWorld();
	VM.UpdateCoroutine(60.0f);
	VM.ResetCoroutines();
	ACameraActor* MainCamera{ World->GetMainCamera() };
	World->SetMainCamera(nullptr);
	delete MainCamera;
	delete World->GetPersistentLevel();
	delete World;
	Check(true, "pending flicker coroutine safely exits after the native light is destroyed");
	return 0;
}
