local score = 0
local elapsedTime = 0

function BeginPlay()
    score = 0
    elapsedTime = 0
end

function EndPlay()
print("EndPlay!\n")
end

function OnOverlap(OtherActor)
print("OnOverlap!\n")
end

-- Shape의 BeginOverlap은 기존 OnOverlap으로 전달됩니다.
function OnEndOverlap(OtherActor)
print("OnEndOverlap!\n")
end

-- Hit는 Sweep 이동으로 발생하며 Hit 정보는 Lua에 복사되어 전달됩니다.
function OnHit(OtherActor, Hit)
print("OnHit!", Hit.ImpactPoint.X, Hit.ImpactPoint.Y, Hit.ImpactPoint.Z)
end

function Tick(dt)
    elapsedTime = elapsedTime + dt
    -- 점수는 가운데 위, 시간은 오른쪽 위, 배터리는 오른쪽 아래에 배치한다.
    AddUIText("Score: " .. score, 50, FVector2.new(0.5, 0), FVector2.new(0.5, 0), FVector2.new(0, 20))
    AddUIText(string.format("Time: %.1f", elapsedTime), 50,
        FVector2.new(1, 0), FVector2.new(1, 0), FVector2.new(-20, 20))
    AddUIImage("Assets/UI/Battery.png", FVector2.new(270, 90),
        FVector2.new(1, 1), FVector2.new(1, 1), FVector2.new(-20, -20))
end


function OnReload()
print("Reload!\n")
end
