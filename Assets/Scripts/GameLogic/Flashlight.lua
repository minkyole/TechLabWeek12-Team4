local GameManager = require("GameManager")

local Flashlight = {}

Flashlight.Config = {
    StartBattery = 90.0,
    MaxBattery = 90.0,
    DrainPerSecond = 1.0,
    ToggleDelay = 0.3,
    SwapTime = 1.5,
    LowBatteryThreshold = 15.0,
    FlickerChancePerSecond = 3.0,
    FlickerMinDuration = 0.05,
    FlickerMaxDuration = 0.15,
    FlickerMinScale = 0.15,
    FlickerMaxScale = 0.6,
    ToggleSound = "Assets/Sounds/손전등키는소리.mp3",
    ToggleSoundVolume = 0.8,
}

local light = nil
local baseIntensity = 1.0
local bOn = false
local battery = 0.0
local toggleTimer = 0.0
local swapTimer = 0.0
local pendingBattery = 0.0
local flickerTimer = 0.0

local function ApplyLight()
    if not light then
        return
    end
    light:SetVisibility(bOn)
    light:SetIntensity(baseIntensity)
end

local function SetOn(bNewOn)
    if bOn == bNewOn then
        return
    end
    bOn = bNewOn
    flickerTimer = 0.0
    ApplyLight()
    GameManager.SetLightOn(bOn)
end

local function UpdateFlicker(dt)
    if not light or not bOn then
        return
    end

    if flickerTimer > 0.0 then
        flickerTimer = flickerTimer - dt
        if flickerTimer <= 0.0 then
            light:SetIntensity(baseIntensity)
        end
        return
    end

    if battery > Flashlight.Config.LowBatteryThreshold then
        return
    end

    if math.random() < Flashlight.Config.FlickerChancePerSecond * dt then
        local config = Flashlight.Config
        flickerTimer = config.FlickerMinDuration + math.random() * (config.FlickerMaxDuration - config.FlickerMinDuration)
        light:SetIntensity(baseIntensity * (config.FlickerMinScale + math.random() * (config.FlickerMaxScale - config.FlickerMinScale)))
    end
end

function Flashlight.Start(spotLight)
    light = spotLight
    baseIntensity = light and light:GetIntensity() or 1.0
    bOn = false
    battery = Flashlight.Config.StartBattery
    toggleTimer = 0.0
    swapTimer = 0.0
    pendingBattery = 0.0
    flickerTimer = 0.0
    ApplyLight()
    GameManager.SetLightOn(false)

    if not light then
        print("[Flashlight] No SpotLight found on this actor")
    end
end

function Flashlight.Stop()
    if light then
        light:SetIntensity(baseIntensity)
    end
    SetOn(false)
    light = nil
end

function Flashlight.CanTurnOn()
    return battery > 0.0 and swapTimer <= 0.0
end

function Flashlight.RequestToggle()
    if toggleTimer > 0.0 or swapTimer > 0.0 then
        return false
    end
    if not bOn and not Flashlight.CanTurnOn() then
        return false
    end
    toggleTimer = Flashlight.Config.ToggleDelay
    if Flashlight.Config.ToggleSound then
        PlaySound(Flashlight.Config.ToggleSound, false, Flashlight.Config.ToggleSoundVolume)
    end
    return true
end

function Flashlight.RequestBatterySwap(seconds)
    if swapTimer > 0.0 or not seconds or seconds <= 0.0 then
        return false
    end
    if battery >= Flashlight.Config.MaxBattery then
        return false
    end
    toggleTimer = 0.0
    SetOn(false)
    swapTimer = Flashlight.Config.SwapTime
    pendingBattery = seconds
    return true
end

function Flashlight.Tick(dt)
    if swapTimer > 0.0 then
        swapTimer = swapTimer - dt
        if swapTimer <= 0.0 then
            swapTimer = 0.0
            battery = math.min(Flashlight.Config.MaxBattery, battery + pendingBattery)
            pendingBattery = 0.0
            GameManager.AddBatteryUsed()
        end
    end

    if toggleTimer > 0.0 then
        toggleTimer = toggleTimer - dt
        if toggleTimer <= 0.0 then
            toggleTimer = 0.0
            if bOn then
                SetOn(false)
            elseif Flashlight.CanTurnOn() then
                SetOn(true)
            end
        end
    end

    if bOn then
        battery = math.max(0.0, battery - Flashlight.Config.DrainPerSecond * dt)
        if battery <= 0.0 then
            SetOn(false)
        end
    end

    UpdateFlicker(dt)
end

function Flashlight.IsOn()
    return bOn
end

function Flashlight.IsSwitching()
    return toggleTimer > 0.0
end

function Flashlight.IsSwapping()
    return swapTimer > 0.0
end

function Flashlight.GetBattery()
    return battery
end

function Flashlight.GetBatteryRatio()
    return battery / Flashlight.Config.MaxBattery
end

return Flashlight
