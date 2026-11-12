local EndingCutscene = require("EndingCutscene")

function BeginPlay()
    EndingCutscene.RegisterFadeSequence(self, "Car")
end

function EndPlay()
    EndingCutscene.UnregisterFadeSequence(self)
end
