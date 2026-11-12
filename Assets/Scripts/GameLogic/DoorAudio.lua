local GameManager = require("GameManager")
local DoorAudio = {}

DoorAudio.Config = { OpenSound = "Assets/Sounds/DoorOpen.wav", CloseSound = "Assets/Sounds/DoorClose.wav", KnockSound = "Assets/Sounds/DoorKnock.wav", OpenVolume = 0.75, CloseVolume = 0.85, KnockVolume = 0.9, FullDistance = 3.0, SilentDistance = 22.0 }

local function GetState(Entry)
    if Entry.mAudio == nil then
        Entry.mAudio = { mMovementHandle = 0, mMovementVolume = 0.0, mKnockHandle = 0, mWaiters = {}, mKnockAttempted = false }
    end
    return Entry.mAudio
end

local function DistanceVolume(Entry)
    local PlayerLocation = GameManager.GetPlayerLocation()
    if PlayerLocation == nil then
        return 0.0
    end
    local Location = Entry.mClosedLocation
    local Distance = math.sqrt((PlayerLocation.X - Location.X) ^ 2 + (PlayerLocation.Y - Location.Y) ^ 2 + (PlayerLocation.Z - Location.Z) ^ 2)
    local Config = DoorAudio.Config
    return math.max(0.0, math.min(1.0, (Config.SilentDistance - Distance) / (Config.SilentDistance - Config.FullDistance)))
end

function DoorAudio.StopKnocking(Entry)
    local State = Entry.mAudio
    if State == nil then
        return
    end
    if State.mKnockHandle ~= 0 then
        StopSound(State.mKnockHandle)
        State.mKnockHandle = 0
    end
    State.mWaiters = {}
    State.mKnockAttempted = false
end

function DoorAudio.PlayMovement(Entry, Open)
    local State = GetState(Entry)
    DoorAudio.StopKnocking(Entry)
    if State.mMovementHandle ~= 0 then
        StopSound(State.mMovementHandle)
    end
    local Config = DoorAudio.Config
    State.mMovementVolume = Open and Config.OpenVolume or Config.CloseVolume
    State.mMovementHandle = PlaySound(Open and Config.OpenSound or Config.CloseSound, false, State.mMovementVolume * DistanceVolume(Entry))
end

function DoorAudio.StopMovement(Entry)
    local State = Entry and Entry.mAudio or nil
    if State ~= nil and State.mMovementHandle ~= 0 then
        StopSound(State.mMovementHandle)
        State.mMovementHandle = 0
    end
end

function DoorAudio.BeginWait(Entry, Owner)
    local State = GetState(Entry)
    State.mWaiters[Owner] = true
    if State.mKnockHandle == 0 and not State.mKnockAttempted then
        State.mKnockAttempted = true
        State.mKnockHandle = PlaySound(DoorAudio.Config.KnockSound, true, DoorAudio.Config.KnockVolume * DistanceVolume(Entry))
    end
end

function DoorAudio.EndWait(Entry, Owner)
    local State = Entry.mAudio
    if State == nil then
        return
    end
    State.mWaiters[Owner] = nil
    if next(State.mWaiters) == nil then
        DoorAudio.StopKnocking(Entry)
    end
end

function DoorAudio.Stop(Entry)
    local State = Entry and Entry.mAudio or nil
    if State == nil then
        return
    end
    DoorAudio.StopKnocking(Entry)
    if State.mMovementHandle ~= 0 then
        StopSound(State.mMovementHandle)
    end
    Entry.mAudio = nil
end

function DoorAudio.Tick(Entry)
    local State = Entry and Entry.mAudio or nil
    if State == nil then
        return
    end
    if not GameManager.IsPlaying() then
        DoorAudio.Stop(Entry)
        return
    end
    local Volume = DistanceVolume(Entry)
    if State.mMovementHandle ~= 0 then
        if IsSoundPlaying(State.mMovementHandle) then
            SetSoundVolume(State.mMovementHandle, State.mMovementVolume * Volume)
        else
            StopSound(State.mMovementHandle)
            State.mMovementHandle = 0
        end
    end
    if State.mKnockHandle ~= 0 then
        SetSoundVolume(State.mKnockHandle, DoorAudio.Config.KnockVolume * Volume)
    end
end

return DoorAudio
