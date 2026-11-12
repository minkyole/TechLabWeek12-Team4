local FirstPersonFootsteps = require("FirstPersonFootsteps")
local StateKey = sceneName .. ":" .. self:GetName()

function BeginPlay()
    FirstPersonFootsteps.Start(StateKey)
end

function Tick(DeltaTime)
    FirstPersonFootsteps.Update(StateKey)
end

function EndPlay()
    FirstPersonFootsteps.Stop(StateKey)
end
