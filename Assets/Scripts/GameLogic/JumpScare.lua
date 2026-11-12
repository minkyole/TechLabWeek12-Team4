local JumpScare = {}
local Runtime = { mViewActor = nil, mState = nil }

JumpScare.Config = {
    LungeDuration = 0.32,
    HoldDuration = 0.65,
    BlackoutDuration = 0.22,
    StartDistance = 1.45,
    EndDistance = 0.5,
    FieldOfView = 48.0,
    Sound = "Assets/Sounds/울음소리.mp3",
    SoundVolume = 0.9,
    SoundPitch = 1.25,
    LightIntensity = 2.0,
    Profiles = {
        Monster = { FaceOffset = { X = 0.9, Y = 0.19, Z = 1.63 }, FacingYaw = 180.0 },
        Watcher = { FaceOffset = { X = -0.015, Y = -0.03, Z = 2.3 }, FacingYaw = 180.0, CameraHeight = 0.12 },
    },
}

local function StopSoundEffect(State)
    if State.mSoundHandle ~= 0 then
        StopSound(State.mSoundHandle)
        State.mSoundHandle = 0
    end
end

local function CaptureLight(Actor)
    local Light = Actor:GetSpotLight()
    if Light == nil then
        return nil
    end
    return { mComponent = Light, mVisible = Light:IsVisible(), mIntensity = Light:GetIntensity(), mInnerCone = Light:GetInnerConeAngle(), mOuterCone = Light:GetOuterConeAngle(), mRadius = Light:GetAttenuationRadius() }
end

local function RestoreLight(State)
    local Light = State.mLight
    if Light ~= nil then
        Light.mComponent:SetVisibility(Light.mVisible)
        Light.mComponent:SetIntensity(Light.mIntensity)
        Light.mComponent:SetInnerConeAngle(Light.mInnerCone)
        Light.mComponent:SetOuterConeAngle(Light.mOuterCone)
        Light.mComponent:SetAttenuationRadius(Light.mRadius)
    end
    if State.mViewModel ~= nil then
        State.mViewModel:SetRelativeLocation(State.mViewModelLocation)
    end
end

local function ApplyPose(State)
    local Config = JumpScare.Config
    local Progress = math.min(1.0, State.mElapsed / Config.LungeDuration)
    local Distance = Config.StartDistance + (Config.EndDistance - Config.StartDistance) * Progress * Progress
    local ImpactTime = math.max(0.0, State.mElapsed - Config.LungeDuration)
    local Impact = Progress >= 1.0 and math.exp(-ImpactTime * 5.0) or 0.0
    local Shake = Impact * math.sin(ImpactTime * 73.0)
    local Recoil = Impact * math.sin(math.min(1.0, ImpactTime / 0.18) * math.pi) * 0.07
    local Origin = State.mOrigin
    local Cosine = State.mCosine
    local Sine = State.mSine
    local Offset = State.mFaceOffset
    State.mKiller:SetActorLocation(FVector.new(Origin.X + Cosine * Distance - Offset.X, Origin.Y + Sine * Distance - Offset.Y, Origin.Z - Offset.Z))
    State.mKiller:SetActorRotation(FRotator.new(0.0, State.mEnemyYaw, 0.0))
    State.mViewActor:SetActorLocation(FVector.new(Origin.X - Cosine * Recoil, Origin.Y - Sine * Recoil, Origin.Z + State.mCameraHeight + Shake * 0.003))
    State.mViewActor:SetActorRotation(FRotator.new(math.deg(math.atan2(State.mCameraHeight, Distance + Recoil)) + Shake * 0.7, State.mYaw + Impact * math.sin(ImpactTime * 51.0) * 0.5, Shake * 1.2))
    State.mCamera:SetFieldOfView(State.mFieldOfView + (Config.FieldOfView - State.mFieldOfView) * Progress)
end

function JumpScare.Stop()
    local State = Runtime.mState
    if State == nil then
        return
    end
    Runtime.mState = nil
    StopSoundEffect(State)
    State.mKiller:SetActorLocation(State.mEnemyLocation)
    State.mKiller:SetActorRotation(State.mEnemyRotation)
    State.mCamera:SetRelativeLocation(State.mCameraLocation)
    State.mCamera:SetRelativeRotation(State.mCameraRotation)
    State.mCamera:SetFieldOfView(State.mFieldOfView)
    RestoreLight(State)
end

function JumpScare.RegisterView(Actor)
    if Runtime.mViewActor ~= Actor then
        JumpScare.Stop()
        Runtime.mViewActor = Actor
    end
end

function JumpScare.UnregisterView(Actor)
    if Runtime.mViewActor == Actor then
        JumpScare.Stop()
        Runtime.mViewActor = nil
    end
end

function JumpScare.ReleaseActor(Actor)
    local State = Runtime.mState
    if State ~= nil and (State.mPlayer == Actor or State.mKiller == Actor) then
        JumpScare.Stop()
    end
end

function JumpScare.Start(Player, Killer, ProfileName)
    local ViewActor = Runtime.mViewActor
    local Profile = JumpScare.Config.Profiles[ProfileName or "Monster"]
    if Player == nil or Killer == nil or ViewActor == nil or Profile == nil then
        return false
    end
    local Camera = Player:GetCamera()
    if Camera == nil or Camera ~= ViewActor:GetCamera() then
        return false
    end
    JumpScare.Stop()

    local Origin = ViewActor:GetActorLocation()
    local EnemyLocation = Killer:GetActorLocation()
    local DeltaX = EnemyLocation.X - Origin.X
    local DeltaY = EnemyLocation.Y - Origin.Y
    local Yaw = DeltaX * DeltaX + DeltaY * DeltaY > 0.0001 and math.deg(math.atan2(DeltaY, DeltaX)) or ViewActor:GetActorRotation().Yaw
    local EnemyYaw = Yaw + Profile.FacingYaw
    local EnemyCosine = math.cos(math.rad(EnemyYaw))
    local EnemySine = math.sin(math.rad(EnemyYaw))
    local Scale = Killer:GetActorScale3D()
    local Offset = Profile.FaceOffset
    local ViewModel = Camera:FindChildComponent("FlashlightMesh")
    local State = {
        mPlayer = Player,
        mKiller = Killer,
        mViewActor = ViewActor,
        mCamera = Camera,
        mCameraLocation = Camera:GetRelativeLocation(),
        mCameraRotation = Camera:GetRelativeRotation(),
        mCameraHeight = Profile.CameraHeight or 0.0,
        mFieldOfView = Camera:GetFieldOfView(),
        mOrigin = FVector.new(Origin.X, Origin.Y, Origin.Z),
        mEnemyLocation = EnemyLocation,
        mEnemyRotation = Killer:GetActorRotation(),
        mEnemyYaw = EnemyYaw,
        mFaceOffset = FVector.new(EnemyCosine * Offset.X * Scale.X - EnemySine * Offset.Y * Scale.Y, EnemySine * Offset.X * Scale.X + EnemyCosine * Offset.Y * Scale.Y, Offset.Z * Scale.Z),
        mYaw = Yaw,
        mCosine = math.cos(math.rad(Yaw)),
        mSine = math.sin(math.rad(Yaw)),
        mElapsed = 0.0,
        mBlackout = false,
        mLight = CaptureLight(ViewActor),
        mViewModel = ViewModel,
        mViewModelLocation = ViewModel ~= nil and ViewModel:GetRelativeLocation() or nil,
        mSoundHandle = 0,
    }
    Runtime.mState = State
    if ViewModel ~= nil then
        ViewModel:SetRelativeLocation(FVector.new(-0.6, 0.0, -0.45))
    end
    if State.mLight ~= nil then
        local Light = State.mLight.mComponent
        Light:SetVisibility(true)
        Light:SetIntensity(JumpScare.Config.LightIntensity)
        Light:SetOuterConeAngle(75.0)
        Light:SetInnerConeAngle(60.0)
        Light:SetAttenuationRadius(6.0)
    end
    ApplyPose(State)
    State.mSoundHandle = PlaySound(JumpScare.Config.Sound, false, JumpScare.Config.SoundVolume)
    if State.mSoundHandle ~= 0 then
        SetSoundPitch(State.mSoundHandle, JumpScare.Config.SoundPitch)
    end
    return true
end

function JumpScare.Update(DeltaTime)
    local State = Runtime.mState
    if State == nil then
        return true
    end
    State.mElapsed = State.mElapsed + math.max(0.0, DeltaTime)
    local VisibleDuration = JumpScare.Config.LungeDuration + JumpScare.Config.HoldDuration
    if State.mElapsed >= VisibleDuration then
        if not State.mBlackout then
            State.mBlackout = true
            StopSoundEffect(State)
        end
    else
        ApplyPose(State)
    end
    return State.mElapsed >= VisibleDuration + JumpScare.Config.BlackoutDuration
end

function JumpScare.IsBlackout()
    return Runtime.mState ~= nil and Runtime.mState.mBlackout
end

return JumpScare
