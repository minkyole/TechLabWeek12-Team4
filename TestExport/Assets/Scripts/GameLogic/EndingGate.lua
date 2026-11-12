local EndingCutscene = require("EndingCutscene")

function BeginPlay()
    local Width = self:GetNavigationExtent().Y * 2.0 + 0.2
    local Angle = math.rad(self:GetActorRotation().Yaw)
    EndingCutscene.RegisterGate(self, FVector.new(-math.sin(Angle) * Width, math.cos(Angle) * Width, 0.0))
end

function EndPlay()
    EndingCutscene.UnregisterGate(self)
end
