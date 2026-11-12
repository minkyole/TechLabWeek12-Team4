local State = { Steps = 0, LastName = "", LastTime = -1, HitWindow = false, ChargeHandle = 0, Clock = 0 }

local LandShake = {
    Type = "Perlin", Duration = 0.4, BlendInTime = 0.02, BlendOutTime = 0.3,
    Pitch = { Amplitude = 3.0, Frequency = 20.0 }, Roll = { Amplitude = 2.0, Frequency = 18.0 },
}

function BeginPlay()
print("BeginPlay!\n")
	local camera = self:GetCamera()
	local manager = GetPlayerCameraManager()

	if camera ~= nil and manager ~= nil then
		assert(manager:SetViewTargetCamera(camera))
	end

	local movement = self:GetMovementComponent()

	if movement == nil then
		return
	end

	movement:SetMaxSpeed(5.0)
	movement:SetAcceleration(20.0)
	movement:SetBrakingDeceleration(30.0)
	movement:SetGravityAcceleration(20.0)
	movement:SetGravityScale(1.0)
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

local function Clamp01(value)
    return math.max(0, math.min(value, 1))
end

local walkSpeed = 5.0
local runSpeed = 10.0
local jumpSpeed = 10.0
local attackReadySpeed = 2.0

local attackReady = 0

function Tick(dt)
    State.Clock = State.Clock + dt
    AddUIText("Steps: " .. State.Steps, 28, FVector2.new(0.0, 0.0), FVector2.new(0.0, 0.0), FVector2.new(30.0, 30.0))
    if State.Clock - State.LastTime < 0.5 then
        AddUIText(State.LastName, 48, FVector2.new(0.5, 0.35), FVector2.new(0.5, 0.5))
    end
    if State.HitWindow then
        AddUIText("HIT WINDOW", 40, FVector2.new(0.5, 0.8), FVector2.new(0.5, 0.5))
    end


	-- print("Tick!\n")
	local movement = self:GetMovementComponent()

    if movement == nil then
        return
    end

    local x = 0.0
    local y = 0.0

    local mesh = self:FindSceneComponentByName("SkeletalMeshComponent")
    if mesh == nil then
        print("mesh not found")
        return
    end

    local rotation = mesh:GetRelativeRotation()

    if IsKeyDown(Key.W) then 
        x = x + 1.0 
    end
    if IsKeyDown(Key.S) then 
        x = x - 1.0 
    end
    if IsKeyDown(Key.D) then 
        y = y + 1.0 
    end
    if IsKeyDown(Key.A) then 
        y = y - 1.0 
    end
    -- Velocity is in world space; mesh rotation is relative to the actor.
    local velocity = movement:GetVelocity()
    local horizontalSpeedSquared = velocity.X * velocity.X + velocity.Y * velocity.Y
    if horizontalSpeedSquared > 0.0001 then
        local movementYaw = math.deg(math.atan2(velocity.Y, velocity.X))
        local actorYaw = self:GetActorRotation().Yaw
        -- This mesh faces -X at zero relative yaw.
        rotation.Yaw = (movementYaw - actorYaw + 180.0 + 180.0) % 360.0 - 180.0
        mesh:SetRelativeRotation(rotation)
    end

    local yaw = math.rad(self:GetActorRotation().Yaw)
    local cosYaw = math.cos(yaw)
    local sinYaw = math.sin(yaw)

    local worldDirection = FVector.new(
        x * cosYaw - y * sinYaw,
        x * sinYaw + y * cosYaw,
        0.0
    )

    movement:SetMaxSpeed(
        IsKeyDown(Key.Shift) and runSpeed or walkSpeed
    )

    -- 입력이 없을 때도 (0, 0, 0)을 전달한다.
    -- 대각선 정규화는 C++ SetMoveInput에서 처리한다.
    movement:SetMoveInput(worldDirection)

	-- 이동 규칙은 AnimInstance 준비 여부와 독립적으로 실행한다.
	if IsKeyPressed(Key.Space) and movement:IsGrounded() then
		movement:SetVerticalVelocity(jumpSpeed)
	end

    if IsMouseDown(MouseButton.Right) then
        attackReady = attackReady + attackReadySpeed * dt
    else
        attackReady = attackReady - attackReadySpeed * dt
    end
    attackReady = Clamp01(attackReady)


	-- 메시 교체 등으로 AnimInstance가 재생성될 수 있으므로
	-- 장기간 저장하지 않고 사용할 때 조회한다.
	local anim = self:GetAnimInstance()

	if anim == nil or not anim:IsGraphReady() then
		return
	end

	-- 누른 순간 한 번만 공격 요청
	if IsMousePressed(MouseButton.Left) then
		assert(
			anim:SetTrigger("AttackRequested"),
			anim:GetLastError()
		)
	end

	-- 피격 테스트
	if IsKeyPressed(Key.H) then
		assert(
			anim:SetTrigger("HitRequested"),
			anim:GetLastError()
		)
	end

	if IsKeyPressed(Key.Y) then
		assert(
			anim:SetTrigger("DeathRequested"),
			anim:GetLastError()
		)
	end

    if IsKeyPressed(Key.R) then
        assert(
            anim:SetTrigger("RollRequested"),
            anim:GetLastError()
        )
    end

	-- 애니메이션 파라미터 업데이트
	anim:SetFloat("Speed", movement:GetGroundSpeed())
    anim:SetBool("IsGrounded", movement:IsGrounded())
    anim:SetFloat("AttackReady", attackReady)
end

function OnReload()
print("Reload!\n")
end

function OnAnimNotify(Name)
    State.LastName, State.LastTime = Name, State.Clock
    if Name == "LeftFoot" then
        PlaySound("Assets/Sounds/FootstepLeft.wav", false, 0.8)
        State.Steps = State.Steps + 1
    elseif Name == "RightFoot" then
        PlaySound("Assets/Sounds/FootstepRight.wav", false, 0.8)
        State.Steps = State.Steps + 1
    elseif Name == "Land" then
        PlaySound("Assets/Sounds/JumpLand.wav", false, 1.0)
        local Manager = GetPlayerCameraManager()
        if Manager then
            Manager:AddCameraModifier("UCameraModifier_CameraShake", { Pattern = LandShake, Scale = 1.0 })
        end
    end
    print("[Notify] " .. Name)
end

function OnAnimNotifyBegin(Name)
    if Name == "SlowMo" then SetGlobalTimeDilation(0.3)
    elseif Name == "HitWindow" then
        State.HitWindow = true
        SetGlobalTimeDilation(0.3)
        PlaySound("Assets/Sounds/SwordSwing.wav", false, 0.9)
    elseif Name == "Charge" then State.ChargeHandle = PlaySound("Assets/Sounds/심장소리.mp3", true, 0.7)
    end
    print("[Notify Begin] " .. Name)
end

function OnAnimNotifyEnd(Name)
    if Name == "SlowMo" then SetGlobalTimeDilation(1.0)
    elseif Name == "HitWindow" 
        then State.HitWindow = false
        SetGlobalTimeDilation(1.0)
    elseif Name == "Charge" and State.ChargeHandle ~= 0 then StopSound(State.ChargeHandle); State.ChargeHandle = 0
    end
    print("[Notify End] " .. Name)
end