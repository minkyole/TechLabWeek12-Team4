local IntroCutscene = require("IntroCutscene")

function BeginPlay()
    IntroCutscene.RegisterFade(self)
end

function EndPlay()
    IntroCutscene.ReleaseActor(self)
end
