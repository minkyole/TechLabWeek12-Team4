local Transition = {}

Transition.Config = {
    Duration = 1.35,
}

function Transition.Start(View)
    local Manager = View:GetPlayerCameraManager()
    if Manager == nil then
        return false, nil, nil
    end
    local PreviousCamera = Manager:GetViewTargetCamera()
    Manager:RemoveAllCameraModifiers()
    return Manager:SetViewTargetWithBlend(View, Transition.Config.Duration), Manager, PreviousCamera
end

return Transition
