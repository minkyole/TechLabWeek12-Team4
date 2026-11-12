local GameManager = require("GameManager")
local Common = {}

function Common.ReportState(Context, PreviousState, StateName)
    Log("[FSM]", Context.mOwner:GetName(), PreviousState or "<none>", "->", StateName)
end

function Common.HandleInterruption(Context, EventName, Location, Radius)
    if EventName == "PlayerFootstep" then
        Common.TryHearFootstep(Context, Location, Radius)
    elseif EventName == "Stun" and Context.mMachine:RequestState("Stunned") then
        Context.mData.mResumeState = Context.mMachine:GetCurrentState()
        Context.mData.mPatrolInterrupted = true
        Context.mData.mChaseInterrupted = true
    end
end

function Common.TryChase(Context)
    if GameManager.IsLightOn() and GameManager.GetPlayer() ~= nil then
        return Context.mMachine:RequestState("Chase")
    end
    return false
end

function Common.TryHearFootstep(Context, Location, Radius)
    if GameManager.GetPlayer() == nil then
        return false
    end
    if GameManager.IsLightOn() then
        local StartedChase = Common.TryChase(Context)
        if StartedChase then
            Context.mData.mPatrolInterrupted = true
        end
        return StartedChase
    end

    local OwnerLocation = Context.mOwner:GetActorLocation()
    local DeltaX = Location.X - OwnerLocation.X
    local DeltaY = Location.Y - OwnerLocation.Y
    if DeltaX * DeltaX + DeltaY * DeltaY > Radius * Radius then
        return false
    end

    local Map = GameManager.GetMap()
    if Map ~= nil then
        local OwnerCell = Map:WorldToCell(OwnerLocation)
        local SoundCell = Map:WorldToCell(Location)
        if OwnerCell.Floor ~= SoundCell.Floor then
            return false
        end
        local Path, Distance = Map:FindPath(OwnerCell, SoundCell)
        if Path == nil or Distance > Radius then
            return false
        end
    end

    local Data = Context.mData
    if Context.mMachine:GetCurrentState() == "Chase" then
        Data.mLastKnownPlayerLocation = FVector.new(Location.X, Location.Y, Location.Z)
        return true
    end
    if Context.mMachine:RequestState("Chase") then
        Data.mLastKnownPlayerLocation = FVector.new(Location.X, Location.Y, Location.Z)
        Data.mPatrolInterrupted = true
        return true
    end
    return false
end

return Common
