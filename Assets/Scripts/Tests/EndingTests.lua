package.path = "Assets/Scripts/GameLogic/?.lua;" .. package.path

FVector = {}
FVector2 = {}
FRotator = {}
CursorMode = { Free = 0, Locked = 1 }
Key = { Space = 1, Enter = 2, R = 3 }
MouseButton = { Left = 1 }

function FVector.new(X, Y, Z)
    return { X = X, Y = Y, Z = Z }
end

function FVector2.new(X, Y)
    return { X = X, Y = Y }
end

function FRotator.new(Pitch, Yaw, Roll)
    return { Pitch = Pitch, Yaw = Yaw, Roll = Roll }
end

local Runtime = { mSounds = {}, mNextHandle = 0, mPressed = nil, mLoads = {}, mPassed = 0 }

function SetCursorMode(Mode)
end

function PlaySound(Path, Loop, Volume)
    Runtime.mNextHandle = Runtime.mNextHandle + 1
    Runtime.mSounds[Runtime.mNextHandle] = Path
    return Runtime.mNextHandle
end

function StopSound(Handle)
    Runtime.mSounds[Handle] = nil
end

function IsKeyPressed(Value)
    return Runtime.mPressed == Value
end

function AddUIImage(Path, Size, Anchor, Alignment, Offset)
end

function AddUIText(Text, Size, Anchor, Alignment, Offset)
end

local Ending = require("EndingCutscene")
local GameManager = require("GameManager")
local View = { mCamera = {}, mLocation = nil, mRotation = nil }
local Subject = { mLocation = nil, mRotation = nil }
local Manager = {}
local CameraSupport = assert(loadfile("Assets/Scripts/Tests/CameraSequenceSupport.lua"))()
Ending.RegisterCameraSequence(CameraSupport.New("Car", View), "Car")
Ending.RegisterCameraSequence(CameraSupport.New("Door", View), "Door")

function Manager.GetViewTarget(Self)
    return nil
end

function Manager.GetViewTargetCamera(Self)
    return nil
end

function Manager.RemoveAllCameraModifiers(Self)
end

function Manager.SetViewTargetWithBlend(Self, Target, Duration)
    return true
end

function Manager.IsValid(Self)
    return true
end

function Manager.SetViewTargetCamera(Self, Camera)
end

function View.GetPlayerCameraManager(Self)
    return Manager
end

function View.GetCamera(Self)
    return Self.mCamera
end

function View.SetFogOverride(Self, Density, HeightFalloff, StartDistance, MaxOpacity, Color, Height)
    Self.mFog = { mDensity = Density, mHeightFalloff = HeightFalloff, mStartDistance = StartDistance, mMaxOpacity = MaxOpacity, mColor = Color, mHeight = Height }
    return true
end

function View.ResetFogOverride(Self)
    Self.mFog = nil
    return true
end

function View.SetActorLocation(Self, Location)
    Self.mLocation = Location
end

function View.SetActorRotation(Self, Rotation)
    Self.mRotation = Rotation
end

function View.mCamera.SetFieldOfView(Self, Value)
    Self.mFieldOfView = Value
end

Subject.SetActorLocation = View.SetActorLocation
Subject.SetActorRotation = View.SetActorRotation

function Subject.GetActorLocation(Self)
    return FVector.new(Self.mLocation.X, Self.mLocation.Y, Self.mLocation.Z)
end

View.GetActorLocation = Subject.GetActorLocation

function Subject.GetActorRotation(Self)
    return FRotator.new(Self.mRotation.Pitch, Self.mRotation.Yaw, Self.mRotation.Roll)
end

local function Near(First, Second)
    assert(math.abs(First - Second) < 0.0001, tostring(First) .. " differs from " .. tostring(Second))
end

local function Test(Name, Run)
    Ending.Stop()
    Ending.UnregisterSubject(Subject)
    GameManager.StartGame()
    Runtime.mSounds = {}
    Runtime.mPressed = nil
    Runtime.mLoads = {}
    Run()
    Runtime.mPassed = Runtime.mPassed + 1
    print("PASS " .. Name)
end

Test("each escape freezes gameplay and preserves results across game teardown", function()
    for _, Route in ipairs({ "Car", "Door" }) do
        GameManager.StartGame()
        Ending.SetGameScene("Assets/Scenes/main.scene")
        GameManager.Tick(123.25)
        GameManager.AddBatteryUsed()
        GameManager.Escape(Route)
        assert(not GameManager.IsPlaying() and GameManager.Phase == GameManager.EPhase.Ending)
        GameManager.GameOver("LateCapture")
        GameManager.Escape(Route == "Car" and "Door" or "Car")
        GameManager.Tick(50.0)
        GameManager.Reset()
        Ending.Start(View, "Ending_" .. Route)
        local State = Ending.GetState()
        Near(State.mResult.ElapsedTime, 123.25)
        assert(State.mRoute == Route and State.mResult.BatteriesUsed == 1)
        assert(State.mResult.GameScene == "Assets/Scenes/main.scene")
        Ending.Stop()
    end
end)

Test("car departs and finishes once while negative time does not rewind", function()
    Ending.RegisterSubject(Subject)
    Ending.Start(View, "Ending_Car")
    Ending.Update(-5.0)
    Near(Ending.GetState().mElapsed, 0.0)
    Ending.Update(5.0)
    assert(Subject.mLocation.X > 0.0 and next(Runtime.mSounds) ~= nil)
    Ending.Update(100.0)
    assert(Ending.GetState().mFinished and next(Runtime.mSounds) == nil)
    local X = Subject.mLocation.X
    Ending.Update(3.0)
    Near(Subject.mLocation.X, X)
end)

Test("car camera follows acceleration and releases the departing vehicle", function()
    Ending.RegisterSubject(Subject)
    Ending.Start(View, "Ending_Car")
    for Index = 1, 24 do
        Ending.Update(0.5)
        local DistanceX = Subject.mLocation.X - View.mLocation.X
        local DistanceY = Subject.mLocation.Y - View.mLocation.Y
        if Ending.GetState().mElapsed <= 8.2 then
            assert(DistanceX * DistanceX + DistanceY * DistanceY < 36.0, "camera falls behind before the release")
        end
        if Ending.GetState().mElapsed >= 2.2 and Ending.GetState().mElapsed <= 8.2 then
            Near(DistanceX, 4.3)
        elseif Ending.GetState().mElapsed > 8.2 then
            assert(DistanceX > 4.3)
        end
    end
    assert(Ending.GetState().mFinished)
end)

Test("fence gate slides on the ground in its own orientation and resets after interruption", function()
    local Gate = { mLocation = FVector.new(-60.3, 50.5, 0.0), mRotation = FRotator.new(0.0, 0.0, 0.0) }
    Gate.GetActorLocation = Subject.GetActorLocation
    Gate.GetActorRotation = Subject.GetActorRotation
    Gate.SetActorLocation = View.SetActorLocation
    function Gate.GetNavigationExtent(Self)
        return FVector.new(0.1, 4.15, 2.8)
    end
    local Environment = { self = Gate }
    setmetatable(Environment, { __index = _G })
    setfenv(assert(loadfile("Assets/Scripts/GameLogic/EndingGate.lua")), Environment)()
    for _, Yaw in ipairs({ 0.0, 90.0 }) do
        Subject.mLocation = FVector.new(-11.0, 50.5, 0.0)
        Subject.mRotation = FRotator.new(0.0, 0.0, 0.0)
        Gate.mRotation = FRotator.new(0.0, Yaw, 0.0)
        Environment.BeginPlay()
        Ending.RegisterSubject(Subject, "Car")
        assert(Ending.Start(View, "Car"))
        Ending.Update(require("Transition").Config.Duration + 2.6)
        Near(Gate.mLocation.Z, 0.0)
        assert(math.abs(Gate.mLocation.X + 60.3) + math.abs(Gate.mLocation.Y - 50.5) > 0.0)
        Ending.Update(5.0)
        Near(Gate.mLocation.X, -60.3 - math.sin(math.rad(Yaw)) * 8.5)
        Near(Gate.mLocation.Y, 50.5 + math.cos(math.rad(Yaw)) * 8.5)
        Near(Gate.mLocation.Z, 0.0)
        Ending.Stop()
        Near(Gate.mLocation.X, -60.3)
        Near(Gate.mLocation.Y, 50.5)
        Near(Gate.mLocation.Z, 0.0)
        Near(Subject.mLocation.X, -11.0)
        Environment.EndPlay()
    end
end)

Test("door unlocks, opens, is crossed, and closes behind the camera", function()
    Ending.RegisterSubject(Subject)
    Ending.Start(View, "Ending_Door")
    Ending.Update(3.5)
    assert(Subject.mRotation.Yaw < -100.0 and View.mLocation.X < 0.0)
    Ending.Update(4.0)
    assert(View.mLocation.X > 0.0)
    Ending.Update(2.5)
    Near(Subject.mRotation.Yaw, 0.0)
    Ending.Update(1.0)
    assert(Ending.GetState().mFinished and next(Runtime.mSounds) == nil)
end)

Test("camera transitions, both exits and interrupted endings preserve the world's distance fog", function()
    for _, Route in ipairs({ "Car", "Door" }) do
        for _, Yaw in ipairs({ 0.0, 90.0 }) do
            Subject.mLocation = FVector.new(12.0, -4.0, 0.5)
            Subject.mRotation = FRotator.new(0.0, Yaw, 0.0)
            Ending.RegisterSubject(Subject, Route)
            local WorldFog = { mDensity = 0.08, mStartDistance = 45.0 }
            View.mFog = WorldFog
            assert(Ending.Start(View, Route))
            assert(View.mFog == WorldFog, "starting a cutscene replaced the world's fog")
            for Frame = 1, 140 do
                Ending.Update(0.1)
                assert(View.mFog == WorldFog, "the cutscene changed fog during camera movement or fading")
            end
            Ending.Finish()
            Ending.Stop()
            assert(View.mFog == WorldFog, "ending cleanup cleared the world's distance fog")
            Ending.UnregisterSubject(Subject)
        end
    end
end)

Test("skip and interrupted scene cleanup stop all cues and release actors", function()
    Ending.RegisterSubject(Subject)
    Ending.Start(View, "Ending_Door")
    Ending.Update(4.0)
    assert(next(Runtime.mSounds) ~= nil)
    Ending.UnregisterSubject(Subject)
    Ending.Update(1.0)
    Ending.Finish()
    Ending.Finish()
    assert(Ending.GetState().mFinished and next(Runtime.mSounds) == nil)
    Ending.Stop()
    assert(Ending.GetState() == nil)
end)

Test("standalone scene preview and new games discard previous results", function()
    GameManager.Escape("Car")
    GameManager.StartGame()
    Ending.RegisterView(View)
    Ending.RegisterSubject(Subject, "Car")
    assert(not Ending.StartPending())
    Ending.Start(View, "Ending_Door")
    assert(Ending.GetState().mRoute == "Door")
    Near(Ending.GetState().mResult.ElapsedTime, 0.0)
    assert(Ending.GetState().mResult.BatteriesUsed == 0)
end)

Test("skip inputs cannot activate the result menu before credits finish", function()
    Subject.mLocation = FVector.new(-11.0, 50.5, 0.0)
    Subject.mRotation = FRotator.new(0.0, 0.0, 0.0)
    Ending.SetGameScene("Assets/Scenes/Demo/06_Game.scene")
    Ending.RegisterSubject(Subject, "Car")
    GameManager.Escape("Car")
    local Environment = { self = View }
    setmetatable(Environment, { __index = _G })
    function Environment.ResetTimeEffects()
    end
    function Environment.GetViewportSize()
        return FVector2.new(1280, 720)
    end
    function Environment.GetMousePosition()
        return FVector2.new(0, 0)
    end
    function Environment.IsMouseOverGameViewport()
        return false
    end
    function Environment.IsMousePressed(Value)
        return false
    end
    function Environment.LoadScene(Path)
        Runtime.mLoads[#Runtime.mLoads + 1] = Path
    end
    local Script = assert(loadfile("Assets/Scripts/GameLogic/EndingActor.lua"))
    setfenv(Script, Environment)
    Script()
    Environment.BeginPlay()
    Environment.Tick(require("Transition").Config.Duration + 1.0)
    Runtime.mPressed = Key.Enter
    Environment.Tick(0.016)
    assert(Ending.GetState().mFinished and #Runtime.mLoads == 0)
    Runtime.mPressed = nil
    Environment.Tick(0.016)
    Runtime.mPressed = Key.R
    Environment.Tick(0.016)
    assert(#Runtime.mLoads == 0)
    Runtime.mPressed = nil
    Environment.Tick(0.6)
    Runtime.mPressed = Key.Enter
    Environment.Tick(0.016)
    assert(#Runtime.mLoads == 0)
    Runtime.mPressed = nil
    Environment.Tick(0.016)
    Runtime.mPressed = Key.R
    Environment.Tick(0.016)
    assert(#Runtime.mLoads == 1 and Runtime.mLoads[1] == "Assets/Scenes/Demo/06_Game.scene")
    Environment.Tick(0.016)
    assert(#Runtime.mLoads == 1)
    Environment.EndPlay()
    assert(Ending.GetState() == nil)
end)

Test("each route plays only its own fade after the cinematic and holds completion until black", function()
    local Sequences = {}
    for _, Route in ipairs({ "Car", "Door" }) do
        local Sequence = { mStarts = 0, mStops = 0, mFinished = false, mPlaying = false }
        function Sequence.PlaySequence(Self)
            Self.mStarts = Self.mStarts + 1
            Self.mPlaying = true
            return true
        end
        function Sequence.StopSequence(Self)
            Self.mStops = Self.mStops + 1
            Self.mFinished = false
            Self.mPlaying = false
            return true
        end
        function Sequence.IsSequenceFinished(Self)
            return Self.mFinished
        end
        Sequences[Route] = Sequence
        Ending.RegisterFadeSequence(Sequence, Route)
    end
    for _, Route in ipairs({ "Car", "Door" }) do
        local Sequence = Sequences[Route]
        local Other = Sequences[Route == "Car" and "Door" or "Car"]
        local OtherStarts = Other.mStarts
        Ending.RegisterSubject(Subject)
        Ending.Start(View, "Ending_" .. Route)
        Ending.Update(Ending.Config[Route].Duration - 0.1)
        assert(Sequence.mStarts == 0 and not Ending.GetState().mFading)
        Ending.Update(0.1)
        local State = Ending.GetState()
        assert(State.mFading and not State.mFinished and Sequence.mStarts == 1)
        assert(Other.mStarts == OtherStarts)
        local LocationX = Subject.mLocation.X
        Ending.BeginFadeOut()
        Ending.Update(1.0)
        assert(State.mFading and not State.mFinished and Sequence.mStarts == 1)
        if Route == "Car" then
            assert(Subject.mLocation.X > LocationX, "car must keep moving during fade")
            assert(Subject.mLocation.X - View.mLocation.X > 4.3)
        end
        Sequence.mFinished = true
        Ending.Update(0.016)
        assert(State.mFinished and not State.mFading and next(Runtime.mSounds) == nil)
        assert(Sequence.mPlaying, "completed fade must hold black through credits")
        Ending.Stop()
        assert(not Sequence.mPlaying and Sequence.mStops == 2)
    end
    for _, Sequence in pairs(Sequences) do
        Ending.UnregisterFadeSequence(Sequence)
    end
end)

Test("skipping starts fade once and interrupted fade releases native playback", function()
    local Sequence = { mStarts = 0, mPlaying = false }
    function Sequence.PlaySequence(Self)
        Self.mStarts = Self.mStarts + 1
        Self.mPlaying = true
        return true
    end
    function Sequence.StopSequence(Self)
        Self.mPlaying = false
        return true
    end
    function Sequence.IsSequenceFinished(Self)
        return false
    end
    Ending.RegisterFadeSequence(Sequence, "Door")
    Ending.Start(View, "Ending_Door")
    Ending.Update(1.0)
    assert(Ending.BeginFadeOut())
    assert(not Ending.BeginFadeOut())
    Ending.Update(0.25)
    assert(Ending.GetState().mFading and not Ending.GetState().mFinished and Sequence.mStarts == 1)
    Ending.Stop()
    assert(not Sequence.mPlaying and Ending.GetState() == nil)
    Ending.Start(View, "Ending_Door")
    assert(Ending.BeginFadeOut())
    Ending.UnregisterFadeSequence(Sequence)
    Ending.Update(0.016)
    assert(Ending.GetState().mFinished)
end)

Ending.Stop()
GameManager.Reset()
print("Ending tests passed: " .. Runtime.mPassed)
