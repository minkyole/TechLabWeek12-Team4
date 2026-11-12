local GameManager = require("GameManager")

function BeginPlay()
    GameManager.RegisterItem(self, "BatteryLarge")
end

function EndPlay()
    GameManager.UnregisterItem(self)
end
