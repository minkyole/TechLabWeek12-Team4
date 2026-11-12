local Common = require("EnemyStates.Common")
local GameManager = require("GameManager")
local EnemyPursuit = require("EnemyPursuit")
local Door = require("Door")
local Chase = {}

local function UpdateLastKnownLocation(Data)
    local Location = GameManager.IsLightOn() and GameManager.GetPlayerLocation() or nil
    if Location ~= nil then
        Data.mLastKnownPlayerLocation = FVector.new(Location.X, Location.Y, Location.Z)
    end
    return Data.mLastKnownPlayerLocation
end

function Chase.Enter(Context, PreviousState)
    Context.mData.mChaseInterrupted = false
    EnemyPursuit.BuildPath(Context, GameManager.GetMap(), UpdateLastKnownLocation(Context.mData))
    Common.ReportState(Context, PreviousState, "Chase")
end

function Chase.Tick(Context, DeltaTime)
    local Data = Context.mData
    local TargetLocation = UpdateLastKnownLocation(Data)
    if GameManager.GetPlayer() == nil or TargetLocation == nil then
        Context.mMachine:RequestState("Patrol")
        return
    end

    local Map = GameManager.GetMap()
    local LightOn = GameManager.IsLightOn()
    local TargetMoved = EnemyPursuit.HasTargetMoved(Data, Map, TargetLocation)
    Data.mChaseReplanElapsed = Data.mChaseReplanElapsed + DeltaTime
    if Data.mChaseMap ~= Map or (Map ~= nil and (Data.mChaseNavigation == nil or not Data.mChaseNavigation:IsCurrent(Map))) or ((TargetMoved or Data.mChasePath == nil) and Data.mChaseReplanElapsed >= (Data.mChaseReplanInterval or 0.5) and not Data.mChaseTurning) then
        EnemyPursuit.BuildPath(Context, Map, TargetLocation)
        TargetMoved = false
    end
    if Map == nil or Data.mChasePath == nil then
        Door.CancelEnemyWait(Context)
        if not LightOn then
            Context.mMachine:RequestState("Search")
        end
        return
    end

    if not EnemyPursuit.Advance(Context, DeltaTime) then
        return
    end

    if not LightOn then
        local LatestLocation = Data.mLastKnownPlayerLocation
        if TargetMoved or LatestLocation.X ~= TargetLocation.X or LatestLocation.Y ~= TargetLocation.Y or LatestLocation.Z ~= TargetLocation.Z then
            EnemyPursuit.BuildPath(Context, Map, LatestLocation)
        else
            Context.mMachine:RequestState("Search")
        end
    end
end

function Chase.Exit(Context, NextState)
    Door.CancelEnemyWait(Context)
    Context.mData.mPatrolPath = nil
    Context.mData.mChasePath = nil
end

function Chase.HandleEvent(Context, EventName, ...)
    Common.HandleInterruption(Context, EventName, ...)
end

return Chase
