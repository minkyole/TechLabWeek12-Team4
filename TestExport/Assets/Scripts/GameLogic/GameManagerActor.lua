local GameManager = require("GameManager")
local EndingCutscene = require("EndingCutscene")
local Runtime = { mDanger = 0.0, mPulsePhase = 0.0 }

local DANGER_VIGNETTE = {
    NearDistance = 3.5,
    FarDistance = 16.0,
    MaxIntensity = 0.175,
    SafeRadius = 1.13,
    DangerRadius = 1.03,
    Softness = 0.1,
    InterpSpeed = 5.0,
    Color = FVector.new(0.3, 0, 0),
}

local GAME_OVER_VIGNETTE = {
    Intensity = 0.9,
    Radius = 0.45,
    Softness = 0.4,
    Color = FVector.new(0.35, 0.0, 0.0),
}

local function Clamp01(Value)
    return math.max(0.0, math.min(1.0, Value))
end

local function GetTargetDanger()
    local PlayerLocation = GameManager.GetPlayerLocation()
    if PlayerLocation == nil then
        return 0.0
    end

    local NearestDistance = DANGER_VIGNETTE.FarDistance
    for _, Enemy in ipairs(GameManager.GetEnemies()) do
        if Enemy ~= nil then
            local Distance = FVector.Distance(PlayerLocation, Enemy:GetActorLocation())
            NearestDistance = math.min(NearestDistance, Distance)
        end
    end

    return Clamp01(
        (DANGER_VIGNETTE.FarDistance - NearestDistance)
        / (DANGER_VIGNETTE.FarDistance - DANGER_VIGNETTE.NearDistance))
end

local function UpdateVignette(DeltaTime)
    if GameManager.IsJumpScaring() or GameManager.Phase == GameManager.EPhase.GameOver then
        SetVignette(
            GAME_OVER_VIGNETTE.Intensity,
            GAME_OVER_VIGNETTE.Radius,
            GAME_OVER_VIGNETTE.Softness,
            GAME_OVER_VIGNETTE.Color)
        return
    end

    if not GameManager.IsPlaying() then
        Runtime.mDanger = 0.0
        ResetVignette()
        return
    end

    local TargetDanger = GetTargetDanger()
    local Blend = 1.0 - math.exp(-DANGER_VIGNETTE.InterpSpeed * math.max(0.0, DeltaTime))
    Runtime.mDanger = Runtime.mDanger + (TargetDanger - Runtime.mDanger) * Blend

    local BaseRadius = DANGER_VIGNETTE.SafeRadius
        + (DANGER_VIGNETTE.DangerRadius - DANGER_VIGNETTE.SafeRadius) * Runtime.mDanger

    local Frequency = 1.2 + Runtime.mDanger * 1.8

    Runtime.mPulsePhase = (
        Runtime.mPulsePhase
        + math.max(0.0, DeltaTime)
        * Frequency
        * math.pi
        * 2.0
    ) % (math.pi * 2.0)

    local Wave = math.sin(Runtime.mPulsePhase) * 0.5 + 0.5
    local Pulse = Wave ^ 8
    local Radius = BaseRadius - Pulse * 0.09 * Runtime.mDanger

    SetVignette(
        DANGER_VIGNETTE.MaxIntensity * Runtime.mDanger,
        Radius,
        DANGER_VIGNETTE.Softness,
        DANGER_VIGNETTE.Color)
end

function BeginPlay()
    Runtime.mDanger = 0.0
    ResetVignette()
    -- sceneName은 폴더 없는 파일 이름이라 시연용 게임 씬은 Demo 경로를 직접 지정한다.
    EndingCutscene.SetGameScene(sceneName == "06_Game" and "Assets/Scenes/Demo/06_Game.scene" or "Assets/Scenes/" .. sceneName .. ".scene")
    GameManager.StartGame(sceneName == "06_Game")
end

function Tick(DeltaTime)
    
    local Phase = GameManager.Phase
    if Phase == GameManager.EPhase.Ending then
        ResetVignette()
        return
    end
    if (Phase == GameManager.EPhase.GameOver or Phase == GameManager.EPhase.Escaped) and IsKeyPressed(Key.R) then
        RestartScene()
        return
    end

    GameManager.Tick(DeltaTime)
    UpdateVignette(DeltaTime)
end

function EndPlay()
    ResetVignette()
    Runtime.mDanger = 0.0
    GameManager.Reset()
end
