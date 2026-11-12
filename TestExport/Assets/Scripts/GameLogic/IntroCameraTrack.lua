local Intro = require("IntroCutscene")

function BeginPlay()
    Intro.RegisterCameraSequence(self, false)
end

function EndPlay()
    Intro.ReleaseActor(self)
end
