package.path = "Assets/Scripts/GameLogic/?.lua;" .. package.path

FVector = {}

function FVector.new(X, Y, Z)
    return { X = X, Y = Y, Z = Z }
end

local MapModule = require("Map")
local MapObstacles = require("MapObstacles")
local EnemyNavigation = require("EnemyNavigation")
local Data = require("MapData")
local Map = MapModule.New(Data)
local Passed = 0

for Floor, Text in pairs(Data.Floors) do
    Map:LoadFloor(Floor, Text)
end
MapObstacles.Apply(Map, Data.Dressing)

local function Test(Name, Run)
    Run()
    Passed = Passed + 1
    print("PASS " .. Name)
end

Test("interior, garage and open yard have separate roof regions", function()
    assert(not Data.bCeiling)
    local Regions = {}
    for _, Region in ipairs(Data.SurfaceRegions) do
        Regions[Region.Id] = Region
    end
    assert(Regions.Interior.RoofHeight == 4.0)
    assert(Regions.Garage.RoofHeight > Regions.Interior.RoofHeight)
    assert(Regions.ServiceYard.bOutdoor and Regions.ServiceYard.RoofHeight == nil)
    local CarCell = Map:WorldToCell(Data.Landmarks.CarExit.Location)
    assert(CarCell.X >= Regions.Garage.MinimumColumn and CarCell.X <= Regions.Garage.MaximumColumn)
    assert(CarCell.Y >= Regions.Garage.MinimumRow and CarCell.Y <= Regions.Garage.MaximumRow)
end)

Test("player can reach both escape requirements and the garage from the indoor spawn", function()
    local Start = Map:WorldToCell(FVector.new(-10.0, 10.0, 0.0))
    for _, Target in ipairs({ FVector.new(-51.0, 14.0, 0.0), Data.Landmarks.FuelCan.Location, Data.Landmarks.CardKey.Location, FVector.new(-11.0, 48.0, 0.0) }) do
        local Path, Reason = Map:FindPath(Start, Map:WorldToCell(Target))
        assert(Path ~= nil, "required location is unreachable: " .. Target.X .. "," .. Target.Y .. ": " .. tostring(Reason))
    end
end)

Test("vehicle road remains clear from the garage bay to the perimeter gate", function()
    local CenterY = Data.Landmarks.CarExit.Location.Y
    for X = -16, -59, -1 do
        assert(Map:IsWalkable(Map:WorldToCell(FVector.new(X, CenterY, 0.0))), "blocked vehicle route at " .. X)
    end
    assert(Data.Landmarks.GarageGate.Location.Y == CenterY)
    assert(Data.Landmarks.CarExit.Yaw == 0.0)
end)

Test("the emergency exit is reached through the full enclosed corridor", function()
    local Door = Data.Landmarks.DoorExit.Location
    local CenterY = Door.Y + 0.92
    assert(Door.X == 23.5 and Map.Origin.X == Data.Origin.X)
    local Start = Map:WorldToCell(FVector.new(-10.0, 10.0, 0.0))
    local Target = Map:WorldToCell(FVector.new(Door.X - 1.0, CenterY, 0.0))
    local Path, Reason = Map:FindPath(Start, Target)
    assert(Path ~= nil, tostring(Reason))
    for X = 1, 23 do
        assert(Map:IsWalkable(Map:WorldToCell(FVector.new(X, CenterY, 0.0))), "blocked corridor at " .. X)
        assert(not Map:IsWalkable(Map:WorldToCell(FVector.new(X, CenterY - 2.5, 0.0))))
        assert(not Map:IsWalkable(Map:WorldToCell(FVector.new(X, CenterY + 2.5, 0.0))))
    end
    assert(not Map:IsWalkable(Map:WorldToCell(FVector.new(Door.X + 1.0, CenterY, 0.0))))
    for _, Region in ipairs(Data.SurfaceRegions) do
        if Region.Id == "EscapeCorridor" then
            assert(Region.RoofHeight == 4.0 and not Region.bOutdoor)
            return
        end
    end
    error("the escape corridor has no roof region")
end)

Test("both enemy sizes can chase from the interior into the escape corridor", function()
    local Door = Data.Landmarks.DoorExit.Location
    local Target = FVector.new(Door.X - 2.0, Door.Y + 0.92, 0.0)
    for _, Extent in ipairs({ FVector.new(0.9413, 0.6946, 1.8927), FVector.new(0.3772, 0.7384, 2.4989) }) do
        local Navigation = EnemyNavigation.New(Map, Extent, 0.04)
        local Path = Navigation:FindChasePath(FVector.new(-5.0, 10.0, 0.0), 0.0, Target)
        assert(Path ~= nil)
        local Last = Path[#Path].Location
        assert(math.abs(Last.X - Target.X) < 0.01 and math.abs(Last.Y - Target.Y) < 0.01, "enemy cannot enter the escape corridor")
    end
end)

Test("both wider yard entrances support oriented enemy paths to the garage", function()
    local Target = FVector.new(-17.0, 50.5, 0.0)
    for _, Extent in ipairs({ FVector.new(0.9413, 0.6946, 1.8927), FVector.new(0.3772, 0.7384, 2.4989) }) do
        local Navigation = EnemyNavigation.New(Map, Extent, 0.04)
        for _, Start in ipairs({ FVector.new(-30.0, 30.0, 0.0), FVector.new(-58.0, 35.0, 0.0) }) do
            local Path = Navigation:FindChasePath(Start, 90.0, Target)
            assert(Path ~= nil)
            local Last = Path[#Path].Location
            assert(math.abs(Last.X - Target.X) < 0.01 and math.abs(Last.Y - Target.Y) < 0.01, "enemy stops before reaching garage")
        end
    end
end)

Test("vehicle and new yard props contribute matching navigation footprints", function()
    local Vehicle = nil
    local Props = {}
    for _, Prop in ipairs(Data.Dressing.Props) do
        Props[Prop.Name] = Prop
        if Prop.SceneActor == "CarExit" then
            Vehicle = Prop
        end
    end
    assert(Vehicle ~= nil and Vehicle.Location.X == Data.Landmarks.CarExit.Location.X and Vehicle.Location.Y == Data.Landmarks.CarExit.Location.Y)
    for _, Name in ipairs({ "GarageEscapeVehicle", "GarageVehicleLift", "YardFuelPump", "YardCargoStack" }) do
        assert(not Map:IsWalkable(Map:WorldToCell(Props[Name].Location)), Name .. " is missing from navigation obstacles")
    end
end)

Test("garage vehicle escape still requires both the car key and fuel", function()
    local Items = {}
    local Route = ""
    local Car = Data.Landmarks.CarExit.Location
    local Environment = { Key = { E = 1 }, FVector2 = { new = FVector.new }, self = {} }
    setmetatable(Environment, { __index = _G })
    function Environment.self.GetActorLocation(Self)
        return Car
    end
    function Environment.IsKeyPressed(Value)
        return true
    end
    function Environment.AddUIText(...)
    end
    local Manager = {}
    function Manager.IsPlaying()
        return true
    end
    function Manager.GetPlayerLocation()
        return FVector.new(Car.X, Car.Y - 1.7, 0.5)
    end
    function Manager.Escape(Value)
        Route = Value
    end
    local Inventory = {}
    function Inventory.Has(Id)
        return Items[Id] == true
    end
    local Ending = {}
    function Ending.RegisterSubject(Subject, Value)
        assert(Subject == Environment.self and Value == "Car")
    end
    function Environment.require(Name)
        return ({ GameManager = Manager, Inventory = Inventory, EndingCutscene = Ending })[Name]
    end
    local Script = assert(loadfile("Assets/Scripts/GameLogic/Exit_Car.lua"))
    setfenv(Script, Environment)()
    Environment.BeginPlay()
    Environment.Tick(0.1)
    assert(Route == "")
    Items.CarKey = true
    Environment.Tick(0.1)
    assert(Route == "")
    Items.FuelCan = true
    Environment.Tick(0.1)
    assert(Route == "Car")
end)

print("Campus map tests passed: " .. Passed)
