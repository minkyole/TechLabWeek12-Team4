local JumpScare = require("JumpScare")
local EndingCutscene = require("EndingCutscene")
local IntroCutscene = require("IntroCutscene")
local GameManager = {}

GameManager.EPhase = {
    Ready = "Ready",
    Intro = "Intro",
    Playing = "Playing",
    JumpScare = "JumpScare",
    GameOver = "GameOver",
    Escaped = "Escaped",
    Ending = "Ending",
}

local function ResetProgress()
    GameManager.Phase = GameManager.EPhase.Ready
    GameManager.ElapsedTime = 0.0
    GameManager.bLightOn = false
    GameManager.BatteriesUsed = 0
    GameManager.Route = nil
    GameManager.GameOverReason = nil
end

local function ClearReferences()
    GameManager.Player = nil
    GameManager.Enemies = {}
    GameManager.EnemyMachines = {}
    GameManager.EnemyScareProfiles = {}
    GameManager.Items = {}
    GameManager.Map = nil
end

local function IndexOf(list, value)
    for index, element in ipairs(list) do
        if element == value then
            return index
        end
    end
    return nil
end

local function FindRegisteredEnemy(Actor)
    local Index = IndexOf(GameManager.Enemies, Actor)
    return Index and GameManager.Enemies[Index] or nil, Index
end

local function CheckPlayerCapture()
    local Map = GameManager.Map
    local PlayerLocation = GameManager.GetPlayerLocation()
    if Map == nil or PlayerLocation == nil then
        return
    end

    local PlayerCell = Map:WorldToCell(PlayerLocation)
    local PlayerFloor = math.floor((PlayerLocation.Z - Map.Origin.Z) / Map.FloorHeight)
    for _, Enemy in ipairs(GameManager.Enemies) do
        local EnemyLocation = Enemy:GetActorLocation()
        local EnemyFloor = math.floor((EnemyLocation.Z - Map.Origin.Z) / Map.FloorHeight)
        if PlayerFloor == EnemyFloor and Map.IsSameCell(PlayerCell, Map:WorldToCell(EnemyLocation)) then
            local Reason = GameManager.EnemyScareProfiles[Enemy] == "Watcher" and "CaughtByWatcher" or "CaughtByEnemy"
            GameManager.GameOver(Reason, Enemy)
            return
        end
    end
end

function GameManager.Reset()
    SetCursorMode(CursorMode.Free)
    JumpScare.Stop()
    EndingCutscene.Stop()
    IntroCutscene.Stop()
    ResetProgress()
    ClearReferences()
end

function GameManager.StartGame(PlayIntro)
    JumpScare.Stop()
    EndingCutscene.Stop()
    IntroCutscene.Stop()
    EndingCutscene.ClearPending()
    ResetProgress()
    GameManager.Phase = PlayIntro and GameManager.EPhase.Intro or GameManager.EPhase.Playing
    SetCursorMode(CursorMode.Locked)
    print("[GameManager] StartGame")
end

function GameManager.Tick(dt)
    if GameManager.Phase == GameManager.EPhase.Intro then
        if IntroCutscene.Update(dt, GameManager.Player) then
            GameManager.Phase = GameManager.EPhase.Playing
            SetCursorMode(CursorMode.Locked)
        end
    elseif GameManager.Phase == GameManager.EPhase.Playing then
        GameManager.ElapsedTime = GameManager.ElapsedTime + dt
        CheckPlayerCapture()
    elseif GameManager.IsJumpScaring() and JumpScare.Update(dt) then
        GameManager.Phase = GameManager.EPhase.GameOver
        SetCursorMode(CursorMode.Free)
    end
end

function GameManager.IsPlaying()
    return GameManager.Phase == GameManager.EPhase.Playing
end

function GameManager.IsJumpScaring()
    return GameManager.Phase == GameManager.EPhase.JumpScare
end

function GameManager.RegisterPlayer(actor)
    if GameManager.Player and GameManager.Player ~= actor then
        print("[GameManager] Player replaced:", GameManager.Player:GetName(), "->", actor:GetName())
    end
    GameManager.Player = actor
end

function GameManager.UnregisterPlayer(Actor)
    IntroCutscene.ReleaseActor(Actor)
    JumpScare.ReleaseActor(Actor)
    if GameManager.Player == Actor then
        GameManager.Player = nil
    end
end

function GameManager.GetPlayer()
    return GameManager.Player
end

function GameManager.GetPlayerLocation()
    if not GameManager.Player then
        return nil
    end
    return GameManager.Player:GetActorLocation()
end

function GameManager.ReportPlayerFootstep(Actor, Radius)
    if not GameManager.IsPlaying() or Actor == nil or Actor ~= GameManager.Player or Radius <= 0.0 then
        return false
    end

    local Location = Actor:GetActorLocation()
    local SoundLocation = FVector.new(Location.X, Location.Y, Location.Z)
    for _, Enemy in ipairs(GameManager.Enemies) do
        local Machine = GameManager.EnemyMachines[Enemy]
        if Machine ~= nil and Machine:IsRunning() then
            Machine:HandleEvent("PlayerFootstep", SoundLocation, Radius)
        end
    end
    return true
end

function GameManager.RegisterEnemy(Actor, Machine, ScareProfile)
    local Enemy = FindRegisteredEnemy(Actor)
    if Enemy == nil then
        Enemy = Actor
        table.insert(GameManager.Enemies, Enemy)
    end
    GameManager.EnemyScareProfiles[Enemy] = ScareProfile or GameManager.EnemyScareProfiles[Enemy] or "Monster"
    if Machine ~= nil then
        GameManager.EnemyMachines[Enemy] = Machine
    end
end

function GameManager.UnregisterEnemy(Actor)
    JumpScare.ReleaseActor(Actor)
    local Enemy, Index = FindRegisteredEnemy(Actor)
    if Enemy ~= nil then
        GameManager.EnemyMachines[Enemy] = nil
        GameManager.EnemyScareProfiles[Enemy] = nil
        table.remove(GameManager.Enemies, Index)
    end
end

function GameManager.GetEnemyState(Actor)
    local Enemy = FindRegisteredEnemy(Actor)
    local Machine = Enemy ~= nil and GameManager.EnemyMachines[Enemy] or nil
    return Machine and Machine:IsRunning() and Machine:GetCurrentState() or nil
end

function GameManager.GetEnemies()
    return GameManager.Enemies
end

function GameManager.IsEnemy(actor)
    return actor ~= nil and IndexOf(GameManager.Enemies, actor) ~= nil
end

function GameManager.RegisterItem(actor, itemId)
    for _, entry in ipairs(GameManager.Items) do
        if entry.Actor == actor then
            entry.ItemId = itemId
            return
        end
    end
    table.insert(GameManager.Items, { Actor = actor, ItemId = itemId })
end

function GameManager.UnregisterItem(actor)
    for index, entry in ipairs(GameManager.Items) do
        if entry.Actor == actor then
            table.remove(GameManager.Items, index)
            return
        end
    end
end

function GameManager.GetItems()
    return GameManager.Items
end

function GameManager.FindNearestItem(location, radius)
    local nearest = nil
    local nearestDistanceSquared = nil
    local radiusSquared = radius * radius
    for _, entry in ipairs(GameManager.Items) do
        local itemLocation = entry.Actor:GetActorLocation()
        local dx = itemLocation.X - location.X
        local dy = itemLocation.Y - location.Y
        local distanceSquared = dx * dx + dy * dy
        if distanceSquared <= radiusSquared and (nearest == nil or distanceSquared < nearestDistanceSquared) then
            nearest = entry
            nearestDistanceSquared = distanceSquared
        end
    end
    return nearest
end

function GameManager.SetMap(map)
    GameManager.Map = map
end

function GameManager.GetMap()
    return GameManager.Map
end

function GameManager.SetLightOn(bOn)
    GameManager.bLightOn = bOn and true or false
end

function GameManager.IsLightOn()
    return GameManager.bLightOn
end

function GameManager.AddBatteryUsed()
    GameManager.BatteriesUsed = GameManager.BatteriesUsed + 1
end

function GameManager.GameOver(Reason, Killer)
    if not GameManager.IsPlaying() then
        return
    end
    GameManager.Phase = GameManager.EPhase.GameOver
    GameManager.GameOverReason = Reason
    SetCursorMode(CursorMode.Free)
    local Enemy = FindRegisteredEnemy(Killer)
    if Enemy ~= nil and JumpScare.Start(GameManager.Player, Enemy, GameManager.EnemyScareProfiles[Enemy]) then
        GameManager.Phase = GameManager.EPhase.JumpScare
    end
    print("[GameManager] GameOver:", Reason)
end

function GameManager.Escape(Route)
    if not GameManager.IsPlaying() then
        return
    end
    GameManager.Route = Route
    GameManager.Phase = EndingCutscene.Prepare(Route, GameManager.GetResult()) and GameManager.EPhase.Ending or GameManager.EPhase.Escaped
    SetCursorMode(CursorMode.Free)
    print("[GameManager] Escaped:", Route)
end

function GameManager.GetResult()
    return {
        Phase = GameManager.Phase,
        ElapsedTime = GameManager.ElapsedTime,
        BatteriesUsed = GameManager.BatteriesUsed,
        Route = GameManager.Route,
        GameOverReason = GameManager.GameOverReason,
    }
end

GameManager.Reset()

return GameManager
