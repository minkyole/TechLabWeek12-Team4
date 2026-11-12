-- Optional update-only script for an .animsm asset.
-- Declare Speed (Float) and IsGrounded (Bool) in the asset before assigning it.
-- Bind the locomotion BlendSpace axis to Speed in the state properties.
function UpdateAnimation(anim, dt)
    local movement = anim:GetMovementComponent()
    if movement == nil then
        return
    end
    assert(anim:SetFloat("Speed", movement:GetGroundSpeed()), anim:GetLastError())
    assert(anim:SetBool("IsGrounded", movement:IsGrounded()), anim:GetLastError())
end
