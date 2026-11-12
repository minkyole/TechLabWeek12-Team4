local Common = require("EnemyStates.Common")
local GameManager = require("GameManager")
local EnemyNavigation = require("EnemyNavigation")
local Search = {}

function Search.Enter(Context, PreviousState)
    Context.mData.mSearchStartYaw = Context.mOwner:GetActorRotation().Yaw
    Common.ReportState(Context, PreviousState, "Search")
end

function Search.Tick(Context, DeltaTime)
    if Common.TryChase(Context) then
        return
    end

    local Data = Context.mData
    local Duration = Data.mSearchDuration or 4.0
    local ElapsedTime = Context.mMachine:GetElapsedTime()
    if GameManager.GetPlayer() == nil or ElapsedTime >= Duration then
        Context.mMachine:RequestState("Patrol")
        return
    end

    local Map = GameManager.GetMap()
    if Map == nil or DeltaTime <= 0.0 then
        return
    end

    local Navigation = Data.mChaseNavigation
    if Navigation == nil or not Navigation:IsCurrent(Map) then
        Navigation = EnemyNavigation.New(Map, Data.mPatrolExtent or FVector.new(0.0, 0.0, 0.0), Data.mPatrolWallMargin or 0.0)
        Data.mChaseNavigation = Navigation
    end
    local Owner = Context.mOwner
    local Location = Owner:GetActorLocation()
    if not Navigation:CanTurn(Location, Map:WorldToCell(Location).Floor) then
        return
    end

    local Rotation = Owner:GetActorRotation()
    local TargetYaw = Data.mSearchStartYaw + math.sin(ElapsedTime / Duration * math.pi * 2.0) * 60.0
    local Angle = (TargetYaw - Rotation.Yaw + 180.0) % 360.0 - 180.0
    local MaximumTurn = (Data.mSearchTurnSpeed or 90.0) * DeltaTime
    local Turn = math.max(-MaximumTurn, math.min(MaximumTurn, Angle))
    if not Owner:Move(FVector.new(0.0, 0.0, 0.0), FRotator.new(Rotation.Pitch, Rotation.Yaw + Turn, Rotation.Roll)) then
        error("[EnemyFSM] Search requires an actor with a root component")
    end
end

function Search.HandleEvent(Context, EventName, ...)
    Common.HandleInterruption(Context, EventName, ...)
end

return Search
