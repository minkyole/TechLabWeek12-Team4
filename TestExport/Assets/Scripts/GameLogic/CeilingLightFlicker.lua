local GameManager = require("GameManager")
local CeilingLightFlicker = {}

CeilingLightFlicker.Config = { InitialMinDelay = 0.6, InitialMaxDelay = 2.4, MinInterval = 2.5, MaxInterval = 7.0, MinCooldown = 0.7, MaxCooldown = 1.8, MinDimScale = 0.015, MaxDimScale = 0.12, MinDimDuration = 0.06, MaxDimDuration = 0.14, MinPulseGap = 0.04, MaxPulseGap = 0.16, MinPulseCount = 2, MaxPulseCount = 5, LongDimChance = 0.35, MinLongDimDuration = 0.18, MaxLongDimDuration = 0.38, MinRecoveryScale = 0.55, MaxRecoveryScale = 0.9, FullSoundDistance = 3.0, SilentDistance = 12.0, SoundVolume = 0.14, Sound = "Assets/Sounds/CeilingLightFlicker.wav" }
CeilingLightFlicker.Config.FlickerRadius = 8.0
CeilingLightFlicker.Config.NearbyFixtureCount = 3
CeilingLightFlicker.Config.MaxHeightAbovePlayer = 5.0
CeilingLightFlicker.Config.NearbyCheckInterval = 0.25

local Runtime = { mStates = {}, mActiveState = nil }

local function RandomRange(Minimum, Maximum)
    return Minimum + math.random() * (Maximum - Minimum)
end

local function DistanceVolume(State)
    local PlayerLocation = GameManager.GetPlayerLocation()
    if PlayerLocation == nil then
        return 0.0
    end
    local Location = State.mLight:GetWorldLocation()
    local Distance = math.sqrt((PlayerLocation.X - Location.X) ^ 2 + (PlayerLocation.Y - Location.Y) ^ 2 + (PlayerLocation.Z - Location.Z) ^ 2)
    local Config = CeilingLightFlicker.Config
    return math.max(0.0, math.min(1.0, (Config.SilentDistance - Distance) / (Config.SilentDistance - Config.FullSoundDistance)))
end

local function NearbyDistanceSquared(State, PlayerLocation)
    if not State.mRunning or not State.mLight:IsVisible() then
        return math.huge
    end
    local Location = State.mLight:GetWorldLocation()
    local Config = CeilingLightFlicker.Config
    local Height = Location.Z - PlayerLocation.Z
    local DistanceSquared = (PlayerLocation.X - Location.X) ^ 2 + (PlayerLocation.Y - Location.Y) ^ 2
    if Height < 0.0 or Height > Config.MaxHeightAbovePlayer or DistanceSquared > Config.FlickerRadius ^ 2 then
        return math.huge
    end
    return DistanceSquared
end

local function CanFlicker(State)
    local PlayerLocation = GameManager.GetPlayerLocation()
    if not GameManager.IsPlaying() or PlayerLocation == nil then
        return false
    end
    local DistanceSquared = NearbyDistanceSquared(State, PlayerLocation)
    if DistanceSquared == math.huge then
        return false
    end
    local CloserCount = 0
    for Name, OtherState in pairs(Runtime.mStates) do
        if OtherState ~= State then
            local OtherDistanceSquared = NearbyDistanceSquared(OtherState, PlayerLocation)
            if OtherDistanceSquared < DistanceSquared or OtherDistanceSquared == DistanceSquared and Name < State.mName then
                CloserCount = CloserCount + 1
            end
        end
    end
    return CloserCount < CeilingLightFlicker.Config.NearbyFixtureCount
end

local function StopSoundEffect(State)
    if State.mSoundHandle ~= 0 then
        StopSound(State.mSoundHandle)
        State.mSoundHandle = 0
    end
end

local function Flicker(State)
    local Config = CeilingLightFlicker.Config
    local PulseCount = math.random(Config.MinPulseCount, Config.MaxPulseCount)
    Runtime.mActiveState = State
    for Pulse = 1, PulseCount do
        if not CanFlicker(State) then
            break
        end
        StopSoundEffect(State)
        State.mSoundHandle = PlaySound(Config.Sound, false, Config.SoundVolume * DistanceVolume(State))
        if State.mSoundHandle ~= 0 then
            SetSoundPitch(State.mSoundHandle, RandomRange(0.94, 1.06))
        end
        State.mLight:SetIntensity(State.mBaseIntensity * RandomRange(Config.MinDimScale, Config.MaxDimScale))
        local LongDim = math.random() < Config.LongDimChance
        Wait(LongDim and RandomRange(Config.MinLongDimDuration, Config.MaxLongDimDuration) or RandomRange(Config.MinDimDuration, Config.MaxDimDuration))
        if not State.mRunning then
            return
        end
        if not CanFlicker(State) then
            break
        end
        if Pulse < PulseCount then
            State.mLight:SetIntensity(State.mBaseIntensity * RandomRange(Config.MinRecoveryScale, Config.MaxRecoveryScale))
            Wait(RandomRange(Config.MinPulseGap, Config.MaxPulseGap))
        end
    end
    if not State.mRunning then
        return
    end
    State.mLight:SetIntensity(State.mBaseIntensity)
    Wait(RandomRange(Config.MinCooldown, Config.MaxCooldown))
    if Runtime.mActiveState == State then
        Runtime.mActiveState = nil
    end
end

local function Run(State)
    local Config = CeilingLightFlicker.Config
    Wait(RandomRange(Config.InitialMinDelay, Config.InitialMaxDelay))
    while State.mRunning do
        if Runtime.mActiveState == nil and CanFlicker(State) then
            Flicker(State)
            if State.mRunning then
                Wait(RandomRange(Config.MinInterval, Config.MaxInterval))
            end
        else
            Wait(Config.NearbyCheckInterval)
        end
    end
end

function CeilingLightFlicker.Stop(Actor)
    local State = Runtime.mStates[Actor:GetName()]
    if State == nil then
        return
    end
    State.mRunning = false
    State.mLight:SetIntensity(State.mBaseIntensity)
    StopSoundEffect(State)
    Runtime.mStates[Actor:GetName()] = nil
    if Runtime.mActiveState == State then
        Runtime.mActiveState = nil
    end
end

function CeilingLightFlicker.Start(Actor)
    CeilingLightFlicker.Stop(Actor)
    local Light = Actor:GetPointLight()
    if Light == nil or Light:GetIntensity() <= 0.0 then
        return
    end
    local State = { mName = Actor:GetName(), mLight = Light, mBaseIntensity = Light:GetIntensity(), mRunning = true, mSoundHandle = 0 }
    Runtime.mStates[Actor:GetName()] = State
    StartCoroutine(coroutine.create(function()
        Run(State)
    end))
end

function CeilingLightFlicker.Tick(Actor)
    local State = Runtime.mStates[Actor:GetName()]
    if State == nil then
        return
    end
    if not GameManager.IsPlaying() or Runtime.mActiveState == State and not CanFlicker(State) then
        State.mLight:SetIntensity(State.mBaseIntensity)
        StopSoundEffect(State)
    elseif State.mSoundHandle ~= 0 then
        if IsSoundPlaying(State.mSoundHandle) then
            SetSoundVolume(State.mSoundHandle, CeilingLightFlicker.Config.SoundVolume * DistanceVolume(State))
        else
            State.mSoundHandle = 0
        end
    end
end

return CeilingLightFlicker
