local GameManager = require("GameManager")

function BeginPlay()
    GameManager.RegisterItem(self, "BatterySmall")
end

function EndPlay()
    GameManager.UnregisterItem(self)
end
