package.path = "Assets/Scripts/GameLogic/?.lua;" .. package.path

FVector = { new = function(X, Y, Z) return { X = X, Y = Y, Z = Z } end }
FVector2 = { new = function(X, Y) return { X = X, Y = Y } end }
FRotator = { new = function(Pitch, Yaw, Roll) return { Pitch = Pitch, Yaw = Yaw, Roll = Roll } end }
Key = { E = 1 }
Log = function(...) end
AddUIText = function(...) end

local Sounds = {}
local SoundCalls = {}
local NextHandle = 0

function PlaySound(Path, Loop, Volume)
    NextHandle = NextHandle + 1
    Sounds[NextHandle] = { mPath = Path, mLoop = Loop, mVolume = Volume, mPlaying = true }
    SoundCalls[#SoundCalls + 1] = Sounds[NextHandle]
    return NextHandle
end

function StopSound(Handle)
    if Sounds[Handle] ~= nil then
        Sounds[Handle].mPlaying = false
    end
end

function IsSoundPlaying(Handle)
    return Sounds[Handle] ~= nil and Sounds[Handle].mPlaying
end

function SetSoundVolume(Handle, Volume)
    Sounds[Handle].mVolume = Volume
end

local GameManager = require("GameManager")
local MapModule = require("Map")
local Door = require("Door")
local DoorAudio = require("DoorAudio")
local EnemyNavigation = require("EnemyNavigation")
local FSM = require("FSM")
local Actor = {}
Actor.__index = Actor
local RegisteredDoors = {}
local Passed = 0

function Actor.New(X, Y, Z, Yaw, Extent)
    return setmetatable({ mLocation = FVector.new(X, Y, Z), mRotation = FRotator.new(0.0, Yaw or 0.0, 0.0), mExtent = Extent or FVector.new(0.4, 0.4, 1.0), mObserved = false }, Actor)
end

function Actor.GetActorLocation(Self)
    return FVector.new(Self.mLocation.X, Self.mLocation.Y, Self.mLocation.Z)
end

function Actor.SetActorLocation(Self, Location)
    Self.mLocation = FVector.new(Location.X, Location.Y, Location.Z)
    return true
end

function Actor.GetActorRotation(Self)
    return FRotator.new(Self.mRotation.Pitch, Self.mRotation.Yaw, Self.mRotation.Roll)
end

function Actor.SetActorRotation(Self, Rotation)
    Self.mRotation = FRotator.new(Rotation.Pitch, Rotation.Yaw, Rotation.Roll)
    return true
end

function Actor.GetNavigationExtent(Self)
    return Self.mExtent
end

function Actor.Move(Self, Delta, Rotation)
    Self.mLocation = FVector.new(Self.mLocation.X + Delta.X, Self.mLocation.Y + Delta.Y, Self.mLocation.Z + Delta.Z)
    Self.mRotation = Rotation
    return true
end

function Actor.GetName(Self)
    return "DoorTestActor"
end

function Actor.GetCamera(Self)
    return Self
end

function Actor.CanSeeActor(Self, Other)
    return Self.mObserved
end

local function Near(Value, Expected)
    assert(math.abs(Value - Expected) < 0.00001, tostring(Value) .. " ~= " .. tostring(Expected))
end

local function NewMap()
    local Map = MapModule.New({ CellSize = 1.0, FloorHeight = 4.0 })
    local Rows = {}
    for Index = 1, 15 do
        Rows[Index] = string.rep(".", 15)
    end
    Map:LoadFloor(1, table.concat(Rows, "\n"))
    Map:LoadFloor(2, table.concat(Rows, "\n"))
    GameManager.SetMap(Map)
    return Map
end

local function NewDoor(X, Y, Yaw)
    local Owner = Actor.New(X or -5.0, Y or 5.0, 2.0, Yaw, FVector.new(0.125, 1.0, 2.0))
    RegisteredDoors[#RegisteredDoors + 1] = Owner
    return Door.Register(Owner)
end

local function Reset()
    for _, Owner in ipairs(RegisteredDoors) do
        Door.Unregister(Owner)
    end
    RegisteredDoors = {}
    Sounds = {}
    SoundCalls = {}
    GameManager.Reset()
    GameManager.StartGame()
    return NewMap()
end

local function Test(Name, Callback)
    Reset()
    Callback()
    Passed = Passed + 1
    print("PASS " .. Name)
end

local function NewContext(Owner)
    local Map = GameManager.GetMap()
    local Navigation = EnemyNavigation.New(Map, Owner:GetNavigationExtent(), 0.0)
    return { mOwner = Owner, mData = { mChaseSpeed = 2.0, mPatrolSpeed = 2.0, mPatrolTurnSpeed = 360.0, mPatrolExtent = Owner:GetNavigationExtent(), mPatrolWallMargin = 0.0, mChaseReplanElapsed = 0.0, mChaseReplanInterval = 0.1, mChaseNavigation = Navigation, mPatrolNavigation = Navigation, mPatrolMap = Map, mPatrolPath = { { Location = Owner:GetActorLocation(), Floor = 1, Heading = 0 }, { Location = FVector.new(-2.0, 5.0, 0.0), Floor = 1, Heading = 0 } }, mPatrolPathIndex = 1, mPatrolHeightOffset = 0.0, mPatrolBlockedTime = 0.0, mPatrolReplanElapsed = 0.0, mStunDuration = 0.5, mResumeState = "Patrol" } }
end

local function WaitAtDoor(Context, DeltaTime)
    local From = Context.mOwner:GetActorLocation()
    local To = FVector.new(From.X + DeltaTime * 2.0, From.Y, From.Z)
    return Door.AdvanceEnemy(Context, From, To, 0.4, 0.4, 0.0, 2.0, DeltaTime)
end

local function FinishMotion(Entry)
    Door.Tick(Entry, 2.0)
    assert(not Entry.mMoving)
end

Test("player E starts smooth opening and closing without jumping", function()
    local Entry = NewDoor()
    local Player = Actor.New(-6.0, 5.0, 0.5, 0.0)
    GameManager.RegisterPlayer(Player)
    local Pressed = true
    local Environment = setmetatable({ self = Entry.mActor, IsKeyPressed = function(Value) return Pressed and Value == Key.E end }, { __index = _G })
    local Script = assert(loadfile("Assets/Scripts/GameLogic/DoorActor.lua"))
    setfenv(Script, Environment)()
    Environment.BeginPlay()
    Environment.Tick(0.0)
    assert(Entry.mOpen and Entry.mMoving)
    Near(Entry.mActor.mRotation.Yaw, 0.0)
    Pressed = false
    Environment.Tick(0.6)
    Near(Entry.mActor.mRotation.Yaw, -45.0)
    Environment.Tick(0.6)
    assert(not Entry.mMoving)
    Near(Entry.mActor.mLocation.Z, 2.0)
    Near(Entry.mActor.mLocation.X, -4.056)
    Near(Entry.mActor.mLocation.Y, 4.056)
    Near(Entry.mActor.mRotation.Yaw, -90.0)
    assert(Door.FindNearest(Player:GetActorLocation()) == Entry)
    Pressed = false
    Environment.Tick(5.0)
    assert(Entry.mOpen)
    Pressed = true
    Environment.Tick(0.0)
    assert(not Entry.mOpen and Entry.mMoving)
    Near(Entry.mActor.mRotation.Yaw, -90.0)
    Pressed = false
    Environment.Tick(0.55)
    Near(Entry.mActor.mRotation.Yaw, -45.0)
    Environment.Tick(0.55)
    assert(not Entry.mMoving)
    Near(Entry.mActor.mLocation.Z, 2.0)
    Near(Entry.mActor.mLocation.X, -5.0)
    Near(Entry.mActor.mLocation.Y, 5.0)
    Near(Entry.mActor.mRotation.Yaw, 0.0)
    Environment.EndPlay()
end)

Test("hinged door pushes away from either opener and keeps its hinge fixed", function()
    local Entry = NewDoor()
    Door.SetOpen(Entry, true, FVector.new(-4.0, 5.0, 0.5))
    FinishMotion(Entry)
    Near(Entry.mActor.mRotation.Yaw, 90.0)
    Near(Entry.mActor.mLocation.X, -5.944)
    Near(Entry.mActor.mLocation.Y, 4.056)
    Door.SetOpen(Entry, false)
    FinishMotion(Entry)
    local Rotated = NewDoor(-10.0, 10.0, 90.0)
    Door.SetOpen(Rotated, true, FVector.new(-10.0, 9.0, 0.5))
    FinishMotion(Rotated)
    Near(Rotated.mActor.mRotation.Yaw, 0.0)
    Near(Rotated.mActor.mLocation.X, -9.056)
    Near(Rotated.mActor.mLocation.Y, 10.944)
    Near(Rotated.mActor.mLocation.Z, 2.0)
end)

Test("rotation eases in and out while the hinge stays fixed at every frame", function()
    local Entry = NewDoor(-8.0, 7.0, 90.0)
    local SingleStep = NewDoor(-2.0, 10.0, 90.0)
    Door.SetOpen(Entry, true)
    Door.SetOpen(SingleStep, true)
    local PreviousAngle = 0.0
    local FirstStep = 0.0
    local MiddleStep = 0.0
    for Index = 1, 120 do
        Door.Tick(Entry, 0.01)
        local Angle = math.abs(Entry.mAngle)
        assert(Angle >= PreviousAngle and Angle <= 90.0)
        if Index == 1 then
            FirstStep = Angle
        elseif Index == 60 then
            MiddleStep = Angle - PreviousAngle
        end
        local Rotation = math.rad(Entry.mActor.mRotation.Yaw)
        Near(Entry.mActor.mLocation.X + math.sin(Rotation) * Entry.mHingeOffset, Entry.mClosedLocation.X + Entry.mHingeOffset)
        Near(Entry.mActor.mLocation.Y - math.cos(Rotation) * Entry.mHingeOffset, Entry.mClosedLocation.Y)
        PreviousAngle = Angle
    end
    Door.Tick(SingleStep, 1.2)
    assert(not Entry.mMoving and not SingleStep.mMoving and FirstStep < MiddleStep * 0.1)
    Near(Entry.mAngle, SingleStep.mAngle)
    Near(Entry.mActor.mLocation.X - Entry.mClosedLocation.X, SingleStep.mActor.mLocation.X - SingleStep.mClosedLocation.X)
    Near(Entry.mActor.mLocation.Y - Entry.mClosedLocation.Y, SingleStep.mActor.mLocation.Y - SingleStep.mClosedLocation.Y)
end)

Test("reversing midway starts from the current angle without switching hinge direction", function()
    local Entry = NewDoor()
    Door.SetOpen(Entry, true)
    Door.Tick(Entry, 0.4)
    local Angle = Entry.mAngle
    local Location = Entry.mActor:GetActorLocation()
    local OpeningHandle = Entry.mAudio.mMovementHandle
    Door.SetOpen(Entry, false)
    Near(Entry.mAngle, Angle)
    Near(Entry.mActor.mLocation.X, Location.X)
    Near(Entry.mActor.mLocation.Y, Location.Y)
    assert(not IsSoundPlaying(OpeningHandle))
    Door.Tick(Entry, 0.1)
    assert(Entry.mAngle > Angle and Entry.mAngle < 0.0)
    Angle = Entry.mAngle
    Door.SetOpen(Entry, true, FVector.new(-4.0, 5.0, 0.5))
    Near(Entry.mAngle, Angle)
    FinishMotion(Entry)
    Near(Entry.mAngle, -90.0)
    Door.SetOpen(Entry, false)
    FinishMotion(Entry)
    Near(Entry.mAngle, 0.0)
end)

Test("animation continues outside interaction range and with inventory open", function()
    local Entry = NewDoor()
    local Player = Actor.New(-6.0, 5.0, 0.5)
    GameManager.RegisterPlayer(Player)
    local Environment = setmetatable({ self = Entry.mActor, IsKeyPressed = function(Value) return false end }, { __index = _G })
    setfenv(assert(loadfile("Assets/Scripts/GameLogic/DoorActor.lua")), Environment)()
    Environment.BeginPlay()
    Door.SetOpen(Entry, true)
    Player:SetActorLocation(FVector.new(-13.0, 12.0, 0.5))
    local Inventory = require("Inventory")
    Inventory.Toggle()
    Environment.Tick(0.6)
    Near(Entry.mAngle, -45.0)
    Environment.Tick(0.6)
    assert(not Entry.mMoving)
    Inventory.Reset()
    Environment.EndPlay()
end)

Test("closing reverses safely if someone enters the doorway during rotation", function()
    local Entry = NewDoor()
    local Player = Actor.New(-6.0, 5.0, 0.5)
    GameManager.RegisterPlayer(Player)
    Door.SetOpen(Entry, true)
    FinishMotion(Entry)
    Door.SetOpen(Entry, false)
    Door.Tick(Entry, 0.3)
    local Angle = Entry.mAngle
    Player:SetActorLocation(FVector.new(-5.0, 5.0, 0.5))
    Door.Tick(Entry, 0.1)
    assert(Entry.mOpen and Entry.mMoving)
    Near(Entry.mAngle, Angle)
    FinishMotion(Entry)
    Near(Entry.mAngle, -90.0)
end)

Test("creaking stops at the end of both opening and closing", function()
    local Entry = NewDoor()
    Door.SetOpen(Entry, true)
    local OpenHandle = Entry.mAudio.mMovementHandle
    Door.Tick(Entry, 0.6)
    assert(IsSoundPlaying(OpenHandle))
    Door.Tick(Entry, 0.6)
    assert(not IsSoundPlaying(OpenHandle) and Entry.mAudio.mMovementHandle == 0)
    Door.SetOpen(Entry, false)
    local CloseHandle = Entry.mAudio.mMovementHandle
    Door.Tick(Entry, 0.55)
    assert(IsSoundPlaying(CloseHandle))
    Door.Tick(Entry, 0.55)
    assert(not IsSoundPlaying(CloseHandle) and Entry.mAudio.mMovementHandle == 0)
end)

Test("open and close sounds play once after successful changes", function()
    local Entry = NewDoor()
    GameManager.RegisterPlayer(Actor.New(-6.0, 5.0, 0.5))
    Door.SetOpen(Entry, true)
    assert(#SoundCalls == 1 and SoundCalls[1].mPath == DoorAudio.Config.OpenSound and not SoundCalls[1].mLoop)
    Door.Tick(Entry, 0.3)
    Door.SetOpen(Entry, true)
    assert(#SoundCalls == 1)
    Door.SetOpen(Entry, false)
    assert(#SoundCalls == 2 and SoundCalls[2].mPath == DoorAudio.Config.CloseSound and not SoundCalls[2].mLoop)
    assert(not SoundCalls[1].mPlaying and SoundCalls[2].mPlaying)
    Door.SetOpen(Entry, true)
    GameManager.GetPlayer():SetActorLocation(FVector.new(-5.0, 5.0, 0.5))
    assert(not Door.SetOpen(Entry, false) and #SoundCalls == 3)
end)

Test("knocking loops during the wait and stops when the monster opens the door", function()
    local Entry = NewDoor()
    GameManager.RegisterPlayer(Actor.New(-4.0, 5.0, 0.5))
    local Context = NewContext(Actor.New(-5.605, 5.0, 0.0))
    WaitAtDoor(Context, 0.5)
    local Handle = Entry.mAudio.mKnockHandle
    assert(Handle ~= 0 and Sounds[Handle].mLoop and Sounds[Handle].mPath == DoorAudio.Config.KnockSound)
    WaitAtDoor(Context, 0.99)
    assert(#SoundCalls == 1 and IsSoundPlaying(Handle) and not Entry.mOpen)
    WaitAtDoor(Context, 0.01)
    assert(Entry.mOpen and not IsSoundPlaying(Handle))
    assert(#SoundCalls == 2 and SoundCalls[2].mPath == DoorAudio.Config.OpenSound)
end)

Test("knocking stops immediately when the player opens or the monster cancels", function()
    local Entry = NewDoor()
    local Context = NewContext(Actor.New(-5.605, 5.0, 0.0))
    WaitAtDoor(Context, 0.5)
    local Handle = Entry.mAudio.mKnockHandle
    Door.SetOpen(Entry, true)
    assert(not IsSoundPlaying(Handle))
    Door.SetOpen(Entry, false)
    WaitAtDoor(Context, 0.5)
    Handle = Entry.mAudio.mKnockHandle
    Door.CancelEnemyWait(Context)
    assert(not IsSoundPlaying(Handle) and Entry.mAudio.mKnockHandle == 0)
end)

Test("monsters share one knocking loop and cancel independently", function()
    local Entry = NewDoor()
    local First = NewContext(Actor.New(-5.605, 5.0, 0.0))
    local Second = NewContext(Actor.New(-5.605, 5.0, 0.0))
    WaitAtDoor(First, 0.1)
    local Handle = Entry.mAudio.mKnockHandle
    WaitAtDoor(Second, 0.1)
    assert(#SoundCalls == 1)
    Door.CancelEnemyWait(First)
    assert(IsSoundPlaying(Handle))
    Door.CancelEnemyWait(Second)
    assert(not IsSoundPlaying(Handle))
end)

Test("door sounds attenuate with distance and stop on game over or removal", function()
    local Entry = NewDoor()
    local Player = Actor.New(-6.0, 5.0, 0.5)
    GameManager.RegisterPlayer(Player)
    Door.SetOpen(Entry, true)
    local Movement = Entry.mAudio.mMovementHandle
    Near(Sounds[Movement].mVolume, DoorAudio.Config.OpenVolume)
    Player:SetActorLocation(FVector.new(-40.0, 5.0, 0.5))
    DoorAudio.Tick(Entry)
    Near(Sounds[Movement].mVolume, 0.0)
    Player:SetActorLocation(FVector.new(-6.0, 5.0, 0.5))
    DoorAudio.Tick(Entry)
    Near(Sounds[Movement].mVolume, DoorAudio.Config.OpenVolume)
    Door.SetOpen(Entry, false)
    local Context = NewContext(Actor.New(-5.605, 5.0, 0.0))
    WaitAtDoor(Context, 0.1)
    local Knock = Entry.mAudio.mKnockHandle
    Movement = Entry.mAudio.mMovementHandle
    GameManager.GameOver("DoorAudioTest")
    DoorAudio.Tick(Entry)
    assert(not IsSoundPlaying(Knock) and not IsSoundPlaying(Movement) and Entry.mAudio == nil)
    GameManager.StartGame()
    Door.SetOpen(Entry, true)
    Movement = Entry.mAudio.mMovementHandle
    Door.Unregister(Entry.mActor)
    assert(not IsSoundPlaying(Movement))
end)

Test("finished audio is released and missing audio does not break doors", function()
    local Entry = NewDoor()
    Door.SetOpen(Entry, true)
    local Handle = Entry.mAudio.mMovementHandle
    Sounds[Handle].mPlaying = false
    DoorAudio.Tick(Entry)
    assert(Entry.mAudio.mMovementHandle == 0)
    Door.SetOpen(Entry, false)
    local SavedPlaySound = PlaySound
    local Attempts = 0
    PlaySound = function(Path, Loop, Volume)
        Attempts = Attempts + 1
        return 0
    end
    local Context = NewContext(Actor.New(-5.605, 5.0, 0.0))
    WaitAtDoor(Context, 0.2)
    WaitAtDoor(Context, 0.2)
    assert(Attempts == 1 and not Entry.mOpen)
    WaitAtDoor(Context, 1.1)
    assert(Entry.mOpen and Attempts == 2)
    PlaySound = SavedPlaySound
end)

Test("closing is blocked while a player or monster occupies the doorway", function()
    local Entry = NewDoor()
    assert(Door.SetOpen(Entry, true))
    local Player = Actor.New(-5.0, 5.0, 0.5)
    GameManager.RegisterPlayer(Player)
    assert(not Door.SetOpen(Entry, false))
    Player:SetActorLocation(FVector.new(-7.0, 5.0, 0.5))
    local Enemy = Actor.New(-5.0, 5.0, 0.0)
    GameManager.RegisterEnemy(Enemy)
    assert(not Door.SetOpen(Entry, false))
    Enemy:SetActorLocation(FVector.new(-3.0, 5.0, 0.0))
    assert(Door.SetOpen(Entry, false))
end)

Test("monster waits the full 1.5 seconds", function()
    local Entry = NewDoor()
    local Context = NewContext(Actor.New(-5.605, 5.0, 0.0))
    local Fraction, Waiting = WaitAtDoor(Context, 1.49)
    Near(Fraction, 0.0)
    assert(Waiting and not Entry.mOpen)
    WaitAtDoor(Context, 0.01)
    assert(Entry.mOpen)
    FinishMotion(Entry)
    local Scale, Blocked = WaitAtDoor(Context, 0.01)
    assert(Scale == 1.0 and not Blocked)
end)

Test("travel time does not count toward opening and large steps stop at the door", function()
    local Entry = NewDoor()
    local Context = NewContext(Actor.New(-10.0, 5.0, 0.0))
    local From = Context.mOwner:GetActorLocation()
    local Fraction, Waiting = Door.AdvanceEnemy(Context, From, FVector.new(0.0, 5.0, 0.0), 0.4, 0.4, 0.0, 2.0, 2.3)
    assert(Waiting and not Entry.mOpen)
    Near(From.X + 10.0 * Fraction, -5.605)
    Near(Context.mData.mDoorWait.mElapsedTime, 0.1025)
end)

Test("monster stops knocking when the player opens and waits until rotation finishes", function()
    local Entry = NewDoor()
    local Context = NewContext(Actor.New(-5.605, 5.0, 0.0))
    WaitAtDoor(Context, 0.5)
    assert(Door.SetOpen(Entry, true))
    local Fraction, Waiting = WaitAtDoor(Context, 0.01)
    assert(Waiting and Context.mData.mDoorWait == nil)
    Door.Tick(Entry, 0.6)
    Fraction, Waiting = WaitAtDoor(Context, 0.01)
    assert(Waiting)
    Door.Tick(Entry, 0.6)
    Fraction, Waiting = WaitAtDoor(Context, 0.01)
    assert(Fraction == 1.0 and not Waiting and Context.mData.mDoorWait == nil)
end)

Test("multiple monsters do not accelerate the opening timer", function()
    local Entry = NewDoor()
    local First = NewContext(Actor.New(-5.605, 5.0, 0.0))
    local Second = NewContext(Actor.New(-5.605, 5.0, 0.0))
    WaitAtDoor(First, 0.8)
    WaitAtDoor(Second, 0.8)
    assert(not Entry.mOpen)
    WaitAtDoor(First, 0.7)
    assert(Entry.mOpen)
    FinishMotion(Entry)
    local _, Waiting = WaitAtDoor(Second, 0.01)
    assert(not Waiting)
end)

Test("closing again starts a fresh wait and zero length substeps preserve it", function()
    local Entry = NewDoor()
    local Context = NewContext(Actor.New(-5.605, 5.0, 0.0))
    WaitAtDoor(Context, 1.0)
    Door.SetOpen(Entry, true)
    Door.SetOpen(Entry, false)
    WaitAtDoor(Context, 1.0)
    assert(not Entry.mOpen)
    local From = Context.mOwner:GetActorLocation()
    Door.AdvanceEnemy(Context, From, From, 0.4, 0.4, 0.0, 2.0, 0.00000000001)
    Near(Context.mData.mDoorWait.mElapsedTime, 1.0)
    WaitAtDoor(Context, 0.5)
    assert(Entry.mOpen)
end)

Test("rotated doors work from both sides and other floors are ignored", function()
    local Entry = NewDoor(-5.0, 5.0, 90.0)
    local Context = NewContext(Actor.New(-5.0, 4.395, 0.0))
    local Fraction, Waiting = Door.AdvanceEnemy(Context, FVector.new(-5.0, 4.395, 0.0), FVector.new(-5.0, 6.0, 0.0), 0.4, 0.4, 90.0, 2.0, 1.49)
    assert(Waiting and not Entry.mOpen)
    Near(Fraction, 0.0)
    Door.AdvanceEnemy(Context, FVector.new(-5.0, 4.395, 0.0), FVector.new(-5.0, 6.0, 0.0), 0.4, 0.4, 90.0, 2.0, 0.01)
    assert(Entry.mOpen)
    Door.SetOpen(Entry, false)
    local _, ReverseWaiting = Door.AdvanceEnemy(Context, FVector.new(-5.0, 5.605, 0.0), FVector.new(-5.0, 4.0, 0.0), 0.4, 0.4, -90.0, 2.0, 1.5)
    assert(ReverseWaiting and Entry.mOpen)
    Door.SetOpen(Entry, false)
    local Scale, UpstairsWaiting = Door.AdvanceEnemy(Context, FVector.new(-5.0, 4.395, 4.0), FVector.new(-5.0, 6.0, 4.0), 0.4, 0.4, 90.0, 2.0, 2.0)
    assert(Scale == 1.0 and not UpstairsWaiting and not Entry.mOpen)
    assert(Door.FindNearest(FVector.new(-5.0, 4.0, 4.5)) == nil)
end)

Test("moving away cancels the timer and only the nearest door blocks a step", function()
    local Entry = NewDoor()
    local FarDoor = NewDoor(-2.0, 5.0)
    local Context = NewContext(Actor.New(-5.605, 5.0, 0.0))
    WaitAtDoor(Context, 1.0)
    local Scale, Waiting = Door.AdvanceEnemy(Context, FVector.new(-5.605, 5.0, 0.0), FVector.new(-7.0, 5.0, 0.0), 0.4, 0.4, 180.0, 2.0, 1.0)
    assert(Scale == 1.0 and not Waiting and Context.mData.mDoorWait == nil)
    Door.AdvanceEnemy(Context, FVector.new(-5.605, 5.0, 0.0), FVector.new(0.0, 5.0, 0.0), 0.4, 0.4, 0.0, 2.0, 1.0)
    assert(not Entry.mOpen and not FarDoor.mOpen)
    WaitAtDoor(Context, 0.5)
    assert(Entry.mOpen and not FarDoor.mOpen)
end)

Test("patrol stops, opens and continues without the stuck timeout", function()
    local Entry = NewDoor()
    local Context = NewContext(Actor.New(-8.0, 5.0, 0.0))
    local States = require("EnemyStates")
    local Machine = FSM.New(Context.mOwner, States, Context.mData)
    Machine:Start("Patrol")
    for Index = 1, 100 do
        Machine:Tick(0.05)
        Door.Tick(Entry, 0.05)
        if not Entry.mOpen then
            assert(Context.mOwner.mLocation.X <= -5.605 + 0.00001)
            assert(Machine:GetCurrentState() == "Patrol")
        end
    end
    assert(Entry.mOpen and Context.mOwner.mLocation.X > -4.0)
    Machine:Stop()
end)

Test("chase replanning preserves the wait and stun resets it", function()
    local Entry = NewDoor()
    local Owner = Actor.New(-5.605, 5.0, 0.0)
    local Context = NewContext(Owner)
    GameManager.RegisterPlayer(Actor.New(-2.0, 5.0, 0.5))
    GameManager.SetLightOn(true)
    local Machine = FSM.New(Owner, require("EnemyStates"), Context.mData)
    Machine:Start("Chase")
    for Index = 1, 10 do
        Machine:Tick(0.05)
    end
    assert(Context.mData.mDoorWait ~= nil and not Entry.mOpen)
    local KnockHandle = Entry.mAudio.mKnockHandle
    Machine:HandleEvent("Stun")
    assert(Machine:GetCurrentState() == "Stunned" and Context.mData.mDoorWait == nil)
    assert(not IsSoundPlaying(KnockHandle))
    Machine:Tick(0.5)
    for Index = 1, 29 do
        Machine:Tick(0.05)
        assert(not Entry.mOpen)
    end
    Machine:Tick(0.05)
    assert(Entry.mOpen)
    for Index = 1, 50 do
        Machine:Tick(0.05)
        Door.Tick(Entry, 0.05)
    end
    assert(Owner.mLocation.X > -4.0)
    Machine:Stop()
end)

Test("watcher stops opening when observed and resumes with a fresh delay", function()
    local Entry = NewDoor()
    local Owner = Actor.New(-5.605, 5.0, 0.0)
    local Context = NewContext(Owner)
    Context.mData.mUnobservedDelay = 0.0
    local Player = Actor.New(-2.0, 5.0, 0.5)
    GameManager.RegisterPlayer(Player)
    local Machine = FSM.New(Owner, require("WatcherStates"), Context.mData)
    Machine:Start("Stalking")
    Machine:Tick(0.25)
    assert(Context.mData.mDoorWait ~= nil and not Entry.mOpen)
    local KnockHandle = Entry.mAudio.mKnockHandle
    Player.mObserved = true
    Machine:Tick(0.01)
    assert(Machine:GetCurrentState() == "Observed" and Context.mData.mDoorWait == nil)
    assert(not IsSoundPlaying(KnockHandle))
    Machine:Tick(2.0)
    assert(not Entry.mOpen)
    Player.mObserved = false
    Machine:Tick(0.01)
    for Index = 1, 5 do
        Machine:Tick(0.25)
        assert(not Entry.mOpen)
    end
    Machine:Tick(0.25)
    assert(Entry.mOpen)
    for Index = 1, 6 do
        Door.Tick(Entry, 0.25)
        Machine:Tick(0.25)
    end
    assert(Owner.mLocation.X > -5.605)
    Machine:Stop()
end)

Test("unregister restores the closed transform and clears interaction", function()
    local Entry = NewDoor()
    Door.SetOpen(Entry, true)
    assert(Door.Register(Entry.mActor) == Entry)
    Door.Unregister(Entry.mActor)
    Near(Entry.mActor.mLocation.Z, 2.0)
    assert(Door.FindNearest(FVector.new(-6.0, 5.0, 0.0)) == nil)
    local Replacement = Door.Register(Entry.mActor)
    assert(not Replacement.mOpen)
    Near(Replacement.mClosedLocation.Z, 2.0)
end)

Reset()
print("Door tests passed: " .. Passed)
