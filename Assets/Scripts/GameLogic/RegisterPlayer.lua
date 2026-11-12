local GameManager = require("GameManager")

function BeginPlay()
    GameManager.RegisterPlayer(self)
end

function EndPlay()
    GameManager.UnregisterPlayer(self)
end
