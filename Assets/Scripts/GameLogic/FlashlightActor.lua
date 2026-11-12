local Flashlight = require("Flashlight")
local GameManager = require("GameManager")
local JumpScare = require("JumpScare")
local IntroCutscene = require("IntroCutscene")

local TOGGLE_KEY = Key.F
local DEBUG_SMALL_BATTERY_KEY = Key.Num1
local DEBUG_LARGE_BATTERY_KEY = Key.Num2
local DEBUG_LOG_KEY = Key.Num3

function BeginPlay()
    JumpScare.RegisterView(self)
    IntroCutscene.RegisterPlayerView(self)
    Flashlight.Start(self:GetSpotLight())
end

function Tick(dt)
    if not GameManager.IsPlaying() then
        return
    end

    if IsKeyPressed(TOGGLE_KEY) then
        Flashlight.RequestToggle()
    end

    if IsKeyPressed(DEBUG_SMALL_BATTERY_KEY) then
        Flashlight.RequestBatterySwap(45.0)
    end

    if IsKeyPressed(DEBUG_LARGE_BATTERY_KEY) then
        Flashlight.RequestBatterySwap(90.0)
    end

    if IsKeyPressed(DEBUG_LOG_KEY) then
        print(string.format("[Flashlight] on=%s battery=%.1f swapping=%s",
            tostring(Flashlight.IsOn()), Flashlight.GetBattery(), tostring(Flashlight.IsSwapping())))
    end

    Flashlight.Tick(dt)
end

function EndPlay()
    IntroCutscene.ReleaseActor(self)
    JumpScare.UnregisterView(self)
    Flashlight.Stop()
end
