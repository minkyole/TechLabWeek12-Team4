local Ending = require("EndingCutscene")

function BeginPlay()
    Ending.RegisterCameraSequence(self, "Car")
end

function EndPlay()
    Ending.UnregisterCameraSequence(self)
end
