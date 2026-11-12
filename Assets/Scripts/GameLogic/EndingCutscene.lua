local Transition = require("Transition")
local EndingCutscene = {}
local Runtime = { mPendingResult = nil, mState = nil, mSubject = nil, mSubjects = {}, mFadeSequences = {}, mCameraSequences = {}, mView = nil, mGate = nil, mGateOpeningOffset = nil, mGameScene = "Assets/Scenes/Demo/06_Game.scene" }

EndingCutscene.Config = {
    Car = { Duration = 12.0, Title = "차량 탈출", Subtitle = "엔진 소리가 어둠 속의 발소리를 지워 버렸다.", Epilogue = "백미러 속 시설이 멀어졌다. 이제 돌아갈 이유는 없다." },
    Door = { Duration = 11.0, Title = "비상문 탈출", Subtitle = "잠금이 풀렸다. 문틈 너머로 새벽 공기가 흘러들었다.", Epilogue = "문이 닫히자, 뒤쫓던 소리도 마침내 끊어졌다." },
}

local function Smooth(Value)
    local Progress = math.max(0.0, math.min(1.0, Value))
    return Progress * Progress * (3.0 - 2.0 * Progress)
end

local function ToWorld(State, X, Y, Z)
    return FVector.new(State.mOrigin.X + X * State.mCosine - Y * State.mSine, State.mOrigin.Y + X * State.mSine + Y * State.mCosine, State.mOrigin.Z + Z)
end

local function UpdateView(State)
    State.mCameraSequence:EvaluateCameraSequence(State.mElapsed, State.mCamera, State.mOrigin, State.mYaw, State.mRoute == "Car" and State.mSide or 1.0, false, false)
end

local function UpdateCar(State)
    local Time = State.mElapsed
    local DrivingTime = math.max(0.0, Time - 2.2)
    local Distance = DrivingTime * DrivingTime * 0.72
    if State.mSubject ~= nil then
        State.mSubject:SetActorLocation(ToWorld(State, Distance, 0.0, math.sin(Time * 37.0) * 0.008))
    end
    if State.mGate ~= nil then
        local Location = State.mGateLocation
        local Offset = State.mGateOpeningOffset
        local Opening = Smooth((Time - 0.2) / 4.0)
        State.mGate:SetActorLocation(FVector.new(Location.X + Offset.X * Opening, Location.Y + Offset.Y * Opening, Location.Z + Offset.Z * Opening))
    end
    UpdateView(State)
end

local function UpdateDoor(State)
    local Time = State.mElapsed
    local Opening = Smooth((Time - 0.9) / 2.4)
    local Closing = Smooth((Time - 8.0) / 1.7)
    if State.mSubject ~= nil then
        local Rotation = State.mSubjectRotation
        State.mSubject:SetActorRotation(FRotator.new(Rotation.Pitch, Rotation.Yaw - 102.0 * Opening * (1.0 - Closing), Rotation.Roll))
    end
    UpdateView(State)
end

local function PlayCue(State, Name, Path, Time, Volume)
    if State.mElapsed >= Time and not State.mPlayedCues[Name] then
        State.mPlayedCues[Name] = true
        local Handle = PlaySound(Path, false, Volume)
        if Handle ~= 0 then
            State.mSounds[#State.mSounds + 1] = Handle
        end
    end
end

local function UpdateSound(State)
    if State.mRoute == "Door" then
        PlayCue(State, "Unlock", "Assets/Sounds/손전등키는소리.mp3", 0.4, 0.7)
        PlayCue(State, "Open", "Assets/Sounds/DoorOpen.wav", 0.9, 0.85)
        PlayCue(State, "Close", "Assets/Sounds/DoorClose.wav", 8.0, 0.85)
        for Index = 1, 8 do
            PlayCue(State, "Step" .. Index, Index % 2 == 1 and "Assets/Sounds/FootstepLeft.wav" or "Assets/Sounds/FootstepRight.wav", 3.9 + (Index - 1) * 0.48, 0.45)
        end
    else
        PlayCue(State, "DoorClose", "Assets/Sounds/DoorClose.wav", 0.15, 0.65)
        PlayCue(State, "Engine", "Assets/Sounds/EndingCarEngine.wav", 0.65, 0.7)
        PlayCue(State, "Gate", "Assets/Sounds/DoorOpen.wav", 0.2, 0.7)
    end
end

local function StopSounds(State)
    for _, Handle in ipairs(State.mSounds) do
        StopSound(Handle)
    end
    State.mSounds = {}
end

function EndingCutscene.SetGameScene(Scene)
    Runtime.mGameScene = Scene
end

function EndingCutscene.ClearPending()
    Runtime.mPendingResult = nil
end

function EndingCutscene.Prepare(Route, Result)
    if EndingCutscene.Config[Route] == nil then
        return false
    end
    Runtime.mPendingResult = { Route = Route, ElapsedTime = Result.ElapsedTime, BatteriesUsed = Result.BatteriesUsed, GameScene = Runtime.mGameScene }
    return true
end

function EndingCutscene.RegisterSubject(Actor, Route)
    if Route ~= nil then
        Runtime.mSubjects[Route] = Actor
    else
        Runtime.mSubject = Actor
    end
end

function EndingCutscene.UnregisterSubject(Actor)
    if Runtime.mState ~= nil and Runtime.mState.mSubject == Actor then
        Runtime.mState.mSubject = nil
    end
    if Runtime.mSubject == Actor then
        Runtime.mSubject = nil
    end
    for Route, Subject in pairs(Runtime.mSubjects) do
        if Subject == Actor then
            Runtime.mSubjects[Route] = nil
        end
    end
end

function EndingCutscene.RegisterFadeSequence(Actor, Route)
    if EndingCutscene.Config[Route] ~= nil then
        Runtime.mFadeSequences[Route] = Actor
    end
end

function EndingCutscene.UnregisterFadeSequence(Actor)
    if Runtime.mState ~= nil and Runtime.mState.mFadeSequence == Actor then
        Runtime.mState.mFadeSequence = nil
    end
    for Route, Sequence in pairs(Runtime.mFadeSequences) do
        if Sequence == Actor then
            Runtime.mFadeSequences[Route] = nil
        end
    end
end

function EndingCutscene.RegisterView(View)
    Runtime.mView = View
end

function EndingCutscene.RegisterCameraSequence(Actor, Route)
    Runtime.mCameraSequences[Route] = Actor
end

function EndingCutscene.UnregisterCameraSequence(Actor)
    if Runtime.mState ~= nil and Runtime.mState.mCameraSequence == Actor then
        EndingCutscene.Stop()
    end
    for Route, Sequence in pairs(Runtime.mCameraSequences) do
        if Sequence == Actor then
            Runtime.mCameraSequences[Route] = nil
        end
    end
end

function EndingCutscene.UnregisterView(View)
    if Runtime.mView == View then
        EndingCutscene.Stop()
        Runtime.mView = nil
    end
end

function EndingCutscene.RegisterGate(Actor, OpeningOffset)
    Runtime.mGate = Actor
    Runtime.mGateOpeningOffset = OpeningOffset or FVector.new(0.0, 8.5, 0.0)
end

function EndingCutscene.UnregisterGate(Actor)
    if Runtime.mState ~= nil and Runtime.mState.mGate == Actor then
        Runtime.mState.mGate = nil
    end
    if Runtime.mGate == Actor then
        Runtime.mGate = nil
        Runtime.mGateOpeningOffset = nil
    end
end

function EndingCutscene.StartPending()
    local Result = Runtime.mPendingResult
    if Result == nil or Runtime.mView == nil or Runtime.mSubjects[Result.Route] == nil then
        return false
    end
    return EndingCutscene.Start(Runtime.mView, Result.Route)
end

function EndingCutscene.Start(View, Scene)
    EndingCutscene.Stop()
    local Route = (Scene == "Ending_Car" or Scene == "Car") and "Car" or "Door"
    local CameraSequence = Runtime.mCameraSequences[Route]
    if CameraSequence == nil or View:GetCamera() == nil then
        return false
    end
    local InPlace = Scene == "Car" or Scene == "Door"
    local Result = Runtime.mPendingResult
    local Subject = InPlace and Runtime.mSubjects[Route] or Runtime.mSubject
    local Location = InPlace and Subject ~= nil and Subject:GetActorLocation() or FVector.new(0.0, 0.0, 0.0)
    local Rotation = InPlace and Subject ~= nil and Subject:GetActorRotation() or FRotator.new(0.0, 0.0, 0.0)
    local Yaw = InPlace and (Rotation.Yaw - (Route == "Car" and 180.0 or 0.0)) or 0.0
    local Cosine = math.cos(math.rad(Yaw))
    local Sine = math.sin(math.rad(Yaw))
    local Origin = InPlace and (Route == "Door" and FVector.new(Location.X - 0.92 * Sine, Location.Y + 0.92 * Cosine, Location.Z - 1.5) or Location) or FVector.new(0.0, 0.0, 0.0)
    local Side = -1.0
    if InPlace and Route == "Car" then
        local Manager = View:GetPlayerCameraManager()
        local SourceView = Manager ~= nil and Manager:GetViewTarget() or nil
        if SourceView ~= nil then
            local SourceLocation = SourceView:GetActorLocation()
            Side = (SourceLocation.Y - Origin.Y) * Cosine - (SourceLocation.X - Origin.X) * Sine >= 0.0 and 1.0 or -1.0
        end
    end
    local State = {
        mView = View,
        mCamera = View:GetCamera(),
        mCameraSequence = CameraSequence,
        mRoute = Route,
        mSubject = Subject,
        mSubjectLocation = Location,
        mSubjectRotation = Rotation,
        mGate = InPlace and Route == "Car" and Runtime.mGate or nil,
        mGateLocation = Runtime.mGate ~= nil and Runtime.mGate:GetActorLocation() or nil,
        mGateOpeningOffset = Runtime.mGateOpeningOffset,
        mOrigin = Origin,
        mSide = Side,
        mYaw = Yaw,
        mCosine = Cosine,
        mSine = Sine,
        mInPlace = InPlace,
        mBlendRemaining = InPlace and Transition.Config.Duration or 0.0,
        mManager = nil,
        mPreviousCamera = nil,
        mViewModel = nil,
        mViewModelLocation = nil,
        mElapsed = 0.0,
        mFinished = false,
        mFading = false,
        mFadeSequence = nil,
        mSounds = {},
        mPlayedCues = {},
        mResult = Result ~= nil and Result.Route == Route and Result or { Route = Route, ElapsedTime = 0.0, BatteriesUsed = 0, GameScene = InPlace and Runtime.mGameScene or "Assets/Scenes/Demo/06_Game.scene" },
    }
    Runtime.mState = State
    if Route == "Car" then
        UpdateCar(State)
    else
        UpdateDoor(State)
    end
    if InPlace then
        local Started, Manager, PreviousCamera = Transition.Start(View)
        if not Started then
            Runtime.mState = nil
            return false
        end
        State.mManager = Manager
        State.mPreviousCamera = PreviousCamera
        State.mViewModel = PreviousCamera ~= nil and PreviousCamera:FindChildComponent("FlashlightMesh") or nil
        if State.mViewModel ~= nil then
            State.mViewModelLocation = State.mViewModel:GetRelativeLocation()
            State.mViewModel:SetRelativeLocation(FVector.new(0.0, 0.0, -10.0))
        end
    end
    Runtime.mPendingResult = nil
    SetCursorMode(CursorMode.Free)
    return true
end

function EndingCutscene.Update(DeltaTime)
    local State = Runtime.mState
    if State == nil or State.mFinished then
        return
    end
    if State.mFading then
        State.mElapsed = State.mElapsed + math.max(0.0, DeltaTime)
        if State.mRoute == "Car" then
            UpdateCar(State)
        else
            UpdateDoor(State)
        end
        if State.mFadeSequence == nil or State.mFadeSequence:IsSequenceFinished() then
            EndingCutscene.Finish()
        end
        return
    end
    local RemainingTime = math.max(0.0, DeltaTime)
    if State.mBlendRemaining > 0.0 then
        local BlendTime = math.min(RemainingTime, State.mBlendRemaining)
        State.mBlendRemaining = State.mBlendRemaining - BlendTime
        RemainingTime = RemainingTime - BlendTime
        if State.mBlendRemaining > 0.0 then
            return
        end
    end
    State.mElapsed = math.min(EndingCutscene.Config[State.mRoute].Duration, State.mElapsed + RemainingTime)
    if State.mRoute == "Car" then
        UpdateCar(State)
    else
        UpdateDoor(State)
    end
    UpdateSound(State)
    if State.mElapsed >= EndingCutscene.Config[State.mRoute].Duration then
        EndingCutscene.BeginFadeOut()
    end
end

function EndingCutscene.BeginFadeOut()
    local State = Runtime.mState
    if State == nil or State.mFinished or State.mFading then
        return false
    end
    local Sequence = Runtime.mFadeSequences[State.mRoute]
    if Sequence == nil or not Sequence:StopSequence() or not Sequence:PlaySequence() then
        EndingCutscene.Finish()
        return false
    end
    State.mFadeSequence = Sequence
    State.mFading = true
    return true
end

function EndingCutscene.Finish()
    local State = Runtime.mState
    if State ~= nil and not State.mFinished then
        State.mFinished = true
        State.mFading = false
        State.mCameraSequence:StopSequence()
        StopSounds(State)
        SetCursorMode(CursorMode.Free)
    end
end

function EndingCutscene.Stop()
    local State = Runtime.mState
    if State == nil then
        return
    end
    Runtime.mState = nil
    State.mCameraSequence:StopSequence()
    StopSounds(State)
    if State.mFadeSequence ~= nil then
        State.mFadeSequence:StopSequence()
    end
    if State.mInPlace and State.mSubject ~= nil then
        State.mSubject:SetActorLocation(State.mSubjectLocation)
        State.mSubject:SetActorRotation(State.mSubjectRotation)
    end
    if State.mGate ~= nil then
        State.mGate:SetActorLocation(State.mGateLocation)
    end
    if State.mViewModel ~= nil then
        State.mViewModel:SetRelativeLocation(State.mViewModelLocation)
    end
    if State.mManager ~= nil and State.mManager:IsValid() then
        State.mManager:SetViewTargetCamera(State.mPreviousCamera)
    end
end

function EndingCutscene.GetState()
    return Runtime.mState
end

return EndingCutscene
