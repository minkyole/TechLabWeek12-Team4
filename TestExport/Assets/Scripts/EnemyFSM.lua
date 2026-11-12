local FSM = require("FSM")
local States = require("EnemyStates")
local GameManager = require("GameManager")
local Runtime = { mMachine = nil }

local function Initialize()
    if Runtime.mMachine ~= nil then
        Runtime.mMachine:Stop()
    end

    local Data = { mPatrolSpeed = 2.0, mPatrolTurnSpeed = 360.0, mPatrolReplanInterval = 2.5, mPatrolExtent = self:GetNavigationExtent(), mPatrolWallMargin = 0.04, mChaseSpeed = 4.0, mChaseReplanInterval = 0.5, mSearchDuration = 4.0, mSearchTurnSpeed = 90.0, mIdleDuration = 1.0, mStunDuration = 1.5, mResumeState = "Idle", mPatrolMap = nil, mPatrolPath = nil, mPatrolPathIndex = 1, mPatrolHeightOffset = 0.0, mPatrolBlockedTime = 0.0 }
    Runtime.mMachine = FSM.New(self, States, Data)
    GameManager.RegisterEnemy(self, Runtime.mMachine)
    Runtime.mMachine:Start("Idle")
    Log("[EnemyFSM]", self:GetName(), "Space: stun for 1.5 seconds; patrol: follow walkable map cells")
end

function BeginPlay()
    Initialize()
end

function Tick(DeltaTime)
    if not GameManager.IsPlaying() then
        return
    end
    if Runtime.mMachine == nil then
        return
    end

    if IsKeyPressed(Key.Space) then
        Runtime.mMachine:HandleEvent("Stun")
    end
    Runtime.mMachine:Tick(DeltaTime)
end

function OnOverlap(OtherActor)
    if Runtime.mMachine ~= nil then
        Runtime.mMachine:HandleEvent("OnOverlap", OtherActor)
    end
end

function OnEndOverlap(OtherActor)
    if Runtime.mMachine ~= nil then
        Runtime.mMachine:HandleEvent("OnEndOverlap", OtherActor)
    end
end

function OnHit(OtherActor, Hit)
    if Runtime.mMachine ~= nil then
        Runtime.mMachine:HandleEvent("OnHit", OtherActor, Hit)
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
