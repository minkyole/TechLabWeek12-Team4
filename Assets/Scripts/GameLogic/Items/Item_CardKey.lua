local GameManager = require("GameManager")

function BeginPlay()
    GameManager.RegisterItem(self, "CardKey")
end

function EndPlay()
    GameManager.UnregisterItem(self)
end
