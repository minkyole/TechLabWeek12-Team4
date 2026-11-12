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
	if (!Passed) {
		std::cerr << "FAIL: " << Message << '\n';
		std::exit(1);
	}
}

static bool Near(float First, float Last, float Tolerance = 0.0001f) {
	return std::abs(First - Last) < Tolerance;
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

static void TestSpline() {
	FSequenceData Data{};
	Data.Rows.clear();
	Data.EndFrameSecond = 1;
	FCameraTimelineTrack Track{};
	const FVector Positions[]{ { 0.0f, 0.0f, 0.0f }, { 1.0f, 2.0f, 0.0f }, { 3.0f, 3.0f, 0.0f }, { 4.0f, 0.0f, 0.0f } };
	for (int32 Index{ 0 }; Index < 4; ++Index) {
		FCameraSection Section{};
		Section.KeyCount = 1;
		Section.KeyFrames[0] = Index * 10;
		Section.KeyCam[0].Location = Positions[Index];
		Track.CamSections.push_back(Section);
	}
	Data.Rows.push_back(FTimeline{ "Camera", FLinearColor{}, Track });
	FSequencePlayer Player{};
	Player.SetSequenceData(Data);
	CameraKey Key{};
	Player.SetPlaybackFrame(15.0f);
	Check(Player.GetCurrentCameraKey(Key) && Near(Key.Location.X, 2.0f) && Near(Key.Location.Y, 2.8125f), "four separate keys follow the Catmull-Rom curve between sections");
	for (int32 Index{ 0 }; Index < 4; ++Index) {
		Player.SetPlaybackFrame(static_cast<float>(Index * 10));
		Check(Player.GetCurrentCameraKey(Key) && Near(Key.Location.X, Positions[Index].X) && Near(Key.Location.Y, Positions[Index].Y), "spline passes through every key at its authored time");
	}
	std::reverse(std::get<FCameraTimelineTrack>(Player.GetSequenceData().Rows.front().Rows).CamSections.begin(), std::get<FCameraTimelineTrack>(Player.GetSequenceData().Rows.front().Rows).CamSections.end());
	Player.SetPlaybackFrame(15.0f);
	Check(Player.GetCurrentCameraKey(Key) && Near(Key.Location.Y, 2.8125f), "dragged and unsorted keys retain chronological interpolation");
	Track.CamSections[2].mCutAtStart = true;
	Data.Rows.front().Rows = Track;
	Player.SetSequenceData(Data);
	Player.SetPlaybackFrame(19.99f);
	Check(Player.GetCurrentCameraKey(Key) && Near(Key.Location.X, 1.0f), "camera cuts hold the preceding shot instead of interpolating");
	Player.SetPlaybackFrame(20.0f);
	Check(Player.GetCurrentCameraKey(Key) && Near(Key.Location.X, 3.0f), "camera cuts switch exactly at the key");
	Track.CamSections.resize(2);
	Track.CamSections[0].KeyCam[0].Rotation.Yaw = 179.0f;
	Track.CamSections[1].KeyCam[0].Rotation.Yaw = -179.0f;
	Data.Rows.front().Rows = Track;
	Player.SetSequenceData(Data);
	Player.SetPlaybackFrame(5.0f);
	Check(Player.GetCurrentCameraKey(Key) && Near(Key.Location.X, 0.5f) && Near(Key.Rotation.Yaw, 180.0f), "two keys remain linear and rotation takes the shortest angle");
	Track.CamSections.resize(1);
	Data.Rows.front().Rows = Track;
	Player.SetSequenceData(Data);
	Player.SetPlaybackFrame(30.0f);
	Check(Player.GetCurrentCameraKey(Key) && Near(Key.Location.X, 0.0f), "a single key holds its camera pose");
	const float Frame{ Player.GetPlaybackFrame() };
	Check(!Player.EvaluateAtTime(std::numeric_limits<double>::quiet_NaN()) && Player.GetPlaybackFrame() == Frame, "invalid sampling time leaves playback unchanged");
}

static void TestScene(FLuaVM& VM, const FString& Path, int32 Expected) {
	std::ifstream Input{ Path };
	auto Document{ json::parse(Input) };
	std::ifstream ReferenceInput{ "Build/CameraSequenceReference.json" };
	const auto References{ json::parse(ReferenceInput) };
	auto Actors{ json::array() };
	for (auto Actor : Document["World"]["PersistentLevel"]["Actors"]) {
		if (Actor["Class"] != "ASequenceActor" || Actor["Name"].get<FString>().find("CameraSequence") == FString::npos && Actor["Name"] != "Campus_Intro_BobSequence") {
			continue;
		}
		auto Components{ json::array() };
		for (const auto& Component : Actor["Components"]) {
			if (Component["Class"] != "UScriptComponent") {
				Components.push_back(Component);
			}
		}
		Actor["Components"] = Components;
		Actors.push_back(Actor);
	}
	Check(Actors.size() == Expected, "scene contains only the expected camera sequences");
	Document["World"]["PersistentLevel"]["Actors"] = Actors;
	Document["World"]["Properties"] = json::object();
	const FString Text{ Document.dump() };
	TArray<uint8> Bytes{};
	Bytes.SetNum(static_cast<int32>(Text.size()));
	std::memcpy(Bytes.GetData(), Text.data(), Text.size());
	UWorld* World{ UWorld::CreateWorld(EWorldType::Game, false) };
	Check(FJsonArchive::LoadWorldFromBytes(World, Bytes), "camera keys, spline mode and cuts deserialize");
	ACameraActor* View{ World->SpawnActor<ACameraActor>(FName{ "CameraView" }) };
	ACameraActor* PlayerView{ World->SpawnActor<ACameraActor>(FName{ "PlayerView" }) };
	ACameraActor* Player{ World->SpawnActor<ACameraActor>(FName{ "Player" }) };
	ACameraActor* Subject{ World->SpawnActor<ACameraActor>(FName{ "Subject" }) };
	ASequenceActor* Fade{ World->SpawnActor<ASequenceActor>(FName{ "TestFade" }) };
	sol::state& State{ *VM.GetState() };
	State["View"] = static_cast<AActor*>(View);
	State["Camera"] = View->GetCameraComponent();
	State["PlayerView"] = static_cast<AActor*>(PlayerView);
	State["Player"] = static_cast<AActor*>(Player);
	State["Subject"] = static_cast<AActor*>(Subject);
	State["Fade"] = static_cast<AActor*>(Fade);
	World->GetPlayerCameraManager()->SetViewTarget(PlayerView);
	Check(VM.ExecuteString("package.path = 'Assets/Scripts/GameLogic/?.lua;' .. package.path; PlaySound = function() return 0 end; StopSound = function() end; SetCursorMode = function() end"), "production script environment initializes");
	float PositionError{};
	float RotationError{};
	float FOVError{};
	int32 Samples{};
	const auto LoadedActors{ World->GetPersistentLevel()->GetActors() };
	for (AActor* Actor : LoadedActors) {
		ASequenceActor* Sequence{ Cast<ASequenceActor>(Actor) };
		if (!Sequence || Sequence == Fade) {
			continue;
		}
		const FString Name{ Actor->GetName() };
		const FString ReferenceName{ Name == "Campus_Intro_CameraSequence" ? "IntroCamera" : Name == "Campus_Intro_BobSequence" ? "IntroBob" : Name == "EndingCarCameraSequence" ? "Car" : "Door" };
		FSequencePlayer& Playback{ Sequence->GetSequencePlayer() };
		Check(std::get<FCameraTimelineTrack>(Playback.GetSequenceData().Rows.front().Rows).mUseCatmullRom, "serialized spline mode is active in native playback");
		for (const auto& Sample : References[ReferenceName]) {
			Playback.EvaluateAtTime(Sample[0].get<double>());
			CameraKey Key{};
			Check(Playback.GetCurrentCameraKey(Key), "native camera evaluates at the requested time");
			for (int32 Axis{ 0 }; Axis < 3; ++Axis) {
				PositionError = std::max(PositionError, std::abs(Key.Location[Axis] - Sample[Axis + 1].get<float>()));
				RotationError = std::max(RotationError, std::abs(Key.Rotation[Axis] - Sample[Axis + 4].get<float>()));
			}
			FOVError = std::max(FOVError, std::abs(Key.FieldOfView - Sample[7].get<float>()));
			++Samples;
		}
		State["Sequence"] = Actor;
		State[ReferenceName] = Actor;
		Check(VM.ExecuteString("assert(Sequence:EvaluateCameraSequence(1.2345, Camera, FVector.new(11,-7,3), 90, -1, false, false))"), "Lua evaluates camera keys with the original origin and mirrored side");
		CameraKey Key{};
		Playback.GetCurrentCameraKey(Key);
		Check(Near(View->GetActorLocation().X, 11.0f + Key.Location.Y) && Near(View->GetActorLocation().Y, -7.0f + Key.Location.X), "native camera uses the route origin and rotation");
		Check(Near(Subject->GetActorLocation().X, 0.0f), "sampling a camera sequence does not move another actor");
		const float Frame{ Playback.GetPlaybackFrame() };
		Sequence->Tick(0.25f);
		Check(Playback.GetPlaybackFrame() == Frame, "script sampling and sequence ticks do not advance time twice");
		const FRotator Rotation{ View->GetActorRotation() };
		const float FOV{ View->GetCameraComponent()->GetFieldOfView() };
		Check(VM.ExecuteString("assert(Sequence:EvaluateCameraSequence(0.5, Camera, FVector.new(0,0,1), 0, 1, true, true))"), "camera bob supports relative position sampling");
		Check(Near(View->GetActorRotation().Yaw, Rotation.Yaw) && Near(View->GetCameraComponent()->GetFieldOfView(), FOV), "relative camera bob preserves rotation and FOV");
	}
	std::cout << Path << ": " << Samples << " samples, position=" << PositionError << ", rotation=" << RotationError << ", FOV=" << FOVError << '\n';
	Check(PositionError < 0.0002f && RotationError < 0.003f && FOVError < 0.001f, "spline camera preserves the previous path and timing within numeric tolerance");
	if (Expected == 4) {
		Check(VM.ExecuteString("local Intro = require('IntroCutscene'); Intro.RegisterView(View); Intro.RegisterPlayerView(PlayerView); Intro.RegisterDoor(Subject); Intro.RegisterFade(Fade); Intro.RegisterCameraSequence(IntroCamera, false); Intro.RegisterCameraSequence(IntroBob, true); assert(not Intro.Update(9, Player)); assert(Player:GetActorLocation().X < Intro.Config.Spawn.X and Subject:GetActorLocation().Z > Intro.Config.Door.Z); Intro.Finish(); assert(Player:GetActorLocation().X == Intro.Config.Destination.X); assert(View:GetPlayerCameraManager():GetViewTargetCamera() == PlayerView:GetCamera()); Intro.Stop()"), "production intro keeps Lua player and door motion and restores the camera after skip");
	}
	for (const FString Route : { FString{ "Car" }, FString{ "Door" } }) {
		if (!State[Route].valid()) {
			continue;
		}
		State["Route"] = Route;
		FFogInfo WorldFog{};
		WorldFog.FogDensity = 0.08f;
		WorldFog.FogMaxOpacity = 0.98f;
		WorldFog.StartDistance = 45.0f;
		World->GetScene().SetFogOverride(WorldFog);
		Check(VM.ExecuteString("local Ending = require('EndingCutscene'); Ending.RegisterCameraSequence(_G[Route], Route); Subject:SetActorLocation(FVector.new(12,-4,0.5)); Subject:SetActorRotation(FRotator.new(0,90,0)); Ending.RegisterSubject(Subject, Route); assert(Ending.Start(View, Route)); Ending.Update(5.35); assert(math.abs(Ending.GetState().mElapsed - 4) < 0.000001)"), "production ending preserves Lua subject motion and camera transition");
		const FFogInfo* InitialFog{ World->GetScene().GetFogInfo() };
		Check(InitialFog && Near(InitialFog->FogDensity, 0.08f) && Near(InitialFog->StartDistance, 45.0f), "the native camera transition preserves world distance fog");
		Check(VM.ExecuteString("require('EndingCutscene').Update(4)"), "production camera reaches the exterior");
		const FFogInfo* ExteriorFog{ World->GetScene().GetFogInfo() };
		Check(ExteriorFog && Near(ExteriorFog->FogDensity, 0.08f) && Near(ExteriorFog->StartDistance, 45.0f) && Near(ExteriorFog->FogMaxOpacity, 0.98f), "crossing the exit keeps the same native world fog");
		Check(VM.ExecuteString("local Ending = require('EndingCutscene'); Ending.Finish(); Ending.Stop(); assert(Subject:GetActorLocation().X == 12 and Subject:GetActorRotation().Yaw == 90); Ending.UnregisterSubject(Subject); Ending.UnregisterCameraSequence(_G[Route])"), "production ending preserves skip and actor restoration");
		Check(World->GetScene().HasFogOverride() && Near(World->GetScene().GetFogInfo()->StartDistance, 45.0f), "stopping the native ending preserves the world's fog");
		World->GetScene().ResetFogOverride();
		State[Route] = sol::nil;
	}
	Check(FJsonArchive::SaveWorldToBytes(World, Bytes) && FJsonArchive::LoadWorldFromBytes(World, Bytes), "camera spline settings survive a scene round trip");
	World->ClearWorld();
	Check(VM.ExecuteString("assert(not Sequence:EvaluateCameraSequence(1, Camera, FVector.new(0,0,0), 0, 1, false, false))"), "unloaded sequences reject stale camera calls");
	DestroyWorld(World);
}

int main() {
	TestSpline();
	FLuaVM VM{};
	Check(VM.Initialize(), "Lua VM initializes camera sequence bindings");
	TestScene(VM, "Assets/Scenes/Demo/06_Game.scene", 4);
	std::cout << "PASS: " << Checks << " camera sequence checks\n";
}
