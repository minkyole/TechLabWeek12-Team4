package.path = "Assets/Scripts/GameLogic/?.lua;" .. package.path

FVector = {}
FRotator = {}
CursorMode = { Free = 0, Locked = 1 }

function FVector.new(X, Y, Z)
    return { X = X, Y = Y, Z = Z }
end

function FRotator.new(Pitch, Yaw, Roll)
    return { Pitch = Pitch, Yaw = Yaw, Roll = Roll }
end

local Runtime = { mSounds = {}, mNextHandle = 0, mPassed = 0 }

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

local Intro = require("IntroCutscene")
local GameManager = require("GameManager")
local Data = require("MapData")
local Map = require("Map").New(Data)
local Config = Intro.Config
local Player = { mLocation = FVector.new(0, 0, 0), mRotation = FRotator.new(0, 0, 0) }
local View = {}
local Door = {}
local Camera = { mLocation = FVector.new(0, 0, 1.38) }
local Model = { mLocation = FVector.new(0.49, 0.19, -0.09) }
local Manager = { mTarget = Camera, mBlendDuration = 0.0 }
local Fade = { mPlaying = false }
local CameraSupport = assert(loadfile("Assets/Scripts/Tests/CameraSequenceSupport.lua"))()
local Dolly = CameraSupport.New("IntroCamera", View)
local Bob = CameraSupport.New("IntroBob", View)

function Player.SetActorLocation(Self, Location)
    Self.mLocation = Location
end

function Player.GetActorLocation(Self)
    return Self.mLocation
end

function Player.SetActorRotation(Self, Rotation)
    Self.mRotation = Rotation
end

function View.GetPlayerCameraManager(Self)
    return Manager
end

function View.GetCamera(Self)
    return Camera
end

function View.SetActorLocation(Self, Location)
    Self.mLocation = Location
end

function View.SetActorRotation(Self, Rotation)
    Self.mRotation = Rotation
end

function Door.SetActorLocation(Self, Location)
    Self.mLocation = Location
end

function Camera.FindChildComponent(Self, Name)
    assert(Name == "FlashlightMesh")
    return Model
end

function Camera.GetRelativeLocation(Self)
    return Self.mLocation
end

function Camera.SetRelativeLocation(Self, Location)
    Self.mLocation = Location
end

function Model.GetRelativeLocation(Self)
    return Self.mLocation
end

function Model.SetRelativeLocation(Self, Location)
    Self.mLocation = Location
end

function Manager.SetViewTarget(Self, Target)
    Self.mTarget = Target
    return true
end

function Manager.SetViewTargetCamera(Self, Target, Duration)
    Self.mTarget = Target
    Self.mBlendDuration = Duration or 0.0
    return true
end

function Fade.PlaySequence(Self)
    Self.mPlaying = true
    return true
end

function Fade.StopSequence(Self)
    Self.mPlaying = false
    return true
end

local function Test(Name, Run)
    GameManager.Reset()
    GameManager.RegisterPlayer(Player)
    Intro.RegisterView(View)
    Intro.RegisterPlayerView(View)
    Intro.RegisterDoor(Door)
    Intro.RegisterFade(Fade)
    Intro.RegisterCameraSequence(Dolly, false)
    Intro.RegisterCameraSequence(Bob, true)
    Run()
    Runtime.mPassed = Runtime.mPassed + 1
    print("PASS " .. Name)
end

for Floor, Text in pairs(Data.Floors) do
    Map:LoadFloor(Floor, Text)
end
require("MapObstacles").Apply(Map, Data.Dressing)

Test("intro waits for registrations and freezes game progress and captures", function()
    Intro.ReleaseActor(View)
    GameManager.StartGame(true)
    GameManager.Tick(1.0)
    assert(GameManager.Phase == GameManager.EPhase.Intro and Intro.GetState() == nil)
    Intro.RegisterView(View)
    Intro.RegisterPlayerView(View)
    GameManager.Tick(0.0)
    assert(Manager.mTarget == View and Fade.mPlaying)
    assert(Player.mLocation.X == Config.Spawn.X and Model.mLocation.Z == -10.0)
    GameManager.GameOver("CaughtByEnemy")
    GameManager.Escape("Door")
    assert(GameManager.Phase == GameManager.EPhase.Intro and GameManager.ElapsedTime == 0.0)
end)

Test("arrival stays on clear map cells, crosses the open door, closes it and hands off once", function()
    GameManager.StartGame(true)
    local Crossed = false
    for Index = 1, math.ceil(Config.Duration / 0.1) do
        GameManager.Tick(0.1)
        assert(Map:IsWalkable(Map:WorldToCell(Player.mLocation)), "intro player is inside a navigation obstacle")
        if Player.mLocation.X > 0.5 and Player.mLocation.X < 1.5 then
            Crossed = true
            assert(Door.mLocation.Z - 1.5 > 1.0, "shutter blocks the player")
        end
    end
    if GameManager.Phase == GameManager.EPhase.Intro then
        GameManager.Tick(0.001)
    end
    assert(Crossed and GameManager.IsPlaying())
    assert(GameManager.ElapsedTime == 0.0 and Player.mLocation.X == Config.Destination.X)
    assert(Manager.mTarget == Camera and Door.mLocation.Z == Config.Door.Z)
    assert(Camera.mLocation.Z == 1.38 and Model.mLocation.X == 0.49 and not Fade.mPlaying)
    assert(next(Runtime.mSounds) == nil)
    local Location = Player.mLocation
    GameManager.Tick(0.25)
    assert(Player.mLocation == Location and GameManager.ElapsedTime == 0.25)
end)

Test("skip from black, camera blend and doorway restores the same playable state", function()
    for _, Time in ipairs({ 0.0, Config.BlendStart + 0.5, Config.ApproachEnd + 1.5, Config.LookBackStart + 0.5 }) do
        GameManager.StartGame(true)
        GameManager.Tick(Time)
        Intro.Finish()
        Intro.Finish()
        GameManager.Tick(0.0)
        assert(GameManager.IsPlaying() and GameManager.ElapsedTime == 0.0)
        assert(Player.mLocation.X == Config.Destination.X and Manager.mTarget == Camera)
        assert(Model.mLocation.X == 0.49 and not Fade.mPlaying and next(Runtime.mSounds) == nil)
    end
end)

Test("negative time, interrupted teardown and replay restore model and camera", function()
    GameManager.StartGame(true)
    GameManager.Tick(4.0)
    local Time = Intro.GetState().mElapsed
    local BlendDuration = Manager.mBlendDuration
    GameManager.Tick(-1.0)
    assert(Intro.GetState().mElapsed == Time and Manager.mBlendDuration == BlendDuration)
    Intro.ReleaseActor(View)
    assert(Intro.GetState() == nil and Manager.mTarget == Camera and not Fade.mPlaying)
    assert(Model.mLocation.X == 0.49 and Camera.mLocation.Z == 1.38 and next(Runtime.mSounds) == nil)
    Intro.RegisterView(View)
    Intro.RegisterPlayerView(View)
    GameManager.StartGame(true)
    GameManager.Tick(0.0)
    assert(Intro.GetState().mElapsed == 0.0 and Player.mLocation.X == Config.Spawn.X)
    GameManager.Reset()
    assert(GameManager.Phase == GameManager.EPhase.Ready and Intro.GetState() == nil)
end)

Test("sample scenes can start directly without the arrival actors", function()
    GameManager.StartGame()
    GameManager.Tick(0.2)
    assert(GameManager.IsPlaying() and GameManager.ElapsedTime == 0.2)
    assert(Intro.GetState() == nil)
end)

print("Intro tests passed: " .. Runtime.mPassed)
