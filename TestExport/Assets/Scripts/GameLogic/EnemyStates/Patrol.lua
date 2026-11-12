local Common = require("EnemyStates.Common")
local GameManager = require("GameManager")
local EnemyNavigation = require("EnemyNavigation")
local Door = require("Door")
local Patrol = {}

local WaypointTolerance = 0.001
local AngleTolerance = 0.01
local BlockedDuration = 1.0

local function ClearPath(Data)
    Data.mPatrolMap = nil
    Data.mPatrolPath = nil
    Data.mPatrolPathIndex = 1
    Data.mPatrolHeightOffset = 0.0
    Data.mPatrolBlockedTime = 0.0
    Data.mPatrolReplanElapsed = 0.0
    Data.mPatrolTurning = false
end

local function BuildPath(Context, Map)
    local Data = Context.mData
    ClearPath(Data)
    if Map == nil then
        return
    end

    local Navigation = Data.mPatrolNavigation
    if Navigation == nil or not Navigation:IsCurrent(Map) then
        Navigation = EnemyNavigation.New(Map, Data.mPatrolExtent or FVector.new(0.0, 0.0, 0.0), Data.mPatrolWallMargin or 0.0)
        Data.mPatrolNavigation = Navigation
    end
    local Location = Context.mOwner:GetActorLocation()
    local StartCell = Map:WorldToCell(Location)
    Data.mPatrolMap = Map
    Data.mPatrolPath = Navigation:FindPatrolPath(Location, Context.mOwner:GetActorRotation().Yaw, Data.mPatrolMinimumDistance or Map.CellSize * 4.0)
    Data.mPatrolHeightOffset = Location.Z - Map:CellToWorld(StartCell).Z
end

local function FinishPatrol(Context)
    Door.CancelEnemyWait(Context)
    ClearPath(Context.mData)
    Context.mMachine:RequestState("Idle")
end

local function MoveOwner(Context, Delta, Rotation)
    if not Context.mOwner:Move(Delta, Rotation) then
        error("[EnemyFSM] Patrol requires an actor with a root component")
    end
    return not Context.mData.mPatrolInterrupted and Context.mMachine:IsRunning()
end

local function ReplanPath(Context)
    local Data = Context.mData
    local Interval = Data.mPatrolReplanInterval or 2.5
    if Interval <= 0.0 or (Data.mPatrolReplanElapsed or 0.0) < Interval or Data.mPatrolTurning then
        return
    end

    local Owner = Context.mOwner
    local Location = Owner:GetActorLocation()
    local Rotation = Owner:GetActorRotation()
    local Point = Data.mPatrolPath[Data.mPatrolPathIndex]
    if Point == nil or math.abs((Point.Heading * 90.0 - Rotation.Yaw + 180.0) % 360.0 - 180.0) > AngleTolerance or not Data.mPatrolNavigation:CanTurn(Location, Point.Floor) then
        return
    end

    Data.mPatrolReplanElapsed = 0.0
    local Path = Data.mPatrolNavigation:FindPatrolPath(Location, Rotation.Yaw, Data.mPatrolMinimumDistance or Data.mPatrolMap.CellSize * 4.0, true)
    if Path ~= nil then
        Data.mPatrolPath = Path
        Data.mPatrolPathIndex = 1
    end
end

function Patrol.Enter(Context, PreviousState)
    local Data = Context.mData
    local Map = GameManager.GetMap()
    Data.mPatrolInterrupted = false
    if Data.mPatrolMap ~= Map or Data.mPatrolPath == nil or not Data.mPatrolNavigation:IsCurrent(Map) then
        BuildPath(Context, Map)
    end
    Common.ReportState(Context, PreviousState, "Patrol")
end

function Patrol.Tick(Context, DeltaTime)
    if Common.TryChase(Context) then
        return
    end
    local Data = Context.mData
    local Owner = Context.mOwner
    local Map = GameManager.GetMap()
    if Data.mPatrolMap ~= Map or (Map ~= nil and Data.mPatrolNavigation ~= nil and not Data.mPatrolNavigation:IsCurrent(Map)) then
        BuildPath(Context, Map)
    end

    if Map == nil or Data.mPatrolPath == nil then
        FinishPatrol(Context)
        return
    end

    local Navigation = Data.mPatrolNavigation
    ReplanPath(Context)
    local RemainingTime = DeltaTime
    local TurnSpeed = Data.mPatrolTurnSpeed or 360.0
    while Data.mPatrolPathIndex <= #Data.mPatrolPath do
        local Point = Data.mPatrolPath[Data.mPatrolPathIndex]
        local Target = Point.Location
        local Location = Owner:GetActorLocation()
        local Rotation = Owner:GetActorRotation()
        local TargetYaw = Point.Heading * 90.0
        local Angle = (TargetYaw - Rotation.Yaw + 180.0) % 360.0 - 180.0
        if math.abs(Angle) > AngleTolerance then
            Door.CancelEnemyWait(Context)
            if RemainingTime <= 0.0 or TurnSpeed <= 0.0 then
                return
            end
            if not Navigation:CanTurn(Location, Point.Floor) then
                FinishPatrol(Context)
                return
            end

            local Turn = math.min(math.abs(Angle), TurnSpeed * RemainingTime)
            Data.mPatrolTurning = true
            local NewYaw = Rotation.Yaw + (Angle < 0.0 and -Turn or Turn)
            if not MoveOwner(Context, FVector.new(0.0, 0.0, 0.0), FRotator.new(Rotation.Pitch, NewYaw, Rotation.Roll)) then
                return
            end
            RemainingTime = RemainingTime - Turn / TurnSpeed
        else
            if Data.mPatrolTurning then
                Data.mPatrolTurning = false
                Data.mPatrolReplanElapsed = 0.0
            end
            local DeltaX = Target.X - Location.X
            local DeltaY = Target.Y - Location.Y
            local DeltaZ = Target.Z + Data.mPatrolHeightOffset - Location.Z
            local Distance = math.sqrt(DeltaX * DeltaX + DeltaY * DeltaY + DeltaZ * DeltaZ)
            if Distance <= WaypointTolerance then
                Data.mPatrolPathIndex = Data.mPatrolPathIndex + 1
            elseif RemainingTime <= 0.0 or Data.mPatrolSpeed <= 0.0 then
                return
            else
                local Step = math.min(Distance, Data.mPatrolSpeed * RemainingTime)
                local Scale = Step / Distance
                local Destination = FVector.new(Location.X + DeltaX * Scale, Location.Y + DeltaY * Scale, Location.Z + DeltaZ * Scale)
                if not Navigation:CanMove(Location, Destination, Point.Floor, Point.Heading) then
                    FinishPatrol(Context)
                    return
                end
                local Fraction, Waiting = Door.AdvanceEnemy(Context, Location, Destination, Navigation.mHalfLength, Navigation.mHalfWidth, TargetYaw, Data.mPatrolSpeed, RemainingTime)
                if Waiting then
                    Data.mPatrolBlockedTime = 0.0
                    if Fraction > 0.0 then
                        MoveOwner(Context, FVector.new(DeltaX * Scale * Fraction, DeltaY * Scale * Fraction, DeltaZ * Scale * Fraction), FRotator.new(Rotation.Pitch, TargetYaw, Rotation.Roll))
                    end
                    return
                end
                if not MoveOwner(Context, FVector.new(DeltaX * Scale, DeltaY * Scale, DeltaZ * Scale), FRotator.new(Rotation.Pitch, TargetYaw, Rotation.Roll)) then
                    return
                end

                local NewLocation = Owner:GetActorLocation()
                local RemainingX = Target.X - NewLocation.X
                local RemainingY = Target.Y - NewLocation.Y
                local RemainingZ = Target.Z + Data.mPatrolHeightOffset - NewLocation.Z
                local NewDistance = math.sqrt(RemainingX * RemainingX + RemainingY * RemainingY + RemainingZ * RemainingZ)
                if Distance - NewDistance < Step - WaypointTolerance then
                    Data.mPatrolBlockedTime = Data.mPatrolBlockedTime + DeltaTime
                    if Data.mPatrolBlockedTime >= BlockedDuration then
                        FinishPatrol(Context)
                    end
                    return
                end

                Data.mPatrolBlockedTime = 0.0
                Data.mPatrolReplanElapsed = (Data.mPatrolReplanElapsed or 0.0) + Step / Data.mPatrolSpeed
                RemainingTime = RemainingTime - Step / Data.mPatrolSpeed
            end
        end
    end

    FinishPatrol(Context)
end

function Patrol.Exit(Context, NextState)
    Door.CancelEnemyWait(Context)
end

function Patrol.HandleEvent(Context, EventName, ...)
    Common.HandleInterruption(Context, EventName, ...)
end

return Patrol
