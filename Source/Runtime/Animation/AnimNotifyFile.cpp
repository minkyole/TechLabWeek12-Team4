#include "EnginePCH.h"
#include "AnimNotifyFile.h"

#include "Animation/AnimSequence.h"
#include "Animation/AnimNotifies/AnimNotify.h"
#include "Core/Misc/FileHelper.h"
#include "UObject/UObjectHash.h"
#include "UObject/UObjectGlobals.h"

#include <algorithm>

namespace
{
	constexpr int32 NotifyFileVersion = 2;

	float ReadFloat(const json& Object, const char* Key, float Default)
	{
		auto It = Object.find(Key);
		return (It != Object.end() && It->is_number()) ? It->get<float>() : Default;
	}

	FString ReadString(const json& Object, const char* Key)
	{
		auto It = Object.find(Key);
		return (It != Object.end() && It->is_string()) ? It->get<FString>() : FString();
	}

	json SaveObjectProperties(const UObject* Object)
	{
		json Properties = json::object();
		for (const UClass* Class = Object->GetClass(); Class; Class = Class->Super)
		{
			for (const FProperty& Property : Class->GetProperties())
			{
				if (Property.HasAnyFlags(CPF_Transient))
				{
					continue;
				}

				const FString Name = Property.Name.ToString();
				const void* Ptr = reinterpret_cast<const uint8*>(Object) + Property.Offset;
				switch (Property.Type)
				{
				case EPropertyType::Float:  Properties[Name] = *static_cast<const float*>(Ptr); break;
				case EPropertyType::Int:    Properties[Name] = *static_cast<const int32*>(Ptr); break;
				case EPropertyType::Bool:   Properties[Name] = *static_cast<const bool*>(Ptr); break;
				case EPropertyType::String: Properties[Name] = *static_cast<const FString*>(Ptr); break;
				default: break;
				}
			}
		}
		return Properties;
	}

	void LoadObjectProperties(UObject* Object, const json& Properties)
	{
		if (!Properties.is_object())
		{
			return;
		}

		for (const UClass* Class = Object->GetClass(); Class; Class = Class->Super)
		{
			for (const FProperty& Property : Class->GetProperties())
			{
				// 저장·로드 양쪽의 임시 프로퍼티 제외
				if (Property.HasAnyFlags(CPF_Transient))
				{
					continue;
				}

				auto It = Properties.find(Property.Name.ToString());
				if (It == Properties.end())
				{
					continue;
				}

				void* Ptr = reinterpret_cast<uint8*>(Object) + Property.Offset;
				switch (Property.Type)
				{
				case EPropertyType::Float:
					if (It->is_number()) *static_cast<float*>(Ptr) = It->get<float>();
					break;
				case EPropertyType::Int:
					if (It->is_number_integer()) *static_cast<int32*>(Ptr) = It->get<int32>();
					break;
				case EPropertyType::Bool:
					if (It->is_boolean()) *static_cast<bool*>(Ptr) = It->get<bool>();
					break;
				case EPropertyType::String:
					if (It->is_string()) *static_cast<FString*>(Ptr) = It->get<FString>();
					break;
				default:
					break;
				}
			}
		}
	}

	UAnimNotify* CreateNotifyFromEntry(const json& Entry, UAnimSequence* Sequence, const FString& NotifyPath)
	{
		const FString ClassName = ReadString(Entry, "Class");
		if (ClassName.empty())
		{
			return nullptr;
		}

		UClass* Class = FindClass(ClassName);
		if (!Class || !Class->IsChildOf(UAnimNotify::StaticClass()))
		{
			HTR_LOG(Warning, "[Notify] {}: unknown notify class {}, loaded as name notify", NotifyPath, ClassName);
			return nullptr;
		}

		UAnimNotify* Notify = NewObject<UAnimNotify>(Sequence, Class);
		if (!Notify)
		{
			return nullptr;
		}

		auto PropertiesIt = Entry.find("Properties");
		if (PropertiesIt != Entry.end())
		{
			LoadObjectProperties(Notify, *PropertiesIt);
		}
		Notify->NormalizeProperties();
		return Notify;
	}
}

FString FAnimNotifyFile::GetNotifyPath(const FString& FbxPath)
{
	return FbxPath + ".notify";
}

bool FAnimNotifyFile::Load(const FString& FbxPath, const TArray<UAnimSequence*>& Sequences)
{
	for (UAnimSequence* Sequence : Sequences)
	{
		if (Sequence)
		{
			Sequence->ClearNotifies();
		}
	}

	const FString NotifyPath = GetNotifyPath(FbxPath);
	if (!std::filesystem::exists(FFileHelper::ToNativePath(NotifyPath)))
	{
		return false;
	}

	FString Text;
	if (!FFileHelper::LoadFileToString(Text, NotifyPath))
	{
		HTR_LOG(Warning, "[Notify] Cannot read {}", NotifyPath);
		return false;
	}

	const json Root = json::parse(Text, nullptr, false);
	if (Root.is_discarded() || !Root.is_object())
	{
		HTR_LOG(Warning, "[Notify] Invalid json: {}", NotifyPath);
		return false;
	}

	auto VersionIt = Root.find("Version");
	const int32 Version = (VersionIt != Root.end() && VersionIt->is_number_integer()) ? VersionIt->get<int32>() : 0;
	if (Version < 1 || Version > NotifyFileVersion)
	{
		HTR_LOG(Warning, "[Notify] Unknown version {} in {}, reading anyway", Version, NotifyPath);
	}

	auto ClipsIt = Root.find("Clips");
	if (ClipsIt == Root.end() || !ClipsIt->is_object())
	{
		return true;
	}

	int32 MatchedClips = 0;
	for (UAnimSequence* Sequence : Sequences)
	{
		if (!Sequence)
		{
			continue;
		}

		auto ClipIt = ClipsIt->find(Sequence->GetName());
		if (ClipIt == ClipsIt->end() || !ClipIt->is_array())
		{
			continue;
		}

		for (const json& Entry : *ClipIt)
		{
			if (!Entry.is_object())
			{
				continue;
			}

			const FString Name = ReadString(Entry, "Name");
			UAnimNotify* NotifyObject = CreateNotifyFromEntry(Entry, Sequence, NotifyPath);
			if (Name.empty() && !NotifyObject)
			{
				continue;
			}

			FAnimNotifyEvent& Notify = Sequence->Notifies.AddDefaulted_GetRef();
			Notify.NotifyName = FName(Name);
			Notify.TriggerTime = std::max(0.0f, ReadFloat(Entry, "Time", 0.0f));
			Notify.Duration = std::max(0.0f, ReadFloat(Entry, "Duration", 0.0f));
			Notify.Notify = NotifyObject;
		}

		std::stable_sort(Sequence->Notifies.begin(), Sequence->Notifies.end(),
			[](const FAnimNotifyEvent& A, const FAnimNotifyEvent& B) { return A.TriggerTime < B.TriggerTime; });
		++MatchedClips;
	}

	if (MatchedClips < static_cast<int32>(ClipsIt->size()))
	{
		HTR_LOG(Warning, "[Notify] {}: {} clip(s) in file have no matching sequence",
			NotifyPath, static_cast<int32>(ClipsIt->size()) - MatchedClips);
	}

	return true;
}

bool FAnimNotifyFile::Save(const FString& FbxPath, const TArray<UAnimSequence*>& Sequences)
{
	json Clips = json::object();
	for (const UAnimSequence* Sequence : Sequences)
	{
		if (!Sequence || Sequence->Notifies.IsEmpty())
		{
			continue;
		}

		json Entries = json::array();
		for (const FAnimNotifyEvent& Notify : Sequence->Notifies)
		{
			json Entry = {
				{ "Name", Notify.NotifyName.IsNone() ? FString() : Notify.NotifyName.ToString() },
				{ "Time", Notify.TriggerTime },
				{ "Duration", Notify.Duration },
			};
			if (Notify.Notify)
			{
				Entry["Class"] = Notify.Notify->GetClass()->GetName();
				Entry["Properties"] = SaveObjectProperties(Notify.Notify);
			}
			Entries.push_back(std::move(Entry));
		}
		Clips[Sequence->GetName()] = std::move(Entries);
	}

	json Root = json::object();
	Root["Version"] = NotifyFileVersion;
	Root["Clips"] = std::move(Clips);

	const FString NotifyPath = GetNotifyPath(FbxPath);
	if (!FFileHelper::SaveStringToFile(Root.dump(4), NotifyPath))
	{
		HTR_LOG(Error, "[Notify] Cannot write {}", NotifyPath);
		return false;
	}
	return true;
}
