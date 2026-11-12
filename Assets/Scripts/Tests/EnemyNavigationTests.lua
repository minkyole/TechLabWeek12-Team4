package.path = "Assets/Scripts/GameLogic/?.lua;" .. package.path

FVector = {}

function FVector.new(X, Y, Z)
    return { X = X, Y = Y, Z = Z }
end

local MapModule = require("Map")
local EnemyNavigation = require("EnemyNavigation")
local OriginalFindPath = MapModule.FindPath
local Passed = 0

local function NewMap(Separated)
    local Map = MapModule.New()
    local Rows = {}
    for Y = 1, 15 do
        Rows[Y] = Separated and ".......#......." or string.rep(".", 15)
    end
    Map:LoadFloor(1, table.concat(Rows, "\n"))
    return Map, EnemyNavigation.New(Map, FVector.new(0.2, 0.2, 0.5), 0.0)
end

local function Test(Name, Run)
    local Success, Message = pcall(Run)
    MapModule.FindPath = OriginalFindPath
    assert(Success, Name .. ": " .. tostring(Message))
    Passed = Passed + 1
    print("PASS " .. Name)
end

local function SamePath(Actual, Expected)
    assert(Actual ~= nil and Expected ~= nil and #Actual == #Expected)
    for Index, Point in ipairs(Actual) do
        local Other = Expected[Index]
        assert(Point.Floor == Other.Floor and Point.Heading == Other.Heading)
        assert(Point.Location.X == Other.Location.X and Point.Location.Y == Other.Location.Y and Point.Location.Z == Other.Location.Z)
    end
end

Test("ordinary A star results remain unchanged", function()
    local Map = MapModule.New()
    Map:LoadFloor(1, ".....\n#####\n.....")
    local Path, Distance = Map:FindPath({ Floor = 1, X = 1, Y = 1 }, { Floor = 1, X = 5, Y = 1 })
    assert(Path ~= nil and #Path == 5 and Distance == 4.0)
    local Missing, Reason = Map:FindPath({ Floor = 1, X = 1, Y = 1 }, { Floor = 1, X = 1, Y = 3 })
    assert(Missing == nil and Reason == "Unreachable")
end)

Test("unreachable chase uses one A star and preserves its closest path", function()
    local Map, Navigation = NewMap(true)
    local Calls = 0
    local Expected = {}
    MapModule.FindPath = function(SearchMap, Start, Goal, Options)
        Calls = Calls + 1
        local Path, Result, Reason = OriginalFindPath(SearchMap, Start, Goal, Options)
        if SearchMap.mClosestCell ~= nil then
            Expected = OriginalFindPath(SearchMap, Start, SearchMap.mClosestCell, { bAllowDiagonal = false })
        end
        return Path, Result, Reason
    end
    local Path = Navigation:FindChasePath(FVector.new(-7.0, 3.0, 0.0), 90.0, FVector.new(-7.0, 11.0, 0.0))
    assert(Path ~= nil and #Path > 1)
    assert(Calls == 1, "expected one search, got " .. Calls)
    assert(Expected ~= nil)
    local Last = Path[#Path]
    local Cell = Expected[#Expected]
    local ExpectedLocation = Navigation:ToWorld(1, (Cell.X - 1) * 0.5, (Cell.Y - 1) * 0.5)
    assert(Last.Location.X == ExpectedLocation.X and Last.Location.Y == ExpectedLocation.Y)
    for Index = 2, #Path do
        local Previous = Path[Index - 1]
        local Point = Path[Index]
        if Previous.Heading == Point.Heading then
            assert(Navigation:CanMove(Previous.Location, Point.Location, Point.Floor, Point.Heading))
        else
            assert(Previous.Location.X == Point.Location.X and Previous.Location.Y == Point.Location.Y)
            assert(Navigation:CanTurn(Previous.Location, Previous.Floor))
        end
    end
end)

Test("identical unreachable chase requests reuse only their last result", function()
    local Map, Navigation = NewMap(true)
    local Calls = 0
    MapModule.FindPath = function(...)
        Calls = Calls + 1
        return OriginalFindPath(...)
    end
    local Location = FVector.new(-7.0, 3.0, 0.0)
    local Target = FVector.new(-7.0, 11.0, 0.0)
    local First = Navigation:FindChasePath(Location, 90.0, Target)
    for Attempt = 1, 5 do
        SamePath(Navigation:FindChasePath(Location, 90.0, Target), First)
    end
    assert(Calls == 1, "expected one search, got " .. Calls)
    First[1] = nil
    local Reused = Navigation:FindChasePath(Location, 90.0, Target)
    assert(Reused ~= nil and Reused[1] ~= nil)
    Navigation:FindChasePath(Location, 90.0, FVector.new(-6.5, 11.0, 0.0))
    assert(Calls == 2)
    Navigation:FindChasePath(FVector.new(-7.5, 3.0, 0.0), 90.0, Target)
    assert(Calls == 3)
    Navigation:FindChasePath(Location, 180.0, Target)
    assert(Calls == 4)
    Map:LoadFloor(1, string.rep(string.rep(".", 15) .. "\n", 15))
    assert(not Navigation:IsCurrent(Map))
    local Updated = EnemyNavigation.New(Map, FVector.new(0.2, 0.2, 0.5), 0.0)
    local Path = Updated:FindChasePath(Location, 90.0, Target)
    assert(Path ~= nil and Path[#Path].Location.Y == Target.Y and Calls == 5)
end)

Test("reachable chase requests keep normal replanning", function()
    local Map, Navigation = NewMap(false)
    local Calls = 0
    MapModule.FindPath = function(...)
        Calls = Calls + 1
        return OriginalFindPath(...)
    end
    for Attempt = 1, 2 do
        local Path = Navigation:FindChasePath(FVector.new(-7.0, 3.0, 0.0), 90.0, FVector.new(-7.0, 11.0, 0.0))
        assert(Path ~= nil and Path[#Path].Location.Y == 11.0)
    end
    assert(Calls == 2)
end)

Test("patrol tries at most eight distinct goals", function()
    local Map, Navigation = NewMap(false)
    local Calls = 0
    local Goals = {}
    MapModule.FindPath = function(SearchMap, Start, Goal, Options)
        local Key = Goal.X .. ":" .. Goal.Y
        assert(not Goals[Key], "repeated patrol goal " .. Key)
        Goals[Key] = true
        Calls = Calls + 1
        return nil, "Unreachable"
    end
    local Path = Navigation:FindPatrolPath(FVector.new(-7.0, 7.0, 0.0), 0.0, 4.0, true)
    assert(Path == nil)
    assert(Calls > 0 and Calls <= 8, "expected at most eight searches, got " .. Calls)
end)

Test("patrol still tries nearby goals after distant goals fail", function()
    local Map, Navigation = NewMap(false)
    local Calls = 0
    Navigation.CanTurnAtNode = function(Self, Floor, X, Y)
        return Y == 10 and (X == 12 or X == 20)
    end
    MapModule.FindPath = function(SearchMap, Start, Goal, Options)
        Calls = Calls + 1
        if Goal.X == 25 and Goal.Y == 21 then
            return { Start, { Floor = Start.Floor, X = Goal.X, Y = Goal.Y }, Goal }, 1.0
        end
        return nil, "Unreachable"
    end
    local Path = Navigation:FindPatrolPath(FVector.new(-5.0, 5.0, 0.0), 90.0, 4.0, false)
    assert(Path ~= nil and Path[#Path].Location.Y == 6.0)
    assert(Calls == 2, "expected one failed distant goal and one nearby goal")
end)

print("Enemy navigation tests passed: " .. Passed)
