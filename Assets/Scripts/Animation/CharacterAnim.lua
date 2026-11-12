-- Lua-mode counterpart of Assets/NewStateMachine.animsm.
-- ULuaAnimInstance calls InitializeAnimation between BeginBuild and Commit.
-- Keep state order, transition order, conditions and options aligned with that asset.
local locomotionPlayer
local swordLocomotionPlayer
local locomotionBlend

local function requireId(anim, id)
    -- Lua treats -1 as true, so assert(id) would not detect an invalid handle.
    assert(id ~= -1, anim:GetLastError())
    return id
end

local function requireSuccess(anim, ok)
    assert(ok, anim:GetLastError())
end

function InitializeAnimation(anim)
    requireSuccess(anim, anim:SetFloat("Speed", 0.0))
    requireSuccess(anim, anim:SetFloat("AttackReady", 0.0))
    requireSuccess(anim, anim:SetBool("IsGrounded", true))
    for _, name in ipairs({ "HitRequested", "AttackRequested", "DeathRequested", "RollRequested" }) do
        requireSuccess(anim, anim:DefineTrigger(name))
    end

    -- Locomotion/Pose: normal movement A, sword movement B, AttackReady alpha.
    -- Use the saved BlendSpace keys instead of the synthetic "Locomotion" alias.
    locomotionPlayer = requireId(anim,
        anim:CreateBlendSpacePlayer("BS_Locomotion.blendspace", { loop = true }))
    swordLocomotionPlayer = requireId(anim,
        anim:CreateBlendSpacePlayer("BS_LocomotionSword.blendspace", { loop = true }))
    locomotionBlend = requireId(anim,
        anim:CreateTwoWayBlend(locomotionPlayer, swordLocomotionPlayer, 0.0))

    local function sequence(clip, loop, rate)
        return requireId(anim, anim:CreateSequencePlayer(
            "Models/UAL1_Standard_RM.fbx#Anim/Armature|" .. clip,
            { loop = loop, playing = true, playRate = rate or 1.0 }))
    end

    -- The PoseGraph is authoritative: Revival uses Sitting_Exit, not the legacy
    -- state-level Asset field (which still says Jump_Start in the saved asset).
    local stateDefinitions = {
        { "Locomotion", locomotionBlend },
        { "Attack", sequence("Sword_Attack", false) },
        { "Hit", sequence("Hit_Chest", false) },
        { "JumpStart", sequence("Jump_Start", false) },
        { "Death", sequence("Death01", false) },
        { "Revival", sequence("Sitting_Exit", false) },
        { "JumpLoop", sequence("Jump_Loop", true) },
        { "JumpLand", sequence("Jump_Land", false) },
        { "Roll", sequence("Roll", false, 1.4) },
    }
    local states = {}
    for _, definition in ipairs(stateDefinitions) do
        states[definition[1]] = requireId(anim, anim:AddState(definition[1], definition[2]))
    end
    requireSuccess(anim, anim:SetEntryState(states.Locomotion))

    local conditions = {}
    local function condition(name, callback)
        conditions[name] = requireId(anim, anim:AddCondition(callback))
    end
    condition("HitRequested", function(ctx)
        return ctx:IsTriggerActive("HitRequested")
    end)
    condition("CanAttack", function(ctx)
        return ctx:GetBool("IsGrounded") and ctx:IsTriggerActive("AttackRequested")
    end)
    condition("Finished", function(ctx)
        return ctx:IsSourceFinished()
    end)
    condition("Airborne", function(ctx)
        return not ctx:GetBool("IsGrounded")
    end)
    condition("Grounded", function(ctx)
        return ctx:GetBool("IsGrounded")
    end)
    condition("DeathRequested", function(ctx)
        return ctx:IsTriggerActive("DeathRequested")
    end)
    condition("CanLeaveLanding", function(ctx)
        return ctx:IsSourceFinished()
            or (ctx:GetStateElapsed() > 0.2 and ctx:GetFloat("Speed") > 0.1)
    end)
    condition("RollRequested", function(ctx)
        return ctx:IsTriggerActive("RollRequested")
    end)

    -- Preserve file order: equal-priority transitions are resolved in this order.
    -- From=false means AnyState; no extra death, jump or roll conditions are added.
    local transitions = {
        { false, "Hit", "HitRequested", 0.1, 100, "Always", true, "HitRequested" },
        { "Locomotion", "Attack", "CanAttack", 0.2, 50, "Never", false, "AttackRequested", "SmoothStep" },
        { "Attack", "Locomotion", "Finished", 0.2, 0, "Never" },
        { "Hit", "Locomotion", "Finished", 0.2, 0, "Never" },
        { "Locomotion", "JumpStart", "Airborne", 0.2, 0, "Always" },
        { false, "Death", "DeathRequested", 0.2, 0, "Always", false, "DeathRequested" },
        { "Death", "Revival", "Finished", 0.5, 0, "Always" },
        { "Revival", "Locomotion", "Finished", 0.2, 0, "Always" },
        { "JumpStart", "JumpLoop", "Finished", 0.2, 0, "Always" },
        { "JumpLoop", "JumpLand", "Grounded", 0.2, 0, "Always" },
        { "JumpLand", "Locomotion", "CanLeaveLanding", 0.5, 0, "Always" },
        { "JumpStart", "JumpLand", "Grounded", 0.2, 0, "Always" },
        { "JumpLand", "JumpStart", "Airborne", 0.2, 0, "Always" },
        { "Locomotion", "Roll", "RollRequested", 0.2, 0, "Always", false, "RollRequested" },
        { "Roll", "Locomotion", "Finished", 0.2, 0, "Always" },
        { "Roll", "JumpLoop", "Airborne", 0.5, 0, "Always" },
    }
    for _, transition in ipairs(transitions) do
        local options = {
            condition = conditions[transition[3]],
            blendDuration = transition[4],
            priority = transition[5],
            interruptPolicy = transition[6],
            restartIfSameState = transition[7] == true,
            blendCurve = transition[9] or "Linear",
            consumeTriggers = transition[8] and { transition[8] } or {},
        }
        local id
        if transition[1] == false then
            id = anim:AddAnyStateTransition(states[transition[2]], options)
        else
            id = anim:AddTransition(states[transition[1]], states[transition[2]], options)
        end
        requireId(anim, id)
    end
end

function UpdateAnimation(anim, deltaTime)
    local movement = anim:GetMovementComponent()
    if movement ~= nil then
        requireSuccess(anim, anim:SetFloat("Speed", movement:GetGroundSpeed()))
        requireSuccess(anim, anim:SetBool("IsGrounded", movement:IsGrounded()))
    end

    -- The actor script (AnimationTest.lua) owns inputs, trigger requests and
    -- AttackReady. Do not overwrite its blend value or generate input twice here.
    local speed = anim:GetFloat("Speed")
    local attackReady = math.max(0.0, math.min(1.0, anim:GetFloat("AttackReady")))
    requireSuccess(anim, anim:SetBlendSpaceInput(locomotionPlayer, speed))
    requireSuccess(anim, anim:SetBlendSpaceInput(swordLocomotionPlayer, speed))
    requireSuccess(anim, anim:SetBlendAlpha(locomotionBlend, attackReady))
end
