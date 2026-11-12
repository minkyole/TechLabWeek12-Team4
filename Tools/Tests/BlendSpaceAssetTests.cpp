#include "EnginePCH.h"
#include "Asset/AssetManager.h"
#include "Animation/BlendSpace.h"
#include "Animation/AnimSequence.h"
#include "Core/Misc/FileHelper.h"
#include "Render/Mesh.h"
#include "UObject/UObjectGlobals.h"
#include <memory>
#include <cstdlib>
#include <cmath>

namespace
{
	int Checks = 0;
	void Check(bool Passed, const char* Message)
	{
		++Checks;
		if (!Passed) { std::cerr << "FAIL: " << Message << '\n'; std::exit(1); }
	}
}

int main()
{
	auto& Manager = UAssetManager::Get();
	std::unique_ptr<USkeletalMesh> Mesh(NewObject<USkeletalMesh>());
	std::unique_ptr<USkeletalMesh> OtherMesh(NewObject<USkeletalMesh>());
	std::unique_ptr<UAnimSequence> Idle(NewObject<UAnimSequence>()), Run(NewObject<UAnimSequence>());
	Idle->BuildFromTracks(FFrameRate(30, 1), 30, {});
	Run->BuildFromTracks(FFrameRate(30, 1), 15, {});
	Idle->SetSkeletalMesh(Mesh.get()); Run->SetSkeletalMesh(Mesh.get());
	auto Register = [&]() {
		Manager.RegisterAsset("BlendSpaceTests/Character.fbx", Mesh.get());
		Manager.RegisterAsset("BlendSpaceTests/Other.fbx", OtherMesh.get());
		Manager.RegisterAsset("BlendSpaceTests/Character.fbx#Anim/Idle", Idle.get());
		Manager.RegisterAsset("BlendSpaceTests/Character.fbx#Anim/Run", Run.get());
	};
	Register();
	const fs::path Folder = fs::path("Assets") / ("_BlendSpaceTests_" + std::to_string(GetCurrentProcessId()));
	Check(fs::create_directory(Folder), "create isolated test folder");
	const FString Path = FFileHelper::ToUtf8Path(Folder / "Locomotion.blendspace");
	FString Error;
	auto* Original = Manager.CreateBlendSpace1D(Path, Mesh.get(), Error);
	Check(Original && Original->GetNumSamples() == 0, "create and save empty asset");
	Check(Original->GetBlendParameter(0)->Name.ToString() == "Speed", "default Speed axis");
	Check(!Manager.CreateBlendSpace1D(Path, Mesh.get(), Error), "creation refuses overwrite");
	Check(Manager.GetAnimationAssets(Mesh.get()).Num() == 2, "enumerate compatible registered sequences");
	std::unique_ptr<UBlendSpace1D> Draft(NewObject<UBlendSpace1D>());
	Draft->CopyDefinitionFrom(*Original);
	Check(Draft->AddSample({Run.get(), FVector(600,0,0)}), "add run");
	Check(Draft->AddSample({Idle.get(), FVector(0,0,0)}), "add idle and sort");
	Check(!Draft->AddSample({Run.get(), FVector(0,0,0)}), "reject duplicate coordinate");
	FBlendParameter Narrow = *Draft->GetBlendParameter(0); Narrow.Max = 300;
	Check(!Draft->SetBlendParameter(0, Narrow), "reject shrinking axis past samples");
	Check(Original->GetNumSamples() == 0, "draft does not mutate live asset");
	Check(Manager.SaveBlendSpace(Path, *Draft, Error), "save edited definition");
	Check(Manager.LoadBlendSpace(Path, Error) == Original && Original->GetNumSamples() == 2, "save preserves live pointer");
	FString Text;
	Check(FFileHelper::LoadFileToString(Text, Path), "read serialized asset");
	const json Good = json::parse(Text);
	Check(Good["Samples"][0]["AnimationAssetKey"] == Idle->GetPath(), "serialize animation AssetKey");
	Check(Good["Samples"][0].size() == 2, "sample contains authored key and X only");
	Manager.Shutdown(); Register();
	auto* Reloaded = Manager.LoadBlendSpace(Path, Error);
	Check(Reloaded && Reloaded != Original && Reloaded->GetNumSamples() == 2, "reload from disk after registry reset");
	Check(Reloaded->GetSample(0)->Animation == Idle.get(), "resolve animation reference");
	TArray<FBlendSampleWeight> Weights;
	Check(Reloaded->GetSampleWeights(FVector(300,0,0), Weights) && Weights.Num() == 2 && std::abs(Weights[0].Weight-.5f)<.0001f, "restored blend weights");
	Check(Reloaded->GetSampleWeights(FVector(-10,0,0), Weights) && Weights.Num() == 1 && Weights[0].SampleIndex == 0, "clamp lower input");
	Check(Reloaded->GetSampleWeights(FVector(700,0,0), Weights) && Weights.Num() == 1 && Weights[0].SampleIndex == 1, "clamp upper input");
	auto Reject = [&](json Bad, const char* Message) {
		const FString BadPath = FFileHelper::ToUtf8Path(Folder / "Invalid.blendspace");
		Check(FFileHelper::SaveStringToFile(Bad.dump(), BadPath), "write invalid fixture");
		Check(!Manager.LoadBlendSpace(BadPath, Error) && !Error.empty(), Message);
	};
	json Bad = Good; Bad["Version"] = 2; Reject(Bad, "reject unknown version");
	Bad = Good; Bad["Type"] = "BlendSpace2D"; Reject(Bad, "reject unsupported type");
	Bad = Good; Bad["Samples"][0]["AnimationAssetKey"] = "Missing"; Reject(Bad, "reject missing animation");
	Bad = Good; Bad["Samples"][0]["AnimationAssetKey"] = Mesh->GetPath(); Reject(Bad, "reject wrong referenced type");
	Bad = Good; Bad["Samples"][1]["X"] = 0; Reject(Bad, "reject duplicate file coordinate");
	Bad = Good; Bad["Samples"][1]["X"] = 601; Reject(Bad, "reject out of range file coordinate");
	Bad = Good; Bad["Axis"]["Max"] = 0; Reject(Bad, "reject invalid file axis");
	Bad = Good; Bad["Samples"] = json::object(); Reject(Bad, "reject non-array samples");
	Run->SetSkeletalMesh(OtherMesh.get()); Reject(Good, "reject mismatched skeleton");
	Check(!Manager.SaveBlendSpace(Path, *Draft, Error), "reject invalid definition before write");
	FString After; FFileHelper::LoadFileToString(After, Path);
	Check(After == Text, "validation failure preserves saved file");
	Run->SetSkeletalMesh(Mesh.get());
	Check(SetFileAttributesW(FFileHelper::ToNativePath(Path).c_str(), FILE_ATTRIBUTE_READONLY), "set read-only fixture");
	Check(!Manager.SaveBlendSpace(Path, *Draft, Error), "report failed file replacement");
	FFileHelper::LoadFileToString(After, Path); Check(After == Text, "failed replacement preserves saved file");
	SetFileAttributesW(FFileHelper::ToNativePath(Path).c_str(), FILE_ATTRIBUTE_NORMAL);
	const FString InvalidPath = FFileHelper::ToUtf8Path(Folder / "Invalid.blendspace");
	FFileHelper::SaveStringToFile("{", InvalidPath);
	Check(!Manager.LoadBlendSpace(InvalidPath, Error), "reject malformed JSON");
	Check(!Manager.SaveBlendSpace("Build/Outside.blendspace", *Draft, Error), "reject destination outside Assets");
	Check(!Manager.SaveBlendSpace(FFileHelper::ToUtf8Path(Folder / "Missing" / "file.blendspace"), *Draft, Error), "report missing save directory");
	// Scan performs its composite-asset pass even when the folder only contains BlendSpaces.
	fs::remove(FFileHelper::ToNativePath(InvalidPath));
	Manager.Shutdown(); Register();
	Manager.ScanAssets(Folder, nullptr);
	const FString Key = FFileHelper::ToUtf8Path(fs::relative(FFileHelper::ToNativePath(Path), "Assets"));
	auto* Scanned = Manager.GetAssetByPath<UBlendSpace1D>(Key);
	Check(Scanned != nullptr, "scan loads BlendSpace in composite pass");
	Manager.Shutdown();
	fs::remove(FFileHelper::ToNativePath(Path)); fs::remove(Folder);
	delete Original; delete Reloaded; delete Scanned;
	std::cout << "PASS: " << Checks << " BlendSpace asset checks\n";
}
