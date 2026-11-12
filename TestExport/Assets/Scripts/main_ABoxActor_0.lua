local Flashlight = require("Flashlight")

local score = 0
local elapsedTime = 0

function BeginPlay()
    score = 0
    elapsedTime = 0
end

function EndPlay()

end

function OnOverlap(OtherActor)

end

-- Shape의 BeginOverlap은 기존 OnOverlap으로 전달됩니다.
function OnEndOverlap(OtherActor)

end

-- Hit는 Sweep 이동으로 발생하며 Hit 정보는 Lua에 복사되어 전달됩니다.
function OnHit(OtherActor, Hit)
end

function Tick(dt)
    elapsedTime = elapsedTime + dt
    AddUIText("Score: " .. score, 50, FVector2.new(0.5, 0), FVector2.new(0.5, 0), FVector2.new(0, 20))
    AddUIText(string.format("Time: %.1f", elapsedTime), 50,  FVector2.new(1, 0), FVector2.new(1, 0), FVector2.new(-20, 20))
    AddUIImage("Assets/UI/BatteryFill.png", FVector2.new(270, 90), FVector2.new(1, 1), FVector2.new(1, 1), FVector2.new(-20, -20), Flashlight.GetBatteryRatio())
    AddUIImage("Assets/UI/BatteryFrame.png", FVector2.new(270, 90), FVector2.new(1, 1), FVector2.new(1, 1), FVector2.new(-20, -20))
end

function OnReload()

end
