local Ending = require("EndingCutscene")

function BeginPlay()
    Ending.RegisterCameraSequence(self, "Door")
end

function EndPlay()
    Ending.UnregisterCameraSequence(self)
end
