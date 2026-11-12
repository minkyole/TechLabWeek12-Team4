local IntroCutscene = require("IntroCutscene")

function BeginPlay()
    IntroCutscene.RegisterDoor(self)
end

function EndPlay()
    IntroCutscene.ReleaseActor(self)
end
