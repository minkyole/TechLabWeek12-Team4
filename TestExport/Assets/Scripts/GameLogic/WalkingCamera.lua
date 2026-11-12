local FirstPersonFootsteps = require("FirstPersonFootsteps")
local WalkingCamera = {}
local Runtime = { mStates = {} }
local WalkSpeed = 3.0
local WalkStepDistance = 1.3
local RunStepDistance = 1.7
local MaximumStepFrequency = 3.2
local VerticalAmplitude = 0.014
local LateralAmplitude = 0.003
local MaximumMotionStrength = 2.0
local PitchLimit = 85.0
local MinimumFieldOfView = 40.0
local MaximumFieldOfView = 85.0

local function Clamp(Value, Minimum, Maximum)
    return math.max(Minimum, math.min(Maximum, Value))
end

local function Blend(Current, Target, Speed, DeltaTime)
    return Target + (Current - Target) * math.exp(-Speed * DeltaTime)
end

local function CreateSpring()
    return { mPosition = 0.0, mVelocity = 0.0 }
end

local function UpdateSpring(Spring, Target, Frequency, DeltaTime)
    local Displacement = Spring.mPosition - Target
    local Decay = math.exp(-Frequency * DeltaTime)
    local Change = (Spring.mVelocity + Frequency * Displacement) * DeltaTime
    Spring.mPosition = Target + (Displacement + Change) * Decay
    Spring.mVelocity = (Spring.mVelocity - Frequency * Change) * Decay
    return Spring.mPosition
end

local function MakeQuaternion(Pitch, Yaw, Roll)
    local HalfPitch = math.rad(Pitch) * 0.5
    local HalfYaw = math.rad(Yaw) * 0.5
    local HalfRoll = math.rad(Roll) * 0.5
    local SP = math.sin(HalfPitch)
    local CP = math.cos(HalfPitch)
    local SY = math.sin(HalfYaw)
    local CY = math.cos(HalfYaw)
    local SR = math.sin(HalfRoll)
    local CR = math.cos(HalfRoll)
    return { X = SR * CP * CY - CR * SP * SY, Y = CR * SP * CY + SR * CP * SY, Z = CR * CP * SY - SR * SP * CY, W = CR * CP * CY + SR * SP * SY }
end

local function ComposeRotation(Base, Pitch, Yaw, Roll)
    local Delta = MakeQuaternion(Pitch, Yaw, Roll)
    local X = Delta.W * Base.X + Delta.X * Base.W + Delta.Y * Base.Z - Delta.Z * Base.Y
    local Y = Delta.W * Base.Y - Delta.X * Base.Z + Delta.Y * Base.W + Delta.Z * Base.X
    local Z = Delta.W * Base.Z + Delta.X * Base.Y - Delta.Y * Base.X + Delta.Z * Base.W
    local W = Delta.W * Base.W - Delta.X * Base.X - Delta.Y * Base.Y - Delta.Z * Base.Z
    local ResultPitch = math.asin(Clamp(2.0 * (W * Y - Z * X), -1.0, 1.0))
    local ResultYaw = math.atan2(2.0 * (W * Z + X * Y), 1.0 - 2.0 * (Y * Y + Z * Z))
    local ResultRoll = math.atan2(2.0 * (W * X + Y * Z), 1.0 - 2.0 * (X * X + Y * Y))
    return FRotator.new(math.deg(ResultPitch), math.deg(ResultYaw), math.deg(ResultRoll))
end

local function CaptureComponent(Camera, Name)
    local Component = Camera:FindChildComponent(Name)
    if Component == nil then
        return nil
    end
    local Rotation = Component:GetRelativeRotation()
    return { mComponent = Component, mLocation = Component:GetRelativeLocation(), mRotation = Rotation, mQuaternion = MakeQuaternion(Rotation.Pitch, Rotation.Yaw, Rotation.Roll) }
end

local function RestoreComponent(Pose)
    if Pose ~= nil then
        Pose.mComponent:SetRelativeLocation(Pose.mLocation)
        Pose.mComponent:SetRelativeRotation(Pose.mRotation)
    end
end

local function ApplyComponent(Pose, X, Y, Z, Pitch, Yaw, Roll, Strength)
    if Pose == nil then
        return
    end
    if Strength == 0.0 then
        RestoreComponent(Pose)
        return
    end

    Pose.mComponent:SetRelativeLocation(FVector.new(Pose.mLocation.X + X * Strength, Pose.mLocation.Y + Y * Strength, Pose.mLocation.Z + Z * Strength))
    Pose.mComponent:SetRelativeRotation(ComposeRotation(Pose.mQuaternion, Pitch * Strength, Yaw * Strength, Roll * Strength))
end

function WalkingCamera.Stop(StateKey)
    local State = Runtime.mStates[StateKey]
    if State ~= nil then
        RestoreComponent(State.mViewModel)
        RestoreComponent(State.mLight)
        State.mCamera:SetRelativeLocation(State.mBaseLocation)
        State.mCamera:SetRelativeRotation(State.mBaseRotation)
        State.mCamera:SetFieldOfView(State.mBaseFieldOfView)
        Runtime.mStates[StateKey] = nil
    end
    FirstPersonFootsteps.Stop(StateKey)
end

function WalkingCamera.Start(StateKey, Actor, InitialPitch, HeadBobStrength)
    local PreviousState = Runtime.mStates[StateKey]
    local LookPitch = PreviousState and PreviousState.mLookPitch or InitialPitch or 0.0
    local FieldOfView = PreviousState and PreviousState.mFieldOfView or nil
    local CameraStrength = HeadBobStrength
    if CameraStrength == nil then
        CameraStrength = PreviousState and PreviousState.mHeadBobStrength or 1.0
    end
    local ModelStrength = PreviousState and PreviousState.mViewModelStrength or 1.0
    local LightStrength = PreviousState and PreviousState.mLightMotionStrength or 0.2
    WalkingCamera.Stop(StateKey)

    local Camera = Actor:GetCamera()
    if Camera == nil then
        return 0.0, ModelStrength, LightStrength
    end

    local BaseRotation = Camera:GetRelativeRotation()
    local BaseFieldOfView = Camera:GetFieldOfView()
    local ViewModel = CaptureComponent(Camera, "FlashlightMesh")
    local Light = CaptureComponent(Camera, "USpotLightComponent_0")
    local LightFollowsViewModel = ViewModel ~= nil and Light ~= nil and ViewModel.mComponent:FindChildComponent("USpotLightComponent_0") == Light.mComponent
    LookPitch = Clamp(LookPitch, -PitchLimit - BaseRotation.Pitch, PitchLimit - BaseRotation.Pitch)
    local State = {
        mActor = Actor,
        mCamera = Camera,
        mBaseLocation = Camera:GetRelativeLocation(),
        mBaseRotation = BaseRotation,
        mBaseFieldOfView = BaseFieldOfView,
        mFieldOfView = FieldOfView or BaseFieldOfView,
        mHeadBobStrength = math.max(0.0, CameraStrength),
        mViewModelStrength = ModelStrength,
        mLightMotionStrength = LightStrength,
        mViewModel = ViewModel,
        mLight = Light,
        mLightFollowsViewModel = LightFollowsViewModel,
        mLookPitch = LookPitch,
        mPendingPitch = 0.0,
        mPendingYaw = 0.0,
        mLookPitchSpring = CreateSpring(),
        mLookYawSpring = CreateSpring(),
        mForwardSpring = CreateSpring(),
        mSideSpring = CreateSpring(),
        mLandingSpring = CreateSpring(),
        mStepProgress = 0.5,
        mStepCount = 0,
        mBlend = 0.0,
        mRunBlend = 0.0,
        mStepImpact = 0.0,
        mBreathingTime = 0.0,
        mPreviousHeight = Actor:GetActorLocation().Z,
        mVerticalSpeed = 0.0,
        mFallDistance = 0.0,
        mIsAirborne = false,
        mSpeed = 0.0,
    }
    Runtime.mStates[StateKey] = State
    FirstPersonFootsteps.Start(StateKey)
    Camera:SetRelativeRotation(FRotator.new(BaseRotation.Pitch + LookPitch, BaseRotation.Yaw, 0.0))
    Camera:SetFieldOfView(State.mFieldOfView)
    return State.mHeadBobStrength, ModelStrength, LightStrength
end

function WalkingCamera.Look(StateKey, PitchDelta, YawDelta)
    local State = Runtime.mStates[StateKey]
    if State == nil then
        return
    end

    State.mLookPitch = Clamp(State.mLookPitch + PitchDelta, -PitchLimit - State.mBaseRotation.Pitch, PitchLimit - State.mBaseRotation.Pitch)
    State.mPendingPitch = State.mPendingPitch + PitchDelta
    State.mPendingYaw = State.mPendingYaw + (YawDelta or 0.0)
    State.mCamera:SetRelativeRotation(FRotator.new(State.mBaseRotation.Pitch + State.mLookPitch, State.mBaseRotation.Yaw, 0.0))
end

function WalkingCamera.SetHeadBobStrength(StateKey, Strength)
    local State = Runtime.mStates[StateKey]
    if State == nil then
        return 0.0
    end

    State.mHeadBobStrength = math.max(0.0, Strength)
    if State.mHeadBobStrength == 0.0 then
        State.mCamera:SetRelativeLocation(State.mBaseLocation)
    end
    return State.mHeadBobStrength
end

function WalkingCamera.SetViewModelStrength(StateKey, Strength)
    local State = Runtime.mStates[StateKey]
    if State == nil then
        return 0.0
    end

    State.mViewModelStrength = Clamp(Strength, 0.0, MaximumMotionStrength)
    if State.mViewModelStrength == 0.0 then
        RestoreComponent(State.mViewModel)
    end
    return State.mViewModelStrength
end

function WalkingCamera.SetLightMotionStrength(StateKey, Strength)
    local State = Runtime.mStates[StateKey]
    if State == nil then
        return 0.0
    end

    State.mLightMotionStrength = Clamp(Strength, 0.0, MaximumMotionStrength)
    if State.mLightMotionStrength == 0.0 then
        RestoreComponent(State.mLight)
    end
    return State.mLightMotionStrength
end

function WalkingCamera.AdjustFieldOfView(StateKey, Delta)
    local State = Runtime.mStates[StateKey]
    if State == nil then
        return nil
    end

    State.mFieldOfView = Clamp(State.mFieldOfView + Delta, MinimumFieldOfView, MaximumFieldOfView)
    State.mCamera:SetFieldOfView(State.mFieldOfView)
    return State.mFieldOfView
end

function WalkingCamera.Land(StateKey, ImpactSpeed)
    local State = Runtime.mStates[StateKey]
    if State == nil or ImpactSpeed < 0.8 then
        return
    end

    State.mLandingSpring.mVelocity = math.max(-0.9, State.mLandingSpring.mVelocity - math.min(0.9, ImpactSpeed * 0.16))
    State.mFallDistance = 0.0
    State.mVerticalSpeed = 0.0
    State.mIsAirborne = false
    FirstPersonFootsteps.Step(StateKey, State.mStepCount, 1.0, State.mRunBlend, Clamp(ImpactSpeed / 5.0, 0.0, 1.0), State.mActor)
end

function WalkingCamera.OnHit(StateKey, Hit)
    local State = Runtime.mStates[StateKey]
    if State ~= nil and Hit.ImpactNormal.Z > 0.55 then
        WalkingCamera.Land(StateKey, -State.mVerticalSpeed)
    end
end

local function UpdateVerticalMovement(StateKey, State, DeltaTime)
    local Height = State.mActor:GetActorLocation().Z
    local Difference = Height - State.mPreviousHeight
    local VerticalSpeed = Difference / DeltaTime
    State.mPreviousHeight = Height
    if math.abs(Difference) > 1.0 then
        State.mFallDistance = 0.0
        State.mVerticalSpeed = 0.0
        State.mIsAirborne = false
        return
    end

    if State.mFallDistance > 0.08 and State.mVerticalSpeed < -0.8 and math.abs(VerticalSpeed) < 0.15 then
        WalkingCamera.Land(StateKey, -State.mVerticalSpeed)
    elseif VerticalSpeed < -0.15 then
        State.mFallDistance = State.mFallDistance - Difference
    elseif math.abs(VerticalSpeed) < 0.15 and State.mFallDistance > 0.0 then
        State.mFallDistance = 0.0
        State.mIsAirborne = false
    end
    State.mVerticalSpeed = VerticalSpeed
    State.mIsAirborne = State.mIsAirborne or math.abs(VerticalSpeed) > 0.3
end

function WalkingCamera.GetMotionState(StateKey)
    local State = Runtime.mStates[StateKey]
    if State == nil then
        return nil
    end
    return { StepCount = State.mStepCount, StepProgress = State.mStepProgress, Speed = State.mSpeed, RunBlend = State.mRunBlend, IsAirborne = State.mIsAirborne }
end

function WalkingCamera.Update(StateKey, LocalMovement, MoveSpeed, DeltaTime, IsRunning, IsActive)
    local State = Runtime.mStates[StateKey]
    if State == nil or DeltaTime <= 0.0 then
        return
    end

    local Active = IsActive ~= false
    if Active then
        UpdateVerticalMovement(StateKey, State, DeltaTime)
    else
        State.mPreviousHeight = State.mActor:GetActorLocation().Z
        State.mFallDistance = 0.0
        State.mVerticalSpeed = 0.0
        State.mIsAirborne = false
    end

    local ReferenceDistance = math.max(0.0, MoveSpeed) * DeltaTime
    local Distance = Active and not State.mIsAirborne and math.min(ReferenceDistance, math.sqrt(LocalMovement.X * LocalMovement.X + LocalMovement.Y * LocalMovement.Y)) or 0.0
    local TargetBlend = ReferenceDistance > 0.0 and Distance / ReferenceDistance or 0.0
    local Moving = Distance / DeltaTime > 0.05
    State.mSpeed = Distance / DeltaTime
    State.mBlend = Blend(State.mBlend, Moving and TargetBlend or 0.0, 14.0, DeltaTime)
    State.mRunBlend = Blend(State.mRunBlend, IsRunning and Moving and 1.0 or 0.0, 8.0, DeltaTime)
    State.mStepImpact = State.mStepImpact * math.exp(-24.0 * DeltaTime)
    FirstPersonFootsteps.Update(StateKey)

    if Moving then
        local StepDistance = WalkStepDistance + (RunStepDistance - WalkStepDistance) * State.mRunBlend
        local Advance = math.min(Distance / StepDistance, MaximumStepFrequency * math.min(DeltaTime, 0.1))
        State.mStepProgress = State.mStepProgress + Advance
        if State.mStepProgress >= 1.0 then
            State.mStepProgress = State.mStepProgress % 1.0
            State.mStepCount = State.mStepCount + 1
            State.mStepImpact = 1.0
            FirstPersonFootsteps.Step(StateKey, State.mStepCount, TargetBlend, State.mRunBlend, nil, State.mActor)
        end
    elseif State.mBlend < 0.0001 then
        State.mBlend = 0.0
        State.mStepProgress = 0.5
    end

    local ForwardTarget = Moving and Clamp(LocalMovement.X / (DeltaTime * WalkSpeed), -1.7, 1.7) or 0.0
    local SideTarget = Moving and Clamp(LocalMovement.Y / (DeltaTime * WalkSpeed), -1.7, 1.7) or 0.0
    local Forward = UpdateSpring(State.mForwardSpring, ForwardTarget, 14.0, DeltaTime)
    local Side = UpdateSpring(State.mSideSpring, SideTarget, 14.0, DeltaTime)
    local PitchLag = UpdateSpring(State.mLookPitchSpring, Active and Clamp(-State.mPendingPitch / DeltaTime * 0.012, -2.5, 2.5) or 0.0, 18.0, DeltaTime)
    local YawLag = UpdateSpring(State.mLookYawSpring, Active and Clamp(-State.mPendingYaw / DeltaTime * 0.014, -3.0, 3.0) or 0.0, 18.0, DeltaTime)
    local Landing = UpdateSpring(State.mLandingSpring, 0.0, 14.0, DeltaTime)
    State.mPendingPitch = 0.0
    State.mPendingYaw = 0.0
    State.mBreathingTime = (State.mBreathingTime + DeltaTime * (0.25 + 0.12 * State.mRunBlend)) % 1.0

    local Phase = (State.mStepCount % 2 + State.mStepProgress) * math.pi
    local VerticalWave = -math.cos(Phase * 2.0)
    local LateralWave = math.sin(Phase)
    local BobWeight = State.mBlend * (1.0 + State.mRunBlend * 0.25)
    local CameraZ = VerticalWave * VerticalAmplitude * BobWeight - State.mStepImpact * 0.002 * State.mBlend + Landing * 0.55
    local CameraY = LateralWave * LateralAmplitude * State.mBlend
    local BaseLocation = State.mBaseLocation
    State.mCamera:SetRelativeLocation(FVector.new(BaseLocation.X, BaseLocation.Y + CameraY * State.mHeadBobStrength, BaseLocation.Z + CameraZ * State.mHeadBobStrength))
    State.mCamera:SetRelativeRotation(FRotator.new(State.mBaseRotation.Pitch + State.mLookPitch, State.mBaseRotation.Yaw, 0.0))

    local Breath = math.sin(State.mBreathingTime * math.pi * 2.0) * 0.0015 * (1.0 - State.mBlend) * (Active and 1.0 or 0.0)
    local ModelX = -Forward * 0.012 - State.mRunBlend * 0.018
    local ModelY = LateralWave * 0.018 * BobWeight - Side * 0.014 + YawLag * 0.002
    local ModelZ = VerticalWave * 0.012 * BobWeight - State.mRunBlend * 0.035 + Breath + Landing * 1.4 + PitchLag * 0.003
    local ModelPitch = State.mRunBlend * 2.0 - VerticalWave * BobWeight * 0.8
    local ModelYaw = LateralWave * BobWeight
    local ModelRoll = -LateralWave * BobWeight * 1.2
    ApplyComponent(State.mViewModel, ModelX, ModelY, ModelZ, ModelPitch, ModelYaw, ModelRoll, State.mViewModelStrength)
    if not State.mLightFollowsViewModel then
        ApplyComponent(State.mLight, ModelX * 0.1, ModelY * 0.15, ModelZ * 0.15, ModelPitch * 0.35, ModelYaw * 0.5, 0.0, State.mLightMotionStrength)
    end
end

return WalkingCamera
