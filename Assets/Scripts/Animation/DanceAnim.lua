local DanceClip = "Models/UAL1_Standard_RM.fbx#Anim/Armature|Dance_Loop"

function InitializeAnimation(anim)
    local player = anim:CreateSequencePlayer(DanceClip, { loop = true })
    assert(player ~= -1, anim:GetLastError())

    local dance = anim:AddState("Dance", player)
    assert(dance ~= -1, anim:GetLastError())

    assert(anim:SetEntryState(dance), anim:GetLastError())
end
