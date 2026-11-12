package.path = "Assets/Scripts/GameLogic/?.lua;" .. package.path

local Runtime = { mPassed = 0, mSources = {}, mNextHandle = 0, mEnemyDistance = 5.0, mPlaying = true, mBreathingPlayAttempts = 0, mFailBreathingSound = false }
local Manager = { mShakes = {}, mValid = true, mCamera = {} }
local Actor = {}
local Enemy = {}
local GameManager = {}
local Shake = {}
Shake.__index = Shake

ECameraShakePlaySpace = { CameraLocal = 0 }
EInitialWaveOscillatorOffsetType = { Zero = 0 }

function Actor:GetPlayerCameraManager()
    return Manager
end

function Actor:GetCamera()
    return Manager.mPlayerCamera
end

function Enemy:GetActorLocation()
    return { X = Runtime.mEnemyDistance, Y = 0.0, Z = 0.0 }
end

function GameManager.GetPlayerLocation()
    return { X = 0.0, Y = 0.0, Z = 0.0 }
end

function GameManager.GetEnemies()
    return { Enemy }
end

function GameManager.GetMap()
    return nil
end

function GameManager.IsPlaying()
    return Runtime.mPlaying
end

function GameManager.GetEnemyState()
    return "Chase"
end

package.loaded.GameManager = GameManager

function PlaySound(Path, Loop, Volume)
    if Path == "Assets/Sounds/RecoveryBreathing.wav" then
        Runtime.mBreathingPlayAttempts = Runtime.mBreathingPlayAttempts + 1
        if Runtime.mFailBreathingSound then
            return 0
        end
    end
    Runtime.mNextHandle = Runtime.mNextHandle + 1
    Runtime.mSources[Runtime.mNextHandle] = { mPath = Path, mLoop = Loop, mVolume = Volume, mPlaying = true }
    return Runtime.mNextHandle
end

function StopSound(Handle)
    Runtime.mSources[Handle].mPlaying = false
end

function SetSoundVolume(Handle, Volume)
    Runtime.mSources[Handle].mVolume = Volume
end

function SetSoundPitch(Handle, Pitch)
    Runtime.mSources[Handle].mPitch = Pitch
end

function IsSoundPlaying(Handle)
    return Runtime.mSources[Handle].mPlaying
end

function Shake:IsValid()
    return self.mValid
end

function Shake:IsFinished()
    return self.mFinished
end

function Shake:SetShakeScale(Scale)
    self.mScale = Scale
end

function Manager:IsValid()
    return self.mValid
end

function Manager:GetViewTargetCamera()
    return self.mCamera
end

function Manager:AddCameraModifier(ClassName, Parameters)
    assert(ClassName == "UCameraModifier_CameraShake" and Parameters.PlaySpace == ECameraShakePlaySpace.CameraLocal)
    local Instance = setmetatable({ mValid = true, mFinished = false, mPattern = Parameters.Pattern, mScale = Parameters.Scale, mElapsed = 0.0 }, Shake)
    table.insert(self.mShakes, Instance)
    return Instance
end

function Manager:RemoveCameraModifier(Instance)
    Instance.mValid = false
end

function Manager:Advance(DeltaTime)
    for _, Instance in ipairs(self.mShakes) do
        Instance.mElapsed = Instance.mElapsed + DeltaTime
        if Instance.mPattern.Duration > 0.0 and Instance.mElapsed >= Instance.mPattern.Duration then
            Instance.mFinished = true
            Instance.mValid = false
        end
    end
end

local EnemyAudio = require("EnemyAudio")
local HorrorCamera = require("HorrorCamera")

local function Check(Condition, Message)
    assert(Condition, Message)
    Runtime.mPassed = Runtime.mPassed + 1
end

local function Near(Left, Right)
    return math.abs(Left - Right) < 0.00001
end

local function Step(Distance, DeltaTime, IsRunning, IsActive)
    Manager:Advance(DeltaTime)
    EnemyAudio.Tick(DeltaTime)
    HorrorCamera.Update("Player", { X = Distance, Y = 0.0, Z = 0.0 }, DeltaTime, IsRunning, IsActive)
end

local function Fresh(Distance)
    HorrorCamera.Stop("Player")
    EnemyAudio.Stop()
    Runtime.mSources = {}
    Runtime.mEnemyDistance = Distance or 5.0
    Runtime.mPlaying = true
    Runtime.mBreathingPlayAttempts = 0
    Runtime.mFailBreathingSound = false
    Manager.mShakes = {}
    Manager.mValid = true
    Manager.mPlayerCamera = {}
    Manager.mCamera = Manager.mPlayerCamera
    EnemyAudio.Start()
    EnemyAudio.Tick(0.5)
    HorrorCamera.Start("Player", Actor)
    Step(0.0, 1.0 / 60.0, false)
end

local function GetBreathingShake()
    for _, Instance in ipairs(Manager.mShakes) do
        if Instance.mValid and Instance.mPattern.Duration == 0.0 then
            return Instance
        end
    end
    return nil
end

local function GetBreathingSound()
    for _, Source in pairs(Runtime.mSources) do
        if Source.mPlaying and Source.mPath == HorrorCamera.Config.BreathingSound then
            return Source
        end
    end
    return nil
end

local function Sprint(Distance, Duration)
    local Frames = math.floor(Duration * 60.0)
    for Index = 1, Frames do
        Step(Distance / Frames, 1.0 / 60.0, true)
    end
end

local function Recover(Duration)
    for Index = 1, math.floor(Duration * 60.0) do
        Step(0.0, 1.0 / 60.0, false)
    end
end

Fresh()
Check(#Manager.mShakes == 1, "first audible heartbeat creates a camera pulse")
Check(Near(Manager.mShakes[1].mScale, 1.0), "heartbeat amplitude follows the current audio volume")
Step(0.0, 0.1, false)
Check(#Manager.mShakes == 1, "paired heartbeat pulses stay separated")
Step(0.0, 0.03, false)
Check(#Manager.mShakes == 2 and Manager.mShakes[2].mScale < Manager.mShakes[1].mScale, "second heartbeat is a separate weaker pulse")
Step(0.0, 0.37, false)
Check(#Manager.mShakes == 3, "maximum heartbeat volume produces a new cycle every half second")

Fresh()
Step(0.0, 2.4, false)
Check(#Manager.mShakes == 1, "a long frame does not replay missed heartbeat pulses")

local function CountHeartbeats(Distance, DeltaTime)
    Fresh(Distance)
    for Index = 1, math.floor(3.0 / DeltaTime) do
        Step(0.0, DeltaTime, false)
    end
    return #Manager.mShakes
end

local QuietCount = CountHeartbeats(15.0, 1.0 / 60.0)
local MediumCount = CountHeartbeats(11.0, 1.0 / 60.0)
local LoudCount = CountHeartbeats(5.0, 1.0 / 60.0)
Check(QuietCount < MediumCount and MediumCount < LoudCount, "louder heartbeat volume increases camera pulse frequency")
Check(math.abs(CountHeartbeats(5.0, 0.1) - LoudCount) <= 1, "heartbeat rate stays stable across frame rates")

Fresh(11.0)
Check(Near(Manager.mShakes[1].mScale, 0.5), "half heartbeat volume produces half camera amplitude")
Runtime.mEnemyDistance = 40.0
Step(0.0, 0.5, false)
Check(#Manager.mShakes == 1 and not Manager.mShakes[1]:IsValid(), "silent heartbeat removes its camera pulses")
EnemyAudio.Stop()
Step(0.0, 1.0 / 60.0, false)
Check(#Manager.mShakes == 1, "stopped audio cannot create a heartbeat pulse")

Fresh(40.0)
Sprint(0.0, 2.0)
Recover(0.5)
Check(GetBreathingShake() == nil, "sprinting into a wall does not accumulate breathing")
Check(GetBreathingSound() == nil, "sprinting into a wall stays silent")
for Index = 1, 120 do
    Step(0.1, 1.0 / 60.0, false)
end
Check(GetBreathingShake() == nil, "walking does not accumulate sprint breathing")
Check(GetBreathingSound() == nil, "walking does not start recovery audio")

Sprint(10.0, 2.0)
Check(GetBreathingShake() == nil, "recovery breathing starts after running ends")
Check(GetBreathingSound() == nil, "recovery audio waits until running ends")
Recover(0.25)
local ShortScale = GetBreathingShake().mScale
Check(ShortScale > 0.0, "running creates recovery breathing")
local ShortSound = GetBreathingSound()
local ShortVolume = ShortSound.mVolume
Check(ShortSound.mLoop and Near(ShortVolume, ShortScale * HorrorCamera.Config.BreathingMaxVolume), "looped breathing volume follows the camera's recovery strength")
Check(Runtime.mBreathingPlayAttempts == 1, "recovery updates reuse one sound handle")
Recover(0.5)
Check(GetBreathingSound() == ShortSound and ShortSound.mVolume < ShortVolume, "recovering smoothly lowers the existing sound's volume")
Recover(3.0)
Check(GetBreathingShake() == nil, "short run breathing settles and releases its modifier")
Check(GetBreathingSound() == nil and not ShortSound.mPlaying, "completed recovery stops and releases its sound")

Fresh(40.0)
Sprint(20.0, 2.0)
Recover(0.25)
local LongScale = GetBreathingShake().mScale
Check(LongScale > ShortScale * 1.8, "longer distance produces stronger breathing at equal running time")
Check(GetBreathingSound().mVolume > ShortVolume * 1.8 and Near(GetBreathingSound().mVolume, LongScale * HorrorCamera.Config.BreathingMaxVolume), "longer runs produce proportionally louder breathing")
Recover(2.0)
Check(GetBreathingShake() ~= nil, "longer distance also produces longer recovery")
Check(GetBreathingSound() ~= nil, "longer runs keep recovery audio playing longer")
Recover(3.0)
Check(GetBreathingShake() == nil, "long run breathing eventually settles")
Check(GetBreathingSound() == nil, "long run audio eventually becomes silent")

Fresh(40.0)
Sprint(10.0, 4.0)
Recover(0.25)
Check(Near(GetBreathingShake().mScale, ShortScale), "equal distance has equal breathing despite different run duration")
Check(Near(GetBreathingSound().mVolume, ShortVolume), "equal running distance gives equal recovery volume")
Sprint(30.0, 6.0)
Recover(0.25)
Check(GetBreathingShake().mScale > LongScale, "resuming a run adds to the remaining recovery distance")
Check(GetBreathingSound().mVolume <= HorrorCamera.Config.BreathingMaxVolume, "accumulated running cannot exceed the configured volume cap")
Recover(7.0)
Check(GetBreathingShake() == nil, "maximum accumulated breathing has a bounded recovery")
Check(GetBreathingSound() == nil, "maximum accumulated audio stops after recovery")

Fresh()
Sprint(10.0, 2.0)
Step(0.0, 0.1, false)
local CameraSwitchSound = GetBreathingSound()
local ForeignSoundHandle = PlaySound("Unrelated.wav", true, 0.3)
local ForeignShake = Manager:AddCameraModifier("UCameraModifier_CameraShake", { Pattern = { Duration = 0.0 }, Scale = 1.0, PlaySpace = ECameraShakePlaySpace.CameraLocal })
Manager.mCamera = {}
Step(0.0, 0.1, false)
Check(GetBreathingShake() == ForeignShake, "camera changes clear only owned effects")
Check(not CameraSwitchSound.mPlaying and GetBreathingSound() == nil and IsSoundPlaying(ForeignSoundHandle), "camera switches stop recovery audio and preserve unrelated sounds")
Manager.mCamera = Manager.mPlayerCamera
Step(0.0, 0.1, false)
HorrorCamera.Stop("Player")
local OwnedValid = 0
for _, Instance in ipairs(Manager.mShakes) do
    if Instance.mValid and Instance ~= ForeignShake then
        OwnedValid = OwnedValid + 1
    end
end
Check(OwnedValid == 0 and ForeignShake:IsValid(), "stopping leaves unrelated camera modifiers intact")

Fresh(40.0)
Sprint(10.0, 2.0)
Recover(0.25)
local InactiveSound = GetBreathingSound()
Step(0.0, 0.1, false, false)
Check(GetBreathingShake() == nil, "inactive gameplay clears breathing immediately")
Check(not InactiveSound.mPlaying and GetBreathingSound() == nil, "pauses and cutscenes stop recovery audio immediately")
Recover(0.5)
Check(GetBreathingShake() == nil, "inactive gameplay resets accumulated running distance")
Check(GetBreathingSound() == nil, "returning from inactive gameplay does not replay stale breathing")
Sprint(10.0, 2.0)
Recover(0.25)
Check(GetBreathingShake() ~= nil, "camera effects resume when gameplay returns after an intro or pause")
Check(GetBreathingSound() ~= nil, "recovery audio resumes after a new run")

local InterruptedSound = GetBreathingSound()
InterruptedSound.mPlaying = false
Recover(0.1)
Check(GetBreathingSound() ~= nil and GetBreathingSound() ~= InterruptedSound, "externally stopped recovery audio restarts while breathing is active")
local ReloadSound = GetBreathingSound()
HorrorCamera.Start("Player", Actor)
Check(not ReloadSound.mPlaying and GetBreathingSound() == nil, "script reload stops the previous recovery loop")
Sprint(10.0, 2.0)
Recover(0.25)
local StoppedSound = GetBreathingSound()
HorrorCamera.Stop("Player")
HorrorCamera.Stop("Player")
Check(not StoppedSound.mPlaying and GetBreathingSound() == nil, "repeated player cleanup safely stops recovery audio")

Fresh(40.0)
Runtime.mFailBreathingSound = true
Sprint(10.0, 2.0)
Recover(0.5)
Check(GetBreathingSound() == nil and Runtime.mBreathingPlayAttempts == 1 and GetBreathingShake() ~= nil, "failed audio loads are not retried every frame and preserve camera breathing")
Runtime.mFailBreathingSound = false
HorrorCamera.Start("Player", Actor)
Sprint(10.0, 2.0)
Recover(0.25)
Check(GetBreathingSound() ~= nil, "reinitializing the player allows a failed sound to load again")

Fresh()
HorrorCamera.Start("Player", Actor)
Check(not Manager.mShakes[1]:IsValid(), "reload clears the previous state's heartbeat")
HorrorCamera.Stop("Player")
EnemyAudio.Stop()

print(string.format("HorrorCameraTests: %d checks passed", Runtime.mPassed))
