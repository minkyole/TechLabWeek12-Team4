local Intro = require("IntroCutscene")

function BeginPlay()
    Intro.RegisterCameraSequence(self, true)
end

function EndPlay()
    Intro.ReleaseActor(self)
end
