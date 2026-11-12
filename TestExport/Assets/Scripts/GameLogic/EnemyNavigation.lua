local MapModule = require("Map")
local EnemyNavigation = {}
EnemyNavigation.__index = EnemyNavigation

local Directions = { { X = 0, Y = -1 }, { X = 1, Y = 0 }, { X = 0, Y = 1 }, { X = -1, Y = 0 } }
local MaximumPatrolAttempts = 8

local function PositionKey(Floor, X, Y)
    return (Floor * 4096 + Y) * 4096 + X
end

local function IsAreaClear(Navigation, Floor, WorldX, WorldY, ExtentX, ExtentY, Radius)
    local Map = Navigation.mMap
    local X = WorldY - Map.Origin.Y
    local Y = Map.Origin.X - WorldX
    local CellSize = Map.CellSize
    local HalfCell = CellSize * 0.5
    local MinimumX = math.floor((X - ExtentY) / CellSize + 0.5) + 1
    local MaximumX = math.floor((X + ExtentY) / CellSize + 0.5) + 1
    local MinimumY = math.floor((Y - ExtentX) / CellSize + 0.5) + 1
    local MaximumY = math.floor((Y + ExtentX) / CellSize + 0.5) + 1
    for Row = MinimumY, MaximumY do
        for Column = MinimumX, MaximumX do
            if not Map:IsWalkableAt(Floor, Column, Row) then
                if Radius == nil then
                    return false
                end
                local DistanceX = math.max(math.abs(X - (Column - 1) * CellSize) - HalfCell, 0.0)
                local DistanceY = math.max(math.abs(Y - (Row - 1) * CellSize) - HalfCell, 0.0)
                if DistanceX * DistanceX + DistanceY * DistanceY <= Radius * Radius then
                    return false
                end
            end
        end
    end
    return true
end

function EnemyNavigation.New(Map, Extent, Margin)
    local FloorVersions = {}
    for Floor, Data in pairs(Map.Floors) do
        FloorVersions[Floor] = Data
    end
    local HalfLength = Extent.X + Margin
    local HalfWidth = Extent.Y + Margin
    return setmetatable({ mMap = Map, mHalfLength = HalfLength, mHalfWidth = HalfWidth, mTurnRadius = math.sqrt(HalfLength * HalfLength + HalfWidth * HalfWidth), mSpacing = Map.CellSize * 0.5, mFloorVersions = FloorVersions, mClearance = {}, mTurns = {}, mSearchMaps = {}, mFailedChase = nil }, EnemyNavigation)
end

function EnemyNavigation.IsCurrent(Navigation, Map)
    if Navigation.mMap ~= Map then
        return false
    end
    for Floor, Data in pairs(Map.Floors) do
        if Navigation.mFloorVersions[Floor] ~= Data then
            return false
        end
    end
    for Floor, Data in pairs(Navigation.mFloorVersions) do
        if Map.Floors[Floor] ~= Data then
            return false
        end
    end
    return true
end

function EnemyNavigation.ToWorld(Navigation, Floor, X, Y)
    local Map = Navigation.mMap
    return FVector.new(Map.Origin.X - Y * Navigation.mSpacing, Map.Origin.Y + X * Navigation.mSpacing, Map.Origin.Z + (Floor - 1) * Map.FloorHeight)
end

function EnemyNavigation.IsNodeClear(Navigation, Floor, X, Y, Heading)
    local Key = PositionKey(Floor, X, Y) * 2 + Heading % 2
    local Clear = Navigation.mClearance[Key]
    if Clear == nil then
        local ExtentX = Heading % 2 == 0 and Navigation.mHalfLength or Navigation.mHalfWidth
        local ExtentY = Heading % 2 == 0 and Navigation.mHalfWidth or Navigation.mHalfLength
        Clear = IsAreaClear(Navigation, Floor, Navigation.mMap.Origin.X - Y * Navigation.mSpacing, Navigation.mMap.Origin.Y + X * Navigation.mSpacing, ExtentX, ExtentY)
        Navigation.mClearance[Key] = Clear
    end
    return Clear
end

function EnemyNavigation.CanTurn(Navigation, Location, Floor)
    local Radius = Navigation.mTurnRadius
    return IsAreaClear(Navigation, Floor, Location.X, Location.Y, Radius, Radius, Radius)
end

function EnemyNavigation.CanTurnAtNode(Navigation, Floor, X, Y)
    local Key = PositionKey(Floor, X, Y)
    local Clear = Navigation.mTurns[Key]
    if Clear == nil then
        local Radius = Navigation.mTurnRadius
        Clear = IsAreaClear(Navigation, Floor, Navigation.mMap.Origin.X - Y * Navigation.mSpacing, Navigation.mMap.Origin.Y + X * Navigation.mSpacing, Radius, Radius, Radius)
        Navigation.mTurns[Key] = Clear
    end
    return Clear
end

function EnemyNavigation.CanMove(Navigation, From, To, Floor, Heading)
    local ExtentX = Heading % 2 == 0 and Navigation.mHalfLength or Navigation.mHalfWidth
    local ExtentY = Heading % 2 == 0 and Navigation.mHalfWidth or Navigation.mHalfLength
    return IsAreaClear(Navigation, Floor, (From.X + To.X) * 0.5, (From.Y + To.Y) * 0.5, ExtentX + math.abs(To.X - From.X) * 0.5, ExtentY + math.abs(To.Y - From.Y) * 0.5)
end

local function FindStart(Navigation, Location, Yaw, ChangeDirection)
    local Map = Navigation.mMap
    local Floor = Map:WorldToCell(Location).Floor
    local FloorData = Map.Floors[Floor]
    if FloorData == nil then
        return nil
    end
    local Heading = math.floor(Yaw / 90.0 + 0.5) % 4
    local Angle = (Heading * 90.0 - Yaw + 180.0) % 360.0 - 180.0
    if math.abs(Angle) > 0.01 and not Navigation:CanTurn(Location, Floor) then
        return nil
    end

    local GridX = math.floor((Location.Y - Map.Origin.Y) / Navigation.mSpacing + 0.5)
    local GridY = math.floor((Map.Origin.X - Location.X) / Navigation.mSpacing + 0.5)
    local Start = nil
    local BestDistance = math.huge
    local ForwardX = math.cos(math.rad(Heading * 90.0))
    local ForwardY = math.sin(math.rad(Heading * 90.0))
    for Y = GridY - 1, GridY + 1 do
        for X = GridX - 1, GridX + 1 do
            local Position = Navigation:ToWorld(Floor, X, Y)
            local Distance = (Position.X - Location.X) ^ 2 + (Position.Y - Location.Y) ^ 2
            local ForwardDistance = (Position.X - Location.X) * ForwardX + (Position.Y - Location.Y) * ForwardY
            if Distance < BestDistance and (not ChangeDirection or ForwardDistance >= -0.001) and Navigation:IsNodeClear(Floor, X, Y, Heading) and Navigation:CanMove(Location, Position, Floor, Heading) then
                Start = { Floor = Floor, X = X, Y = Y, Heading = Heading }
                BestDistance = Distance
            end
        end
    end
    return Start
end

local function ToSearchCell(X, Y, Heading)
    return { Floor = Heading + 1, X = X * 2 + 1, Y = Y * 2 + 1 }
end

local function IsStepClear(Navigation, Floor, X, Y, DeltaX, DeltaY, Heading)
    local ExtentX = Heading % 2 == 0 and Navigation.mHalfLength or Navigation.mHalfWidth
    local ExtentY = Heading % 2 == 0 and Navigation.mHalfWidth or Navigation.mHalfLength
    return IsAreaClear(Navigation, Floor, Navigation.mMap.Origin.X - (Y + DeltaY * 0.5) * Navigation.mSpacing, Navigation.mMap.Origin.Y + (X + DeltaX * 0.5) * Navigation.mSpacing, ExtentX + math.abs(DeltaY) * Navigation.mSpacing * 0.5, ExtentY + math.abs(DeltaX) * Navigation.mSpacing * 0.5)
end

local function GetSearchLinks(Links, Key)
    local Data = getmetatable(Links)
    local BaseLinks = Data.mBaseLinks[Key]
    local GoalLinks = Data.mGoalLinks[Key]
    local ExcludedLinks = Data.mExcludedLinks[Key]
    if GoalLinks == nil and ExcludedLinks == nil then
        return BaseLinks
    end
    local Result = {}
    for _, Link in ipairs(BaseLinks or {}) do
        if ExcludedLinks == nil or Link.Key ~= ExcludedLinks[1].Key then
            Result[#Result + 1] = Link
        end
    end
    for _, Link in ipairs(GoalLinks or {}) do
        Result[#Result + 1] = Link
    end
    rawset(Links, Key, Result)
    return Result
end

local function IsSearchNodeClear(SearchMap, Layer, Column, Row)
    if Layer == 5 then
        return Column == SearchMap.mGoal.X and Row == SearchMap.mGoal.Y
    end
    if Layer < 1 or Layer > 4 or Column % 2 ~= 1 or Row % 2 ~= 1 then
        return false
    end
    local Navigation = SearchMap.mNavigation
    local Start = SearchMap.mStart
    local FloorData = Navigation.mMap.Floors[Start.Floor]
    local X = (Column - 1) * 0.5
    local Y = (Row - 1) * 0.5
    local Heading = Layer - 1
    if X < 0 or X > (FloorData.Width - 1) * 2 or Y < 0 or Y > (FloorData.Height - 1) * 2 or not Navigation:IsNodeClear(Start.Floor, X, Y, Heading) then
        return false
    end

    if SearchMap.mTargetLocation ~= nil then
        local Distance = (Navigation.mMap.Origin.X - Y * Navigation.mSpacing - SearchMap.mTargetLocation.X) ^ 2 + (Navigation.mMap.Origin.Y + X * Navigation.mSpacing - SearchMap.mTargetLocation.Y) ^ 2
        if Distance < SearchMap.mClosestDistance then
            SearchMap.mClosestCell = ToSearchCell(X, Y, Heading)
            SearchMap.mClosestDistance = Distance
        end
    end

    local BaseMap = SearchMap.mBaseMap
    local Key = PositionKey(Layer, Column, Row)
    if not BaseMap.mLinked[Key] then
        BaseMap.mLinked[Key] = true
        local Cell = ToSearchCell(X, Y, Heading)
        local Direction = Directions[Layer]
        if IsStepClear(Navigation, Start.Floor, X, Y, Direction.X, Direction.Y, Heading) then
            BaseMap:AddLink(Cell, ToSearchCell(X + Direction.X, Y + Direction.Y, Heading), Navigation.mSpacing, true)
        end
        if Navigation:CanTurnAtNode(Start.Floor, X, Y) then
            BaseMap:AddLink(Cell, ToSearchCell(X, Y, (Heading + 1) % 4), Navigation.mSpacing, true)
            BaseMap:AddLink(Cell, ToSearchCell(X, Y, (Heading + 3) % 4), Navigation.mSpacing, true)
        else
            if IsStepClear(Navigation, Start.Floor, X, Y, -Direction.X, -Direction.Y, Heading) then
                BaseMap:AddLink(Cell, ToSearchCell(X - Direction.X, Y - Direction.Y, Heading), Navigation.mSpacing, true)
            end
        end
    end
    return true
end

local function FindRoute(Navigation, Start, X, Y, ChangeDirection, TargetLocation)
    local FloorData = Navigation.mMap.Floors[Start.Floor]
    X = math.max(0, math.min((FloorData.Width - 1) * 2, X))
    Y = math.max(0, math.min((FloorData.Height - 1) * 2, Y))
    local SearchMap = MapModule.New({ CellSize = Navigation.mSpacing * 0.5 })
    SearchMap.mNavigation = Navigation
    SearchMap.mStart = Start
    SearchMap.mGoal = { Floor = 5, X = X * 2 + 1, Y = Y * 2 + 1 }
    SearchMap.mTargetLocation = TargetLocation
    SearchMap.mClosestCell = nil
    SearchMap.mClosestDistance = math.huge
    local BaseMap = Navigation.mSearchMaps[Start.Floor]
    if BaseMap == nil then
        BaseMap = MapModule.New({ CellSize = Navigation.mSpacing * 0.5 })
        BaseMap.mLinked = {}
        Navigation.mSearchMaps[Start.Floor] = BaseMap
    end
    SearchMap.mBaseMap = BaseMap
    local GoalMap = MapModule.New()
    for Heading = 0, 3 do
        GoalMap:AddLink(ToSearchCell(X, Y, Heading), SearchMap.mGoal, 0.0, true)
    end
    local ExcludedMap = MapModule.New()
    if ChangeDirection then
        local Direction = Directions[Start.Heading + 1]
        ExcludedMap:AddLink(ToSearchCell(Start.X, Start.Y, Start.Heading), ToSearchCell(Start.X + Direction.X, Start.Y + Direction.Y, Start.Heading), 0.0, true)
    end
    SearchMap.Links = setmetatable({}, { __index = GetSearchLinks, mBaseLinks = BaseMap.Links, mGoalLinks = GoalMap.Links, mExcludedLinks = ExcludedMap.Links })
    SearchMap.IsWalkableAt = IsSearchNodeClear
    local StartCell = ToSearchCell(Start.X, Start.Y, Start.Heading)
    SearchMap:IsWalkable(StartCell)
    local function GetFallbackCell()
        return SearchMap.mClosestCell
    end
    local Options = { bAllowDiagonal = false, MaxNodes = (FloorData.Width * 2 - 1) * (FloorData.Height * 2 - 1) * 4 + 1, GetFallbackCell = TargetLocation ~= nil and GetFallbackCell or nil }
    local Cells, Result, Reason = SearchMap:FindPath(StartCell, SearchMap.mGoal, Options)
    if Cells == nil then
        return nil, Result
    end

    local Path = {}
    for _, Cell in ipairs(Cells) do
        if Cell.Floor <= 4 then
            local Point = { Location = Navigation:ToWorld(Start.Floor, (Cell.X - 1) * 0.5, (Cell.Y - 1) * 0.5), Floor = Start.Floor, Heading = Cell.Floor - 1 }
            local Count = #Path
            if Count >= 2 and Path[Count].Heading == Point.Heading and Path[Count - 1].Heading == Point.Heading then
                Path[Count] = Point
            else
                Path[#Path + 1] = Point
            end
        end
    end
    return Path, Reason
end

local function TrimPath(Path, Location)
    if Path == nil then
        return nil
    end
    while #Path > 1 and math.abs(Path[1].Location.X - Location.X) <= 0.001 and math.abs(Path[1].Location.Y - Location.Y) <= 0.001 and Path[1].Location.X == Path[2].Location.X and Path[1].Location.Y == Path[2].Location.Y do
        table.remove(Path, 1)
    end
    return Path
end

local function CopyPath(Path)
    if Path == nil then
        return nil
    end
    local Copy = {}
    for Index, Point in ipairs(Path) do
        Copy[Index] = Point
    end
    return Copy
end

function EnemyNavigation.FindPatrolPath(Navigation, Location, Yaw, MinimumDistance, ChangeDirection)
    local Start = FindStart(Navigation, Location, Yaw, ChangeDirection)
    if Start == nil then
        return nil
    end
    local FloorData = Navigation.mMap.Floors[Start.Floor]
    local Range = math.ceil(math.max(MinimumDistance, Navigation.mMap.CellSize) * 2.0 / Navigation.mSpacing) + 4
    local Direction = Directions[Start.Heading + 1]
    local Goals = {}
    local NearbyGoals = {}
    local SideGoals = {}
    local NearbySideGoals = {}
    for Y = math.max(0, Start.Y - Range), math.min((FloorData.Height - 1) * 2, Start.Y + Range) do
        for X = math.max(0, Start.X - Range), math.min((FloorData.Width - 1) * 2, Start.X + Range) do
            local Distance = ((X - Start.X) ^ 2 + (Y - Start.Y) ^ 2) * Navigation.mSpacing * Navigation.mSpacing
            if Distance >= Navigation.mMap.CellSize * Navigation.mMap.CellSize and Navigation:CanTurnAtNode(Start.Floor, X, Y) then
                local Goal = { X = X, Y = Y }
                local Sideways = math.abs((X - Start.X) * Direction.Y - (Y - Start.Y) * Direction.X) > math.abs((X - Start.X) * Direction.X + (Y - Start.Y) * Direction.Y)
                NearbyGoals[#NearbyGoals + 1] = Goal
                if Sideways then
                    NearbySideGoals[#NearbySideGoals + 1] = Goal
                end
                if Distance >= MinimumDistance * MinimumDistance then
                    Goals[#Goals + 1] = Goal
                    if Sideways then
                        SideGoals[#SideGoals + 1] = Goal
                    end
                end
            end
        end
    end
    local Candidates = ChangeDirection and { SideGoals, NearbySideGoals, Goals, NearbyGoals } or { Goals, NearbyGoals }
    local AttemptedGoals = {}
    local AttemptsPerList = MaximumPatrolAttempts / #Candidates
    for _, List in ipairs(Candidates) do
        local Attempts = 0
        while #List > 0 and Attempts < AttemptsPerList do
            local Index = math.random(1, #List)
            local Goal = List[Index]
            List[Index] = List[#List]
            List[#List] = nil
            if not AttemptedGoals[Goal] then
                AttemptedGoals[Goal] = true
                Attempts = Attempts + 1
                local Path = FindRoute(Navigation, Start, Goal.X, Goal.Y, ChangeDirection, nil)
                if Path ~= nil then
                    return TrimPath(Path, Location)
                end
            end
        end
    end
    return nil
end

function EnemyNavigation.FindChasePath(Navigation, Location, Yaw, TargetLocation)
    TargetLocation = { X = TargetLocation.X, Y = TargetLocation.Y, Z = TargetLocation.Z }
    local Start = FindStart(Navigation, Location, Yaw, false)
    if Start == nil or Navigation.mMap:WorldToCell(TargetLocation).Floor ~= Start.Floor then
        return nil
    end
    local X = math.floor((TargetLocation.Y - Navigation.mMap.Origin.Y) / Navigation.mSpacing + 0.5)
    local Y = math.floor((Navigation.mMap.Origin.X - TargetLocation.X) / Navigation.mSpacing + 0.5)
    local StartKey = PositionKey(Start.Floor, Start.X, Start.Y) * 4 + Start.Heading
    local FailedChase = Navigation.mFailedChase
    if FailedChase ~= nil and FailedChase.mStartKey == StartKey and FailedChase.mTargetX == X and FailedChase.mTargetY == Y then
        return TrimPath(CopyPath(FailedChase.mPath), Location)
    end
    Navigation.mFailedChase = nil
    local FailedRequest = { mStartKey = StartKey, mTargetX = X, mTargetY = Y, mPath = nil }
    local FloorData = Navigation.mMap.Floors[Start.Floor]
    X = math.max(0, math.min((FloorData.Width - 1) * 2, X))
    Y = math.max(0, math.min((FloorData.Height - 1) * 2, Y))
    if not Navigation:IsNodeClear(Start.Floor, X, Y, 0) and not Navigation:IsNodeClear(Start.Floor, X, Y, 1) then
        local BestDistance = math.huge
        local Goal = nil
        local Range = math.ceil((math.max(Navigation.mHalfLength, Navigation.mHalfWidth) + Navigation.mMap.CellSize * 3.0) / Navigation.mSpacing)
        for Row = math.max(0, Y - Range), math.min((FloorData.Height - 1) * 2, Y + Range) do
            for Column = math.max(0, X - Range), math.min((FloorData.Width - 1) * 2, X + Range) do
                local Distance = (Navigation.mMap.Origin.X - Row * Navigation.mSpacing - TargetLocation.X) ^ 2 + (Navigation.mMap.Origin.Y + Column * Navigation.mSpacing - TargetLocation.Y) ^ 2
                if Distance < BestDistance and Navigation:CanTurnAtNode(Start.Floor, Column, Row) then
                    BestDistance = Distance
                    Goal = { X = Column, Y = Row }
                end
            end
        end
        if Goal == nil then
            Navigation.mFailedChase = FailedRequest
            return nil
        end
        X = Goal.X
        Y = Goal.Y
    end
    local Path, Reason = FindRoute(Navigation, Start, X, Y, false, TargetLocation)
    if Reason == "Unreachable" then
        FailedRequest.mPath = CopyPath(Path)
        Navigation.mFailedChase = FailedRequest
    end
    return TrimPath(Path, Location)
end

return EnemyNavigation
