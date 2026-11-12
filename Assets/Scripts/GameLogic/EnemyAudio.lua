local GameManager = require("GameManager")

local EnemyAudio = {}

EnemyAudio.Config = {
    FootstepSound = "Assets/Sounds/괴물발소리.mp3",
    HeartbeatSound = "Assets/Sounds/심장소리.mp3",

    FootstepFullDistance = 3.0,
    FootstepSilentDistance = 40.0,
    HeartbeatFullDistance = 6.0,
    HeartbeatSilentDistance = 16.0,

    FootstepMaxVolume = 0.7,
    FootstepPitch = 1.0,
    ChaseFootstepPitch = 1.5,
    HeartbeatMaxVolume = 0.6,

    VolumeChangePerSecond = 2.0,
    MovingSpeedThreshold = 0.2,
}

local layers = {}
local enemyStates = setmetatable({}, { __mode = "k" })
local nearestDistance = math.huge

local function StartLayer(name, path, pitch)
    if not path then
        return
    end
    local handle = PlaySound(path, true, 0.0)
    if handle == 0 then
        print("[EnemyAudio] Cannot play", name, path)
        return
    end
    if pitch then
        SetSoundPitch(handle, pitch)
    end
    layers[name] = { Handle = handle, Volume = 0.0, Pitch = pitch or 1.0 }
end

local function UpdateLayerPitch(Name, Pitch)
    local Layer = layers[Name]
    if Layer ~= nil and Layer.Pitch ~= Pitch then
        SetSoundPitch(Layer.Handle, Pitch)
        Layer.Pitch = Pitch
    end
end

local function UpdateLayer(name, target, dt)
    local layer = layers[name]
    if not layer then
        return
    end
    local step = EnemyAudio.Config.VolumeChangePerSecond * dt
    local volume = layer.Volume
    if volume < target then
        volume = math.min(target, volume + step)
    elseif volume > target then
        volume = math.max(target, volume - step)
    end
    if volume ~= layer.Volume then
        layer.Volume = volume
        SetSoundVolume(layer.Handle, volume)
    end
end

local function HorizontalDistance(a, b)
    local dx = a.X - b.X
    local dy = a.Y - b.Y
    return math.sqrt(dx * dx + dy * dy)
end

local function CellKey(cell)
    return cell.Floor .. ":" .. cell.X .. ":" .. cell.Y
end

local function PathDistance(state, enemyLocation, playerLocation)
    local map = GameManager.GetMap()
    if not map then
        return HorizontalDistance(enemyLocation, playerLocation)
    end

    local enemyCell = map:WorldToCell(enemyLocation)
    local playerCell = map:WorldToCell(playerLocation)
    local cacheKey = CellKey(enemyCell) .. "|" .. CellKey(playerCell)
    if state.CacheKey == cacheKey then
        return state.Distance
    end

    local path, distance = map:FindPath(enemyCell, playerCell)
    if not path then
        distance = distance == "Unreachable" and math.huge or HorizontalDistance(enemyLocation, playerLocation)
    end

    state.CacheKey = cacheKey
    state.Distance = distance
    return distance
end

local function VolumeForDistance(distance, fullDistance, silentDistance)
    if distance <= fullDistance then
        return 1.0
    end
    if distance >= silentDistance then
        return 0.0
    end
    return 1.0 - (distance - fullDistance) / (silentDistance - fullDistance)
end

function EnemyAudio.FootstepVolumeForDistance(distance)
    local config = EnemyAudio.Config
    return VolumeForDistance(distance, config.FootstepFullDistance, config.FootstepSilentDistance)
end

function EnemyAudio.HeartbeatVolumeForDistance(distance)
    local config = EnemyAudio.Config
    return VolumeForDistance(distance, config.HeartbeatFullDistance, config.HeartbeatSilentDistance)
end

function EnemyAudio.Start()
    EnemyAudio.Stop()
    local config = EnemyAudio.Config
    StartLayer("Footstep", config.FootstepSound, config.FootstepPitch)
    StartLayer("Heartbeat", config.HeartbeatSound)
end

function EnemyAudio.Stop()
    for _, layer in pairs(layers) do
        StopSound(layer.Handle)
    end
    layers = {}
    enemyStates = setmetatable({}, { __mode = "k" })
    nearestDistance = math.huge
end

function EnemyAudio.Tick(dt)
    local config = EnemyAudio.Config
    local playerLocation = GameManager.GetPlayerLocation()
    local footstepTarget = 0.0
    local FootstepPitch = config.FootstepPitch
    nearestDistance = math.huge

    if playerLocation and GameManager.IsPlaying() and dt > 0.0 then
        for _, enemy in ipairs(GameManager.GetEnemies()) do
            local state = enemyStates[enemy]
            if not state then
                state = {}
                enemyStates[enemy] = state
            end

            local location = enemy:GetActorLocation()
            local bMoving = state.LastLocation ~= nil
                and HorizontalDistance(location, state.LastLocation) / dt > config.MovingSpeedThreshold
            state.LastLocation = { X = location.X, Y = location.Y, Z = location.Z }

            local distance = PathDistance(state, location, playerLocation)
            nearestDistance = math.min(nearestDistance, distance)

            if bMoving then
                local Volume = EnemyAudio.FootstepVolumeForDistance(distance)
                if Volume > footstepTarget then
                    footstepTarget = Volume
                    local StateName = GameManager.GetEnemyState(enemy)
                    FootstepPitch = (StateName == "Chase" or StateName == "Stalking") and config.ChaseFootstepPitch or config.FootstepPitch
                end
            end
        end
    end

    UpdateLayerPitch("Footstep", FootstepPitch)
    UpdateLayer("Footstep", footstepTarget * config.FootstepMaxVolume, dt)
    UpdateLayer("Heartbeat", EnemyAudio.HeartbeatVolumeForDistance(nearestDistance) * config.HeartbeatMaxVolume, dt)
end

function EnemyAudio.GetNearestDistance()
    return nearestDistance
end

function EnemyAudio.GetHeartbeatStrength()
    local Layer = layers.Heartbeat
    if Layer == nil or EnemyAudio.Config.HeartbeatMaxVolume <= 0.0 or not IsSoundPlaying(Layer.Handle) then
        return 0.0
    end
    return math.min(1.0, Layer.Volume / EnemyAudio.Config.HeartbeatMaxVolume)
end

function EnemyAudio.GetLayerVolume(name)
    local layer = layers[name]
    return layer and layer.Volume or 0.0
end

return EnemyAudio
