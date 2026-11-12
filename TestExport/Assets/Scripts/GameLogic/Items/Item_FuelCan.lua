local GameManager = require("GameManager")

function BeginPlay()
    GameManager.RegisterItem(self, "FuelCan")
end

function EndPlay()
    GameManager.UnregisterItem(self)
end
