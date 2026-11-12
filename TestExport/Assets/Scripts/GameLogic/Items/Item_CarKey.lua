local GameManager = require("GameManager")

function BeginPlay()
    GameManager.RegisterItem(self, "CarKey")
end

function EndPlay()
    GameManager.UnregisterItem(self)
end
