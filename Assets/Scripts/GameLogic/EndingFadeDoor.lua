local EndingCutscene = require("EndingCutscene")

function BeginPlay()
    EndingCutscene.RegisterFadeSequence(self, "Door")
end

function EndPlay()
    EndingCutscene.UnregisterFadeSequence(self)
end
