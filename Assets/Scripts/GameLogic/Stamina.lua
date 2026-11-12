local Stamina = {}

Stamina.Config = {
    Max = 6.0,
    DrainPerSecond = 1.0,
    RecoverPerSecond = 1.0,
    RecoverDelay = 0.5,
    RunAgainThreshold = 2.0,
}

local stamina = Stamina.Config.Max
local bRunning = false
local bExhausted = false
local recoverWait = 0.0

function Stamina.Reset()
    stamina = Stamina.Config.Max
    bRunning = false
    bExhausted = false
    recoverWait = 0.0
end

function Stamina.Tick(dt, bWantsRun, bMoving)
    local config = Stamina.Config

    if bExhausted and stamina >= config.RunAgainThreshold then
        bExhausted = false
    end

    bRunning = bWantsRun and bMoving and not bExhausted and stamina > 0.0

    if bRunning then
        stamina = math.max(0.0, stamina - config.DrainPerSecond * dt)
        recoverWait = config.RecoverDelay
        if stamina <= 0.0 then
            bExhausted = true
        end
    elseif recoverWait > 0.0 then
        recoverWait = recoverWait - dt
    else
        stamina = math.min(config.Max, stamina + config.RecoverPerSecond * dt)
    end

    return bRunning
end

function Stamina.IsRunning()
    return bRunning
end

function Stamina.IsExhausted()
    return bExhausted
end

function Stamina.Get()
    return stamina
end

function Stamina.GetRatio()
    return stamina / Stamina.Config.Max
end

return Stamina
