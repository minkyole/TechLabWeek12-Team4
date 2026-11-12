local FSM = require("FSM")
local States = require("WatcherStates")
local GameManager = require("GameManager")
local Runtime = { mMachine = nil }

local function Initialize()
    if Runtime.mMachine ~= nil then
        Runtime.mMachine:Stop()
    end
    local Data = { mPatrolExtent = self:GetNavigationExtent(), mPatrolWallMargin = 0.04, mPatrolTurnSpeed = 540.0, mChaseSpeed = 2.5, mChaseReplanInterval = 0.35, mUnobservedDelay = 0.75, mStunDuration = 1.5 }
    Runtime.mMachine = FSM.New(self, States, Data)
    GameManager.RegisterEnemy(self, Runtime.mMachine, "Watcher")
    Runtime.mMachine:Start("Observed")
end

function BeginPlay()
    Initialize()
end

function Tick(DeltaTime)
    if not GameManager.IsPlaying() or Runtime.mMachine == nil then
        return
    end
    if IsKeyPressed(Key.Space) then
        Runtime.mMachine:HandleEvent("Stun")
    end
    Runtime.mMachine:Tick(DeltaTime)
end

function OnOverlap(OtherActor)
    if OtherActor == GameManager.GetPlayer() then
        GameManager.GameOver("CaughtByWatcher", self)
    end
end

function EndPlay()
    if Runtime.mMachine ~= nil then
        Runtime.mMachine:Stop()
        Runtime.mMachine = nil
    end
    GameManager.UnregisterEnemy(self)
end

function OnReload()
    Initialize()
end
