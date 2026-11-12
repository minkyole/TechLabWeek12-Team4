local Map = require("Map")
local GameManager = require("GameManager")
local MapObstacles = require("MapObstacles")

local MAP_DATA_MODULE = "MapData"
local DEBUG_KEY = Key.F9

local currentMap = nil

local function LoadMapData()
    package.loaded[MAP_DATA_MODULE] = nil
    return require(MAP_DATA_MODULE)
end

local function BuildMap()
    local data = LoadMapData()
    local map = Map.New({
        Origin = self:GetActorLocation(),
        CellSize = data.CellSize,
        FloorHeight = data.FloorHeight,
        bAllowDiagonal = data.bAllowDiagonal,
    })

    for floor, text in pairs(data.Floors) do
        local floorData = map:LoadFloor(floor, text)
        print("[MapActor] Floor", floor, "loaded:", floorData.Width .. "x" .. floorData.Height)
    end

    for _, link in ipairs(data.Links or {}) do
        map:AddLink(link.From, link.To, link.Cost, link.bOneWay)
    end

    if data.Dressing ~= nil then
        local BlockedCells = MapObstacles.Apply(map, data.Dressing)
        print("[MapActor] Facility obstacle cells:", BlockedCells)
    end

    currentMap = map
    GameManager.SetMap(map)
    return map
end

local function LogPlayerCell()
    local player = GameManager.GetPlayer()
    if not player or not currentMap then
        print("[MapActor] No player or map")
        return
    end

    local location = player:GetActorLocation()
    local cell = currentMap:WorldToCell(location)
    print("[MapActor] Player", location.X, location.Y, location.Z,
        "-> cell", cell.Floor, cell.X, cell.Y,
        currentMap:IsWalkable(cell) and "floor" or "WALL")
end

function BeginPlay()
    BuildMap()
end

function Tick(dt)
    if not GameManager.IsPlaying() then
        return
    end

    if IsKeyPressed(DEBUG_KEY) then
        LogPlayerCell()
    end
end

function EndPlay()
    if GameManager.GetMap() == currentMap then
        GameManager.SetMap(nil)
    end
    currentMap = nil
end

function OnReload()
    BuildMap()
end
