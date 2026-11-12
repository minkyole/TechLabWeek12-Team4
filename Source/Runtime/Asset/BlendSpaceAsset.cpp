#include "EnginePCH.h"
#include "Asset/AssetManager.h"
#include "Animation/BlendSpace.h"
#include "Animation/AnimSequence.h"
#include "Core/Misc/FileHelper.h"
#include "Render/Mesh.h"
#include "UObject/UObjectGlobals.h"
#include <memory>
#include <cmath>

namespace
{
	bool ResolveBlendSpacePath(const FString& Path, fs::path& Native, FString& Key, FString& Error)
	{
		std::error_code EC;
		Native = fs::weakly_canonical(FFileHelper::ToNativePath(Path), EC);
		if (EC) { Error = "Cannot resolve asset path."; return false; }
		const fs::path Root = fs::weakly_canonical("Assets", EC);
		if (EC) { Error = "Cannot resolve Assets folder."; return false; }
		const fs::path Relative = Native.lexically_relative(Root);
		if (Relative.empty() || Relative.is_absolute() || *Relative.begin() == ".." ||
			_stricmp(Native.extension().string().c_str(), ".blendspace") != 0)
		{
			Error = "Choose a .blendspace file inside Assets."; return false;
		}
		Key = FFileHelper::ToUtf8Path(Relative);
		return true;
	}

	bool ValidateDefinition(const UBlendSpace1D& Definition, UBlendSpace1D& Validated, FString& Error)
	{
		const USkeletalMesh* Mesh = Definition.GetSkeletalMesh();
		if (!Mesh || UAssetManager::GetAssetByPath<USkeletalMesh>(Mesh->GetPath()) != Mesh)
		{
			Error = "The skeletal mesh is not a registered asset."; return false;
		}
		Validated.SetSkeletalMesh(Mesh);
		Validated.NotifyTriggerMode = Definition.NotifyTriggerMode;
		const FBlendParameter* Axis = Definition.GetBlendParameter(0);
		if (!Axis || !Validated.SetBlendParameter(0, *Axis))
		{
			Error = "Axis minimum and maximum must be finite, with minimum < maximum."; return false;
		}
		for (const FBlendSample& Sample : Definition.GetSamples())
		{
			if (!Sample.Animation || UAssetManager::GetAssetByPath<UAnimSequence>(Sample.Animation->GetPath()) != Sample.Animation)
			{
				Error = "A sample animation is not a registered asset."; return false;
			}
			if (!Validated.AddSample(Sample))
			{
				Error = "Invalid sample: check mesh compatibility, duration, axis range and duplicate coordinates.";
				return false;
			}
		}
		return true;
	}
}

TArray<UAnimSequence*> UAssetManager::GetAnimationAssets(const USkeletalMesh* Mesh) const
{
	TArray<UAnimSequence*> Result;
	for (const auto& Entry : AssetMap)
	{
		if (Entry.second && Entry.second->IsA<UAnimSequence>())
		{
			auto* Sequence = Cast<UAnimSequence>(Entry.second);
			if (!Mesh || Sequence->GetSkeletalMesh() == Mesh) Result.Add(Sequence);
		}
	}
	std::sort(Result.begin(), Result.end(), [](const auto* A, const auto* B) { return A->GetPath() < B->GetPath(); });
	return Result;
}

TArray<USkeletalMesh*> UAssetManager::GetSkeletalMeshAssets() const
{
	TArray<USkeletalMesh*> Result;
	for (const auto& Entry : AssetMap)
		if (Entry.second && Entry.second->IsA<USkeletalMesh>()) Result.Add(Cast<USkeletalMesh>(Entry.second));
	std::sort(Result.begin(), Result.end(), [](const auto* A, const auto* B) { return A->GetPath() < B->GetPath(); });
	return Result;
}

UBlendSpace1D* UAssetManager::CreateBlendSpace1D(const FString& Path, const USkeletalMesh* Mesh, FString& Error)
{
	std::unique_ptr<UBlendSpace1D> Definition(NewObject<UBlendSpace1D>());
	Definition->SetSkeletalMesh(Mesh);
	FBlendParameter Axis;
	Axis.Name = FName("Speed");
	Definition->SetBlendParameter(0, Axis);
	if (!SaveBlendSpace(Path, *Definition, Error, true)) return nullptr;
	fs::path Native;
	FString Key;
	if (!ResolveBlendSpacePath(Path, Native, Key, Error)) return nullptr;
	return GetAssetByPath<UBlendSpace1D>(Key);
}

UBlendSpace1D* UAssetManager::LoadBlendSpace(const FString& Path, FString& Error)
{
	Error.clear();
	fs::path Native;
	FString Key;
	if (!ResolveBlendSpacePath(Path, Native, Key, Error)) return nullptr;
	if (auto* Existing = GetAssetByPath<UBlendSpace1D>(Key)) return Existing;
	if (AssetMap.Contains(Key)) { Error = "Asset key already belongs to another asset type."; return nullptr; }
	FString Text;
	if (!FFileHelper::LoadFileToString(Text, FFileHelper::ToUtf8Path(Native)))
	{
		Error = "Cannot read BlendSpace file."; return nullptr;
	}
	try
	{
		const json Data = json::parse(Text);
		if (Data.at("Version") != 1 || Data.at("Type") != "BlendSpace1D")
		{
			Error = "Unsupported BlendSpace version or type."; return nullptr;
		}
		std::unique_ptr<UBlendSpace1D> Definition(NewObject<UBlendSpace1D>());
		const FString MeshKey = Data.at("SkeletalMeshAssetKey").get<FString>();
		auto* Mesh = GetAssetByPath<USkeletalMesh>(MeshKey);
		if (!Mesh) { Error = "Missing skeletal mesh asset: " + MeshKey; return nullptr; }
		Definition->SetSkeletalMesh(Mesh);
		const json& AxisData = Data.at("Axis");
		FBlendParameter Axis;
		Axis.Name = FName(AxisData.at("Name").get<FString>());
		Axis.Min = AxisData.at("Min").get<float>();
		Axis.Max = AxisData.at("Max").get<float>();
		if (!Definition->SetBlendParameter(0, Axis)) { Error = "Invalid axis range."; return nullptr; }
		if (auto ModeIt = Data.find("NotifyTriggerMode"); ModeIt != Data.end() && ModeIt->is_string() &&
			!NotifyTriggerModeFromString(ModeIt->get<FString>(), Definition->NotifyTriggerMode))
		{
			Error = "Unknown NotifyTriggerMode: " + ModeIt->get<FString>(); return nullptr;
		}
		if (!Data.at("Samples").is_array()) { Error = "Samples must be an array."; return nullptr; }
		for (const json& Entry : Data.at("Samples"))
		{
			const FString AnimationKey = Entry.at("AnimationAssetKey").get<FString>();
			FBlendSample Sample;
			Sample.Animation = GetAssetByPath<UAnimSequence>(AnimationKey);
			Sample.SampleValue = FVector(Entry.at("X").get<float>(), 0, 0);
			if (!Sample.Animation) { Error = "Missing animation asset: " + AnimationKey; return nullptr; }
			if (!Definition->AddSample(Sample))
			{
				Error = "Invalid sample (mesh, duration, range or duplicate coordinate): " + AnimationKey; return nullptr;
			}
		}
		auto* Result = Definition.release();
		RegisterAsset(Key, Result);
		AssetPathMap.Add(Key, FFileHelper::ToUtf8Path(Native));
		return Result;
	}
	catch (const json::exception& Exception)
	{
		Error = FString("Invalid BlendSpace file: ") + Exception.what();
		return nullptr;
	}
}

bool UAssetManager::SaveBlendSpace(const FString& Path, const UBlendSpace1D& Definition, FString& Error, bool bCreateOnly)
{
	Error.clear();
	fs::path Native;
	FString Key;
	if (!ResolveBlendSpacePath(Path, Native, Key, Error)) return false;
	std::error_code EC;
	const bool Exists = fs::exists(Native, EC);
	if (EC) { Error = "Cannot inspect destination file."; return false; }
	if (bCreateOnly && (Exists || AssetMap.Contains(Key))) { Error = "An asset with that name already exists."; return false; }
	auto* Target = GetAssetByPath<UBlendSpace1D>(Key);
	if (!Target && AssetMap.Contains(Key)) { Error = "Asset key belongs to another asset type."; return false; }
	std::unique_ptr<UBlendSpace1D> Validated(NewObject<UBlendSpace1D>());
	if (!ValidateDefinition(Definition, *Validated, Error)) return false;
	const FBlendParameter& Axis = *Validated->GetBlendParameter(0);
	json Data = {{"Version", 1}, {"Type", "BlendSpace1D"},
		{"SkeletalMeshAssetKey", Validated->GetSkeletalMesh()->GetPath()},
		{"Axis", {{"Name", Axis.Name.ToString()}, {"Min", Axis.Min}, {"Max", Axis.Max}}},
		{"NotifyTriggerMode", NotifyTriggerModeToString(Validated->NotifyTriggerMode)},
		{"Samples", json::array()}};
	for (const FBlendSample& Sample : Validated->GetSamples())
		Data["Samples"].push_back({{"AnimationAssetKey", Sample.Animation->GetPath()}, {"X", Sample.SampleValue.X}});

	// A unique sibling file keeps failed writes away from the last good asset.
	wchar_t TempName[MAX_PATH];
	if (!GetTempFileNameW(Native.parent_path().c_str(), L"bsp", 0, TempName))
	{
		Error = "Cannot create temporary save file."; return false;
	}
	const fs::path Temp(TempName);
	bool Saved = false;
	try { Saved = FFileHelper::SaveStringToFile(Data.dump(2), FFileHelper::ToUtf8Path(Temp)); }
	catch (const json::exception&) { Error = "Cannot encode asset text."; }
	if (Saved)
	{
		// Without REPLACE_EXISTING, creation cannot overwrite a concurrently created file.
		Saved = MoveFileExW(Temp.c_str(), Native.c_str(), MOVEFILE_WRITE_THROUGH |
			(bCreateOnly ? 0 : MOVEFILE_REPLACE_EXISTING)) != FALSE;
	}
	if (!Saved)
	{
		fs::remove(Temp, EC);
		if (Error.empty()) Error = "Cannot save or replace asset file. Check file permissions.";
		return false;
	}
	if (Target) Target->CopyDefinitionFrom(*Validated);
	else { Target = Validated.release(); RegisterAsset(Key, Target); }
	AssetPathMap.Add(Key, FFileHelper::ToUtf8Path(Native));
	return true;
}

TArray<UBlendSpace1D*> UAssetManager::GetBlendSpaceAssets(const USkeletalMesh* Mesh) const
{
	TArray<UBlendSpace1D*> Result;
	for (const auto& Entry : AssetMap)
	{
		if (Entry.second && Entry.second->IsA<UBlendSpace1D>())
		{
			auto* Asset = Cast<UBlendSpace1D>(Entry.second);
			if (!Mesh || Asset->GetSkeletalMesh() == Mesh) Result.Add(Asset);
		}
	}
	std::sort(Result.begin(), Result.end(), [](const auto* A, const auto* B) { return A->GetPath() < B->GetPath(); });
	return Result;
}
