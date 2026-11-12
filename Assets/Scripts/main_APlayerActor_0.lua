local Inventory = require("Inventory")
local Stamina = require("Stamina")
local WalkingCamera = require("WalkingCamera")
local HorrorCamera = require("HorrorCamera")
local GameManager = require("GameManager")

local MouseSensitivityX = 0.12
local MouseSensitivityY = 0.12
local WalkSpeed = 3.0
local RunSpeed = 5.0
local WalkingStateKey = sceneName .. ":" .. self:GetName()
local Runtime = { mHeadBobStrength = 1.0, mLastHeadBobStrength = 1.0, mViewModelStrength = 1.0, mLastViewModelStrength = 1.0, mLightMotionStrength = 0.2, mLastLightMotionStrength = 0.2 }

local function Initialize()
    local Rotation = self:GetActorRotation()
    Runtime.mHeadBobStrength, Runtime.mViewModelStrength, Runtime.mLightMotionStrength = WalkingCamera.Start(WalkingStateKey, self, Rotation.Pitch)
    HorrorCamera.Start(WalkingStateKey, self)
    Runtime.mLastHeadBobStrength = Runtime.mHeadBobStrength > 0.0 and Runtime.mHeadBobStrength or 1.0
    Runtime.mLastViewModelStrength = Runtime.mViewModelStrength > 0.0 and Runtime.mViewModelStrength or 1.0
    Runtime.mLastLightMotionStrength = Runtime.mLightMotionStrength > 0.0 and Runtime.mLightMotionStrength or 0.2
    self:SetActorRotation(FRotator.new(0.0, Rotation.Yaw, 0.0))
end

function BeginPlay()
    print("BeginPlay!\n")
    Stamina.Reset()
    Initialize()
    Log("[PlayerCamera] F6: head bob on/off, F7/F8: lower/higher strength, [: escape items, ]: narrower view")
    Log("[PlayerCamera] F9: flashlight motion on/off, F10/F11: lower/higher strength, B or F12: light motion on/off")
end

function EndPlay()
    if IsExecutableGame() then
        SetCursorMode(CursorMode.Free)
    end
    WalkingCamera.Stop(WalkingStateKey)
    HorrorCamera.Stop(WalkingStateKey)
end

function OnOverlap(OtherActor)
    if GameManager.IsEnemy(OtherActor) then
        GameManager.GameOver("CaughtByEnemy", OtherActor)
    end
end

-- Shape의 BeginOverlap은 기존 OnOverlap으로 전달됩니다.
function OnEndOverlap(OtherActor)
end

-- Hit는 Sweep 이동으로 발생하며 Hit 정보는 Lua에 복사되어 전달됩니다.
function OnHit(OtherActor, Hit)
    WalkingCamera.OnHit(WalkingStateKey, Hit)
end

function Tick(DeltaTime)
    local ExecutableGame = IsExecutableGame()
    if ExecutableGame and GameManager.IsPlaying() and (IsKeyPressed(Key.Num0) or IsKeyPressed(Key.NumPad0)) then
        SetCursorMode(GetCursorMode() == CursorMode.Locked and CursorMode.Free or CursorMode.Locked)
    end
    local MouseCaptured = ExecutableGame and SetGameMouseCapture(GameManager.IsPlaying() and GetCursorMode() == CursorMode.Locked) or false
    if DeltaTime <= 0.0 then
        return
    end
    if GameManager.Phase == GameManager.EPhase.Intro or GameManager.IsJumpScaring() or GameManager.Phase == GameManager.EPhase.GameOver or GameManager.Phase == GameManager.EPhase.Ending or GameManager.Phase == GameManager.EPhase.Escaped then
        HorrorCamera.Update(WalkingStateKey, FVector.new(0.0, 0.0, 0.0), DeltaTime, false, false)
        return
    end

    local HeadBobStrength = Runtime.mHeadBobStrength
    if IsKeyPressed(Key.F6) then
        HeadBobStrength = HeadBobStrength > 0.0 and 0.0 or Runtime.mLastHeadBobStrength
    end
    if IsKeyPressed(Key.F7) then
        HeadBobStrength = HeadBobStrength - 0.25
    end
    if IsKeyPressed(Key.F8) then
        HeadBobStrength = HeadBobStrength + 0.25
    end
    if HeadBobStrength ~= Runtime.mHeadBobStrength then
        Runtime.mHeadBobStrength = WalkingCamera.SetHeadBobStrength(WalkingStateKey, HeadBobStrength)
        if Runtime.mHeadBobStrength > 0.0 then
            Runtime.mLastHeadBobStrength = Runtime.mHeadBobStrength
        end
        Log("[PlayerCamera] Head bob:", string.format("%.0f%%", Runtime.mHeadBobStrength * 100.0))
    end

    local ViewModelStrength = Runtime.mViewModelStrength
    if IsKeyPressed(Key.F9) then
        ViewModelStrength = ViewModelStrength > 0.0 and 0.0 or Runtime.mLastViewModelStrength
    end
    if IsKeyPressed(Key.F10) then
        ViewModelStrength = ViewModelStrength - 0.25
    end
    if IsKeyPressed(Key.F11) then
        ViewModelStrength = ViewModelStrength + 0.25
    end
    if ViewModelStrength ~= Runtime.mViewModelStrength then
        Runtime.mViewModelStrength = WalkingCamera.SetViewModelStrength(WalkingStateKey, ViewModelStrength)
        if Runtime.mViewModelStrength > 0.0 then
            Runtime.mLastViewModelStrength = Runtime.mViewModelStrength
        end
        Log("[PlayerCamera] Flashlight motion:", string.format("%.0f%%", Runtime.mViewModelStrength * 100.0))
    end
    if IsKeyPressed(Key.B) or IsKeyPressed(Key.F12) then
        Runtime.mLightMotionStrength = WalkingCamera.SetLightMotionStrength(WalkingStateKey, Runtime.mLightMotionStrength > 0.0 and 0.0 or Runtime.mLastLightMotionStrength)
        Log("[PlayerCamera] Light motion:", string.format("%.0f%%", Runtime.mLightMotionStrength * 100.0))
    end

    local FieldOfViewDelta = 0.0
    if IsKeyPressed(Key.RightBracket) then
        FieldOfViewDelta = FieldOfViewDelta - 2.0
    end
    if FieldOfViewDelta ~= 0.0 then
        local FieldOfView = WalkingCamera.AdjustFieldOfView(WalkingStateKey, FieldOfViewDelta)
        if FieldOfView ~= nil then
            Log("[PlayerCamera] Vertical field of view:", FieldOfView)
        end
    end

    if not GameManager.IsPlaying() then
        WalkingCamera.Update(WalkingStateKey, FVector.new(0.0, 0.0, 0.0), 0.0, DeltaTime, false, false)
        HorrorCamera.Update(WalkingStateKey, FVector.new(0.0, 0.0, 0.0), DeltaTime, false, false)
        return
    end

    if MouseCaptured or (not ExecutableGame and IsMouseDown(MouseButton.Right)) then
        local MouseDeltaX = GetMouseDeltaX()
        local MouseDeltaY = GetMouseDeltaY()

        if MouseDeltaX ~= 0 or MouseDeltaY ~= 0 then
            local Rotation = self:GetActorRotation()
            Rotation.Yaw = (Rotation.Yaw + MouseDeltaX * MouseSensitivityX + 180) % 360 - 180
            self:SetActorRotation(Rotation)
            WalkingCamera.Look(WalkingStateKey, MouseDeltaY * MouseSensitivityY, MouseDeltaX * MouseSensitivityX)
        end
    end

    local Direction = FVector.new(0, 0, 0)

    if IsKeyDown(Key.W) then
        Direction.X = Direction.X + 1
    end
    if IsKeyDown(Key.S) then
        Direction.X = Direction.X - 1
    end
    if IsKeyDown(Key.D) then
        Direction.Y = Direction.Y + 1
    end
    if IsKeyDown(Key.A) then
        Direction.Y = Direction.Y - 1
    end

    local IsMoving = Direction:SizeSquared() > 0.0
    local IsRunning = Stamina.Tick(DeltaTime, IsKeyDown(Key.Shift), IsMoving)
    local Speed = (IsRunning and RunSpeed or WalkSpeed) * Inventory.GetSpeedMultiplier()
    local Rotation = self:GetActorRotation()
    local YawRadians = math.rad(Rotation.Yaw)
    local CosYaw = math.cos(YawRadians)
    local SinYaw = math.sin(YawRadians)
    local Movement = FVector.new(0, 0, 0)

    if IsMoving then
        local LocalDirection = Direction:Normalized()
        local WorldDirection = FVector.new(LocalDirection.X * CosYaw - LocalDirection.Y * SinYaw, LocalDirection.X * SinYaw + LocalDirection.Y * CosYaw, 0.0)
        local Delta = WorldDirection * Speed * DeltaTime
        local PreviousLocation = self:GetActorLocation()
        self:Move(FVector.new(Delta.X, 0, 0), Rotation)
        self:Move(FVector.new(0, Delta.Y, 0), Rotation)
        Movement = self:GetActorLocation() - PreviousLocation
    end

    local LocalMovement = FVector.new(Movement.X * CosYaw + Movement.Y * SinYaw, -Movement.X * SinYaw + Movement.Y * CosYaw, 0.0)
    WalkingCamera.Update(WalkingStateKey, LocalMovement, Speed, DeltaTime, IsRunning)
    HorrorCamera.Update(WalkingStateKey, LocalMovement, DeltaTime, IsRunning)
end



function OnReload()
    Initialize()
end
