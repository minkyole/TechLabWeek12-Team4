function BeginPlay()
print("BeginPlay!\n")
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
    local direction = FVector.new(0, 0, 0)

    if IsKeyDown(Key.H) then
        direction.X = direction.X + 1
    end
    if IsKeyDown(Key.J) then
        direction.X = direction.X - 1
    end
    if IsKeyDown(Key.K) then
        direction.Y = direction.Y + 1
    end
    if IsKeyDown(Key.L) then
        direction.Y = direction.Y - 1
    end

    if direction:SizeSquared() > 0 then
        local delta = direction:Normalized() * 2 * dt
        self:Move(delta, self:GetActorRotation())
    end
end



function OnReload()

end
