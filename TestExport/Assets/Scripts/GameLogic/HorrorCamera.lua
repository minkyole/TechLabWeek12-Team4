local EnemyAudio = require("EnemyAudio")
local HorrorCamera = {}
local Runtime = { mStates = {} }

HorrorCamera.Config = {
    HeartbeatMinimumFrequency = 0.8,
    HeartbeatMaximumFrequency = 2.0,
    HeartbeatSecondaryPhase = 0.25,
    HeartbeatSecondaryStrength = 0.65,
    HeartbeatMaximumDelay = 0.1,
    HeartbeatPattern = { Type = "Wave", Duration = 0.16, BlendInTime = 0.01, BlendOutTime = 0.11, Z = { Amplitude = 0.025, Frequency = 3.125 }, Pitch = { Amplitude = 1.2, Frequency = 3.125 }, FOV = { Amplitude = 2.0, Frequency = 3.125 } },
    FullBreathingRunDistance = 30.0,
    RecoveryDistancePerSecond = 5.0,
    BreathingBlendSpeed = 6.0,
    BreathingSound = "Assets/Sounds/RecoveryBreathing.wav",
    BreathingMaxVolume = 0.65,
    BreathingPattern = { Type = "Wave", Duration = 0.0, BlendInTime = 0.2, BlendOutTime = 0.4, Z = { Amplitude = 0.06, Frequency = 0.65 }, Pitch = { Amplitude = 2.4, Frequency = 0.65 }, Roll = { Amplitude = 0.25, Frequency = 0.325 } },
}

local function RemoveShake(State, Shake)
    if Shake ~= nil and Shake:IsValid() and State.mManager ~= nil and State.mManager:IsValid() then
        State.mManager:RemoveCameraModifier(Shake)
    end
end

local function StopBreathingSound(State)
    if State.mBreathingSoundHandle ~= 0 then
        StopSound(State.mBreathingSoundHandle)
        State.mBreathingSoundHandle = 0
    end
end

local function UpdateBreathingSound(State)
    local Config = HorrorCamera.Config
    local Volume = math.max(0.0, math.min(1.0, Config.BreathingMaxVolume)) * math.max(0.0, math.min(1.0, State.mBreathingStrength))
    if State.mBreathingStrength < 0.001 or Volume <= 0.0 then
        StopBreathingSound(State)
        return
    end
    if State.mBreathingSoundHandle ~= 0 and not IsSoundPlaying(State.mBreathingSoundHandle) then
        StopBreathingSound(State)
    end
    if State.mBreathingSoundHandle == 0 and not State.mBreathingSoundFailed then
        State.mBreathingSoundHandle = PlaySound(Config.BreathingSound, true, Volume)
        State.mBreathingSoundFailed = State.mBreathingSoundHandle == 0
    end
    if State.mBreathingSoundHandle ~= 0 then
        SetSoundVolume(State.mBreathingSoundHandle, Volume)
    end
end

local function ResetEffects(State)
    StopBreathingSound(State)
    State.mBreathingSoundFailed = false
    for _, Shake in ipairs(State.mHeartbeatShakes) do
        RemoveShake(State, Shake)
    end
    RemoveShake(State, State.mBreathingShake)
    State.mHeartbeatShakes = {}
    State.mBreathingShake = nil
    State.mHeartbeatPhase = nil
    State.mRunDistance = 0.0
    State.mBreathingStrength = 0.0
end

local function AddShake(State, Pattern, Scale)
    local Shake = State.mManager:AddCameraModifier("UCameraModifier_CameraShake", { Pattern = Pattern, Scale = Scale, PlaySpace = ECameraShakePlaySpace.CameraLocal })
    return Shake ~= nil and Shake:IsValid() and Shake or nil
end

local function SetZeroOffsets(Pattern)
    for _, Name in ipairs({ "X", "Y", "Z", "Pitch", "Yaw", "Roll", "FOV" }) do
        if Pattern[Name] ~= nil then
            Pattern[Name].InitialOffsetType = EInitialWaveOscillatorOffsetType.Zero
        end
    end
end

local function PlayHeartbeat(State, Strength)
    local Shake = AddShake(State, HorrorCamera.Config.HeartbeatPattern, Strength)
    if Shake ~= nil then
        table.insert(State.mHeartbeatShakes, Shake)
    end
end

local function UpdateHeartbeat(State, DeltaTime)
    for Index = #State.mHeartbeatShakes, 1, -1 do
        local Shake = State.mHeartbeatShakes[Index]
        if not Shake:IsValid() or Shake:IsFinished() then
            table.remove(State.mHeartbeatShakes, Index)
        end
    end

    local Strength = EnemyAudio.GetHeartbeatStrength()
    if Strength <= 0.0 then
        for _, Shake in ipairs(State.mHeartbeatShakes) do
            RemoveShake(State, Shake)
        end
        State.mHeartbeatShakes = {}
        State.mHeartbeatPhase = nil
        return
    end
    if State.mHeartbeatPhase == nil then
        State.mHeartbeatPhase = 0.0
        PlayHeartbeat(State, Strength)
        return
    end

    local Config = HorrorCamera.Config
    local Frequency = Config.HeartbeatMinimumFrequency + (Config.HeartbeatMaximumFrequency - Config.HeartbeatMinimumFrequency) * Strength
    local PreviousPhase = State.mHeartbeatPhase
    local Phase = PreviousPhase + DeltaTime * Frequency
    State.mHeartbeatPhase = Phase % 1.0
    local Wrapped = Phase >= 1.0
    if Wrapped and State.mHeartbeatPhase / Frequency <= Config.HeartbeatMaximumDelay then
        PlayHeartbeat(State, Strength)
    end
    local SecondaryPhase = Config.HeartbeatSecondaryPhase
    if State.mHeartbeatPhase >= SecondaryPhase and (Wrapped or PreviousPhase < SecondaryPhase) and (State.mHeartbeatPhase - SecondaryPhase) / Frequency <= Config.HeartbeatMaximumDelay then
        PlayHeartbeat(State, Strength * Config.HeartbeatSecondaryStrength)
    end
end

local function UpdateBreathing(State, Distance, DeltaTime, IsRunning)
    local Config = HorrorCamera.Config
    local Running = IsRunning and Distance > DeltaTime * 0.05
    if Running then
        State.mRunDistance = math.min(Config.FullBreathingRunDistance, State.mRunDistance + Distance)
    end

    local TargetStrength = Running and 0.0 or State.mRunDistance / Config.FullBreathingRunDistance
    local Blend = 1.0 - math.exp(-Config.BreathingBlendSpeed * DeltaTime)
    State.mBreathingStrength = State.mBreathingStrength + (TargetStrength - State.mBreathingStrength) * Blend
    if not Running then
        State.mRunDistance = math.max(0.0, State.mRunDistance - Config.RecoveryDistancePerSecond * DeltaTime)
    end

    if State.mBreathingShake ~= nil and not State.mBreathingShake:IsValid() then
        State.mBreathingShake = nil
    end
    if State.mRunDistance <= 0.0 and State.mBreathingStrength < 0.001 then
        RemoveShake(State, State.mBreathingShake)
        State.mBreathingShake = nil
        State.mBreathingStrength = 0.0
        StopBreathingSound(State)
        return
    end
    if State.mBreathingShake == nil and State.mBreathingStrength >= 0.001 then
        State.mBreathingShake = AddShake(State, Config.BreathingPattern, State.mBreathingStrength)
    end
    if State.mBreathingShake ~= nil then
        State.mBreathingShake:SetShakeScale(State.mBreathingStrength)
    end
    UpdateBreathingSound(State)
end

function HorrorCamera.Start(StateKey, Actor)
    HorrorCamera.Stop(StateKey)
    SetZeroOffsets(HorrorCamera.Config.HeartbeatPattern)
    SetZeroOffsets(HorrorCamera.Config.BreathingPattern)
    Runtime.mStates[StateKey] = { mActor = Actor, mManager = nil, mHeartbeatPhase = nil, mHeartbeatShakes = {}, mBreathingShake = nil, mRunDistance = 0.0, mBreathingStrength = 0.0, mBreathingSoundHandle = 0, mBreathingSoundFailed = false }
end

function HorrorCamera.Stop(StateKey)
    local State = Runtime.mStates[StateKey]
    if State ~= nil then
        ResetEffects(State)
        Runtime.mStates[StateKey] = nil
    end
end

function HorrorCamera.Update(StateKey, LocalMovement, DeltaTime, IsRunning, IsActive)
    local State = Runtime.mStates[StateKey]
    if State == nil or DeltaTime <= 0.0 then
        return
    end
    if IsActive == false then
        ResetEffects(State)
        return
    end

    local Manager = State.mActor:GetPlayerCameraManager()
    if Manager == nil or not Manager:IsValid() or Manager:GetViewTargetCamera() ~= State.mActor:GetCamera() then
        ResetEffects(State)
        return
    end
    if State.mManager ~= Manager then
        ResetEffects(State)
        State.mManager = Manager
    end

    local Distance = math.sqrt(LocalMovement.X * LocalMovement.X + LocalMovement.Y * LocalMovement.Y)
    UpdateHeartbeat(State, DeltaTime)
    UpdateBreathing(State, Distance, DeltaTime, IsRunning)
end

return HorrorCamera
