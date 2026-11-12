#include "EnginePCH.h"
#include "Editor/MapTools/MapWallGenerator.h"

#include "Asset/AssetManager.h"
#include "Components/BoxComponent.h"
#include "Components/ScriptComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/Level.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Render/Material.h"
#include "Scripting/LuaVM.h"

#include <algorithm>
#include <sol/sol.hpp>

namespace
{
	const FString GeneratedPrefix = "MapGen_";
	const FString MapActorScriptName = "MapActor.lua";
	const FString MapDataModule = "MapData";
	constexpr float FloorThickness = 1.0f;
	constexpr float CeilingThickness = 0.2f;
	constexpr char WallCharacter = '#';
	constexpr char FloorCharacter = '.';

	struct FMapFloor
	{
		int32 Index = 1;
		TArray<FString> Rows;
	};

	struct FMapData
	{
		float CellSize = 1.0f;
		float FloorHeight = 4.0f;
		float WallHeight = 4.0f;
		bool bCeiling = true;
		FString WallBaseColor;
		FString WallNormal;
		float WallTileSize = 2.0f;
		FString FloorBaseColor;
		FString FloorNormal;
		float FloorTileSize = 1.0f;
		FString CeilingBaseColor;
		FString CeilingNormal;
		float CeilingTileSize = 1.0f;
		TArray<FMapFloor> Floors;
	};

	bool IsBlankLine(const FString& Line)
	{
		return Line.find_first_not_of(" \t") == FString::npos;
	}

	TArray<FString> SplitRows(const FString& Text)
	{
		TArray<FString> Rows;
		size_t Start = 0;
		while (Start <= Text.size())
		{
			size_t End = Text.find('\n', Start);
			if (End == FString::npos)
				End = Text.size();

			FString Line = Text.substr(Start, End - Start);
			if (!Line.empty() && Line.back() == '\r')
				Line.pop_back();
			Rows.Add(Line);
			Start = End + 1;
		}

		while (Rows.Num() > 0 && IsBlankLine(Rows[0]))
			Rows.RemoveAt(0);
		while (Rows.Num() > 0 && IsBlankLine(Rows[Rows.Num() - 1]))
			Rows.RemoveAt(Rows.Num() - 1);

		return Rows;
	}

	bool LoadMapData(FMapData& OutData, FString& OutError)
	{
		FLuaVM* LuaVM = GEngine ? GEngine->GetLuaVM() : nullptr;
		sol::state* State = LuaVM ? LuaVM->GetState() : nullptr;
		if (!State)
		{
			OutError = "Lua VM is not initialized";
			return false;
		}

		sol::state& Lua = *State;
		sol::protected_function SearchPath = Lua["package"]["searchpath"];
		const FString PackagePath = Lua["package"]["path"].get_or<FString>("");
		sol::protected_function_result Found = SearchPath(MapDataModule, PackagePath);
		if (!Found.valid() || Found.get_type() != sol::type::string)
		{
			OutError = "MapData.lua was not found in package.path";
			return false;
		}

		const FString DataPath = Found.get<FString>(0);
		sol::environment Environment(Lua, sol::create, Lua.globals());
		sol::protected_function_result Loaded = Lua.safe_script_file(DataPath, Environment, sol::script_pass_on_error);
		if (!Loaded.valid())
		{
			sol::error Error = Loaded;
			OutError = std::format("Failed to load {}: {}", DataPath, Error.what());
			return false;
		}

		if (Loaded.get_type() != sol::type::table)
		{
			OutError = std::format("{} must return a table", DataPath);
			return false;
		}

		sol::table Data = Loaded.get<sol::table>(0);
		OutData.CellSize = Data.get_or("CellSize", 1.0f);
		OutData.FloorHeight = Data.get_or("FloorHeight", 4.0f);
		OutData.WallHeight = Data.get_or("WallHeight", OutData.FloorHeight);
		OutData.bCeiling = Data.get_or("bCeiling", true);
		OutData.WallBaseColor = Data.get<sol::optional<FString>>("WallBaseColor").value_or(FString());
		OutData.WallNormal = Data.get<sol::optional<FString>>("WallNormal").value_or(FString());
		OutData.WallTileSize = Data.get_or("WallTileSize", 2.0f);
		OutData.FloorBaseColor = Data.get<sol::optional<FString>>("FloorBaseColor").value_or(FString());
		OutData.FloorNormal = Data.get<sol::optional<FString>>("FloorNormal").value_or(FString());
		OutData.FloorTileSize = Data.get_or("FloorTileSize", 1.0f);
		OutData.CeilingBaseColor = Data.get<sol::optional<FString>>("CeilingBaseColor").value_or(FString());
		OutData.CeilingNormal = Data.get<sol::optional<FString>>("CeilingNormal").value_or(FString());
		OutData.CeilingTileSize = Data.get_or("CeilingTileSize", 1.0f);

		sol::optional<sol::table> Floors = Data["Floors"];
		if (!Floors)
		{
			OutError = std::format("{} has no Floors table", DataPath);
			return false;
		}

		for (const auto& [Key, Value] : *Floors)
		{
			if (Key.get_type() != sol::type::number || Value.get_type() != sol::type::string)
				continue;

			FMapFloor Floor;
			Floor.Index = static_cast<int32>(Key.as<double>());
			Floor.Rows = SplitRows(Value.as<FString>());
			OutData.Floors.Add(Floor);
		}

		if (OutData.Floors.Num() == 0)
		{
			OutError = std::format("{} has no floors", DataPath);
			return false;
		}

		std::sort(OutData.Floors.begin(), OutData.Floors.end(),
			[](const FMapFloor& A, const FMapFloor& B) { return A.Index < B.Index; });
		return true;
	}

	bool IsGeneratedActor(const AActor& Actor)
	{
		for (const UActorComponent* Component : Actor.GetComponents())
		{
			if (Component && Component->GetName().rfind(GeneratedPrefix, 0) == 0)
				return true;
		}
		return false;
	}

	int32 RemoveGeneratedActors(UWorld& World)
	{
		ULevel* Level = World.GetPersistentLevel();
		if (!Level)
			return 0;

		TArray<AActor*> ToRemove;
		for (AActor* Actor : Level->GetActors())
		{
			if (Actor && IsGeneratedActor(*Actor))
				ToRemove.Add(Actor);
		}

		for (AActor* Actor : ToRemove)
			World.DestroyActor(Actor);

		return ToRemove.Num();
	}

	UMaterial* CreateTiledMaterial(const FString& BaseColorPath, const FString& NormalPath, float TileSize)
	{
		if (BaseColorPath.empty())
			return nullptr;

		UMaterial* Material = UMaterial::CreateInstance(UAssetManager::GetAssetByPath<UMaterial>("DefaultMaterial"));
		if (!Material)
			return nullptr;

		if (UTexture2D* BaseColor = UAssetManager::Get().LoadTexture(BaseColorPath))
			Material->Textures[0] = BaseColor;
		else
			HTR_LOG(Warning, "[MapWallGenerator] Texture not found: {}", BaseColorPath);

		if (!NormalPath.empty())
		{
			if (UTexture2D* Normal = UAssetManager::Get().LoadTexture(NormalPath))
				Material->Textures[1] = Normal;
			else
				HTR_LOG(Warning, "[MapWallGenerator] Normal map not found: {}", NormalPath);
		}

		Material->SamplerState = ESamplerState::LinearWrap;
		Material->WorldUVTileSize = TileSize;
		return Material;
	}

	AActor* SpawnCubeActor(UWorld& World, AActor& Parent, const FVector& Location, const FVector& Scale, const FString& MeshName, const FString& BoxName, UMaterial* Material = nullptr)
	{
		const FTransform Transform(FRotator(0.0f, 0.0f, 0.0f), Location, Scale);
		AActor* Actor = World.SpawnActor(AActor::StaticClass(), NAME_None, &Transform);
		if (!Actor)
			return nullptr;

		UStaticMeshComponent* Mesh = Cast<UStaticMeshComponent>(Actor->AddComponent(UStaticMeshComponent::StaticClass(), FName(MeshName.c_str())));
		if (Mesh && Material)
			Mesh->SetMaterial(0, Material);

		if (!BoxName.empty())
		{
			if (UBoxComponent* Box = Cast<UBoxComponent>(Actor->AddComponent(UBoxComponent::StaticClass(), FName(BoxName.c_str()))))
			{
				Box->SetBoxExtent(FVector(0.5f, 0.5f, 0.5f));
				Box->SetBlockComponent(true);
				Box->SetDrawOnlyIfSelected(true);
			}
		}

		Actor->AttachToActor(&Parent, EAttachmentRule::KeepWorld);
		return Actor;
	}

	char GetCell(const TArray<FString>& Rows, int32 X, int32 Y)
	{
		if (Y < 1 || Y > Rows.Num())
			return 0;
		const FString& Row = Rows[Y - 1];
		if (X < 1 || X > static_cast<int32>(Row.size()))
			return 0;
		return Row[X - 1];
	}

	int32 CountOpenEdgeCells(const FMapFloor& Floor, FString& OutFirstCell)
	{
		static const int32 Offsets[4][2] = { { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 } };
		int32 Count = 0;

		for (int32 Y = 1; Y <= Floor.Rows.Num(); ++Y)
		{
			const FString& Row = Floor.Rows[Y - 1];
			for (int32 X = 1; X <= static_cast<int32>(Row.size()); ++X)
			{
				if (Row[X - 1] != FloorCharacter)
					continue;

				for (const auto& Offset : Offsets)
				{
					const char Neighbour = GetCell(Floor.Rows, X + Offset[0], Y + Offset[1]);
					if (Neighbour != FloorCharacter && Neighbour != WallCharacter)
					{
						if (Count == 0)
							OutFirstCell = std::format("floor {} ({},{})", Floor.Index, X, Y);
						++Count;
						break;
					}
				}
			}
		}

		return Count;
	}
}

bool FMapWallGenerator::IsMapActorScript(const UScriptComponent& ScriptComponent)
{
	return std::filesystem::path(ScriptComponent.GetScriptPath()).filename().string() == MapActorScriptName;
}

FMapWallGenerateResult FMapWallGenerator::Generate(AActor& MapActor)
{
	FMapWallGenerateResult Result;

	UWorld* World = MapActor.GetWorld();
	if (!World || World->GetWorldType() != EWorldType::Editor)
	{
		Result.Message = "Map walls can only be generated in the editor world";
		return Result;
	}

	FMapData Data;
	if (!LoadMapData(Data, Result.Message))
	{
		HTR_LOG(Error, "[MapWallGenerator] {}", Result.Message);
		return Result;
	}

	Result.RemovedActors = RemoveGeneratedActors(*World);

	const FVector Origin = MapActor.GetActorLocation();
	const float Cell = Data.CellSize;
	UMaterial* WallMaterial = CreateTiledMaterial(Data.WallBaseColor, Data.WallNormal, Data.WallTileSize);
	UMaterial* FloorMaterial = CreateTiledMaterial(Data.FloorBaseColor, Data.FloorNormal, Data.FloorTileSize);
	UMaterial* CeilingMaterial = CreateTiledMaterial(Data.CeilingBaseColor, Data.CeilingNormal, Data.CeilingTileSize);
	FString FirstOpenCell;

	for (const FMapFloor& Floor : Data.Floors)
	{
		const float BaseZ = Origin.Z + static_cast<float>(Floor.Index - 1) * Data.FloorHeight;
		int32 Width = 0;

		for (int32 Y = 1; Y <= Floor.Rows.Num(); ++Y)
		{
			const FString& Row = Floor.Rows[Y - 1];
			const int32 RowLength = static_cast<int32>(Row.size());
			Width = std::max(Width, RowLength);

			int32 X = 1;
			while (X <= RowLength)
			{
				if (Row[X - 1] != WallCharacter)
				{
					++X;
					continue;
				}

				const int32 Start = X;
				while (X <= RowLength && Row[X - 1] == WallCharacter)
					++X;
				const int32 End = X - 1;

				const FVector Location(
					Origin.X - static_cast<float>(Y - 1) * Cell,
					Origin.Y + (static_cast<float>(Start + End) * 0.5f - 1.0f) * Cell,
					BaseZ + Data.WallHeight * 0.5f);
				const FVector Scale(Cell, static_cast<float>(End - Start + 1) * Cell, Data.WallHeight);

				if (SpawnCubeActor(*World, MapActor, Location, Scale, GeneratedPrefix + "WallMesh", GeneratedPrefix + "WallBox", WallMaterial))
					++Result.WallActors;
			}
		}

		const int32 Height = Floor.Rows.Num();
		if (Width > 0 && Height > 0)
		{
			const FVector Location(
				Origin.X - static_cast<float>(Height - 1) * Cell * 0.5f,
				Origin.Y + static_cast<float>(Width - 1) * Cell * 0.5f,
				BaseZ - FloorThickness * 0.5f);
			const FVector Scale(static_cast<float>(Height) * Cell, static_cast<float>(Width) * Cell, FloorThickness);

			if (SpawnCubeActor(*World, MapActor, Location, Scale, GeneratedPrefix + "FloorMesh", FString(), FloorMaterial))
				++Result.FloorActors;

			if (Data.bCeiling)
			{
				const FVector CeilingLocation(Location.X, Location.Y, BaseZ + Data.WallHeight + CeilingThickness * 0.5f);
				const FVector CeilingScale(Scale.X, Scale.Y, CeilingThickness);
				if (SpawnCubeActor(*World, MapActor, CeilingLocation, CeilingScale, GeneratedPrefix + "CeilingMesh", FString(), CeilingMaterial))
					++Result.CeilingActors;
			}
		}

		Result.OpenEdgeCells += CountOpenEdgeCells(Floor, FirstOpenCell);
	}

	Result.bSuccess = true;
	Result.Message = std::format("Removed {}, added {} walls, {} floors and {} ceilings",
		Result.RemovedActors, Result.WallActors, Result.FloorActors, Result.CeilingActors);
	if (Result.OpenEdgeCells > 0)
		Result.Message += std::format("\n{} floor cells are open to the map edge (first: {})", Result.OpenEdgeCells, FirstOpenCell);

	HTR_LOG(Info, "[MapWallGenerator] {}", Result.Message);
	return Result;
}
