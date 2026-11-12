local GameManager = require("GameManager")
local Transition = require("Transition")

local BGM_SOUND = "Assets/Sounds/공포브금검은집.mp3"
local BGM_VOLUME = 0.35
local FADE_IN_SECONDS = 2.0
local FADE_OUT_SECONDS = 1.5

local handle = 0
local bFadingOut = false

local function IsGameFinished()
    local phase = GameManager.Phase
    return GameManager.IsJumpScaring() or phase == GameManager.EPhase.GameOver or phase == GameManager.EPhase.Escaped or phase == GameManager.EPhase.Ending
end

function BeginPlay()
    bFadingOut = false
    handle = PlaySound(BGM_SOUND, true, 0.0)
    if handle == 0 then
        print("[BackgroundMusic] Cannot play", BGM_SOUND)
        return
    end
    FadeSound(handle, BGM_VOLUME, FADE_IN_SECONDS)
end

function Tick(dt)
    if handle ~= 0 and not bFadingOut and IsGameFinished() then
        bFadingOut = true
        local Duration = GameManager.Phase == GameManager.EPhase.Ending and Transition.Config.Duration or FADE_OUT_SECONDS
        FadeSound(handle, 0.0, Duration)
    end
end

function EndPlay()
    if handle ~= 0 then
        StopSound(handle)
        handle = 0
    end
end
