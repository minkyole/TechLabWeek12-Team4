local GameManager = require("GameManager")
local EnemyPursuit = require("EnemyPursuit")
local Door = require("Door")
local Observed = {}
local Stalking = {}
local Stunned = {}

local function IsObserved(Context)
    local Player = GameManager.GetPlayer()
    local Camera = Player and Player:GetCamera() or nil
    return Camera == nil or Camera:CanSeeActor(Context.mOwner)
end

local function ReportState(Context, PreviousState, StateName)
    Log("[Watcher]", Context.mOwner:GetName(), PreviousState or "<none>", "->", StateName)
end

local function HandleEvent(Context, EventName)
    if EventName == "Stun" and Context.mMachine:RequestState("Stunned") then
        Context.mData.mChaseInterrupted = true
    end
end

function Observed.Enter(Context, PreviousState)
    Context.mData.mUnobservedTime = 0.0
    ReportState(Context, PreviousState, "Observed")
end

function Observed.Tick(Context, DeltaTime)
    local Data = Context.mData
    if IsObserved(Context) then
        Data.mUnobservedTime = 0.0
        return
    end
    Data.mUnobservedTime = Data.mUnobservedTime + DeltaTime
    if Data.mUnobservedTime >= (Data.mUnobservedDelay or 0.75) then
        Context.mMachine:RequestState("Stalking")
    end
end

Observed.HandleEvent = HandleEvent

function Stalking.Enter(Context, PreviousState)
    Context.mData.mChaseInterrupted = false
    EnemyPursuit.BuildPath(Context, GameManager.GetMap(), GameManager.GetPlayerLocation())
    ReportState(Context, PreviousState, "Stalking")
end

function Stalking.Tick(Context, DeltaTime)
    if IsObserved(Context) then
        Context.mMachine:RequestState("Observed")
        return
    end

    local Data = Context.mData
    local Map = GameManager.GetMap()
    local Target = GameManager.GetPlayerLocation()
    local TargetMoved = Target ~= nil and EnemyPursuit.HasTargetMoved(Data, Map, Target)
    Data.mChaseReplanElapsed = Data.mChaseReplanElapsed + DeltaTime
    if Data.mChaseMap ~= Map or (Map ~= nil and (Data.mChaseNavigation == nil or not Data.mChaseNavigation:IsCurrent(Map))) or ((Data.mChasePath == nil or TargetMoved) and Data.mChaseReplanElapsed >= (Data.mChaseReplanInterval or 0.35) and not Data.mChaseTurning) then
        EnemyPursuit.BuildPath(Context, Map, Target)
    end
    if Map == nil or Data.mChasePath == nil then
        Door.CancelEnemyWait(Context)
        return
    end

    local RemainingTime = math.min(DeltaTime, 0.25)
    while RemainingTime > 0.0 and GameManager.IsPlaying() do
        local StepTime = math.min(RemainingTime, 1.0 / 60.0)
        local Finished = EnemyPursuit.Advance(Context, StepTime)
        if Data.mChaseInterrupted or not Context.mMachine:IsRunning() then
            return
        end
        if IsObserved(Context) then
            Context.mMachine:RequestState("Observed")
            return
        end
        if Finished then
            Data.mChasePath = nil
            return
        end
        if Data.mChasePath == nil then
            return
        end
        RemainingTime = RemainingTime - StepTime
    end
end

function Stalking.Exit(Context, NextState)
    Door.CancelEnemyWait(Context)
    Context.mData.mChasePath = nil
end

Stalking.HandleEvent = HandleEvent

function Stunned.Enter(Context, PreviousState)
    ReportState(Context, PreviousState, "Stunned")
end

function Stunned.Tick(Context, DeltaTime)
    if Context.mMachine:GetElapsedTime() >= (Context.mData.mStunDuration or 1.5) then
        Context.mMachine:RequestState(IsObserved(Context) and "Observed" or "Stalking")
    end
end

return { Observed = Observed, Stalking = Stalking, Stunned = Stunned }
