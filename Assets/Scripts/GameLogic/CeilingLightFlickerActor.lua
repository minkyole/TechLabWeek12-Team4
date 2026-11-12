local CeilingLightFlicker = require("CeilingLightFlicker")

function BeginPlay()
    CeilingLightFlicker.Start(self)
end

function Tick(DeltaTime)
    CeilingLightFlicker.Tick(self)
end

function EndPlay()
    CeilingLightFlicker.Stop(self)
end

function OnReload()
    CeilingLightFlicker.Start(self)
end
