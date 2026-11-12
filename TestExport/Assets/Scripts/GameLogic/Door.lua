local GameManager = require("GameManager")
local DoorAudio = require("DoorAudio")
local Door = {}

local PlayerInteractionRadius = 1.8
local EnemyOpenDelay = 1.5
local StopMargin = 0.08
local HingeInset = 0.056
local OpenDuration = 1.2
local CloseDuration = 1.1
local Doors = {}

local function CopyLocation(Location)
    return FVector.new(Location.X, Location.Y, Location.Z)
end

local function IsSameFloor(Entry, Location)
    local Map = GameManager.GetMap()
    local Bottom = Entry.mClosedLocation.Z - Entry.mHalfHeight
    if Map ~= nil then
        return Map:WorldToCell(Location).Floor == Map:WorldToCell(FVector.new(Entry.mClosedLocation.X, Entry.mClosedLocation.Y, Bottom + 0.01)).Floor
    end
    return Location.Z >= Bottom - 0.5 and Location.Z < Bottom + Entry.mHalfHeight * 2.0
end

local function ToLocal(Entry, Location)
    local DeltaX = Location.X - Entry.mClosedLocation.X
    local DeltaY = Location.Y - Entry.mClosedLocation.Y
    return DeltaX * Entry.mCosine + DeltaY * Entry.mSine, -DeltaX * Entry.mSine + DeltaY * Entry.mCosine
end

local function ProjectExtent(Entry, HalfLength, HalfWidth, Yaw)
    local Angle = math.rad(Yaw - Entry.mYaw)
    local Cosine = math.abs(math.cos(Angle))
    local Sine = math.abs(math.sin(Angle))
    return HalfLength * Cosine + HalfWidth * Sine, HalfLength * Sine + HalfWidth * Cosine
end

local function IsActorInDoorway(Entry, Actor)
    if Actor == nil then
        return false
    end
    local Location = Actor:GetActorLocation()
    if not IsSameFloor(Entry, Location) then
        return false
    end
    local Extent = Actor:GetNavigationExtent()
    local NormalExtent, TangentExtent = ProjectExtent(Entry, math.max(Extent.X, 0.25), math.max(Extent.Y, 0.25), Actor:GetActorRotation().Yaw)
    local Normal, Tangent = ToLocal(Entry, Location)
    return math.abs(Normal) < Entry.mHalfDepth + NormalExtent + 0.02 and math.abs(Tangent) < Entry.mHalfWidth + TangentExtent + 0.02
end

local function IsDoorwayOccupied(Entry)
    if IsActorInDoorway(Entry, GameManager.GetPlayer()) then
        return true
    end
    for _, Enemy in ipairs(GameManager.GetEnemies()) do
        if IsActorInDoorway(Entry, Enemy) then
            return true
        end
    end
    return false
end

local function ClipAxis(Start, Delta, Extent, Minimum, Maximum)
    if math.abs(Delta) < 0.000001 then
        if math.abs(Start) > Extent then
            return nil, nil
        end
        return Minimum, Maximum
    end
    local First = (-Extent - Start) / Delta
    local Last = (Extent - Start) / Delta
    Minimum = math.max(Minimum, math.min(First, Last))
    Maximum = math.min(Maximum, math.max(First, Last))
    if Minimum > Maximum then
        return nil, nil
    end
    return Minimum, Maximum
end

local function FindBlockingDoor(From, To, HalfLength, HalfWidth, Yaw)
    local Nearest = nil
    local Fraction = 1.0
    for _, Entry in ipairs(Doors) do
        if (not Entry.mOpen or Entry.mMoving) and IsSameFloor(Entry, From) then
            local Normal, Tangent = ToLocal(Entry, From)
            local EndNormal, EndTangent = ToLocal(Entry, To)
            local DeltaNormal = EndNormal - Normal
            if Normal * DeltaNormal < -0.000001 then
                local NormalExtent, TangentExtent = ProjectExtent(Entry, HalfLength, HalfWidth, Yaw)
                local Minimum, Maximum = ClipAxis(Normal, DeltaNormal, Entry.mHalfDepth + NormalExtent + StopMargin, 0.0, Fraction)
                if Minimum ~= nil then
                    Minimum, Maximum = ClipAxis(Tangent, EndTangent - Tangent, Entry.mHalfWidth + TangentExtent, Minimum, Maximum)
                    if Minimum ~= nil and (Nearest == nil or Minimum < Fraction) then
                        Nearest = Entry
                        Fraction = Minimum
                    end
                end
            end
        end
    end
    return Nearest, Fraction
end

local function ApplyAngle(Entry, Angle)
    local Location = CopyLocation(Entry.mClosedLocation)
    local Radians = math.rad(Angle)
    local NormalOffset = -Entry.mHingeOffset * math.sin(Radians)
    local TangentOffset = Entry.mHingeOffset * (math.cos(Radians) - 1.0)
    Location.X = Location.X + NormalOffset * Entry.mCosine - TangentOffset * Entry.mSine
    Location.Y = Location.Y + NormalOffset * Entry.mSine + TangentOffset * Entry.mCosine
    local Rotation = FRotator.new(Entry.mClosedRotation.Pitch, Entry.mYaw + Angle, Entry.mClosedRotation.Roll)
    if not Entry.mActor:SetActorLocation(Location) or not Entry.mActor:SetActorRotation(Rotation) then
        return false
    end
    Entry.mAngle = Angle
    return true
end

function Door.Register(Actor)
    for _, Entry in ipairs(Doors) do
        if Entry.mActor == Actor then
            return Entry
        end
    end
    local Extent = Actor:GetNavigationExtent()
    local Rotation = Actor:GetActorRotation()
    local Yaw = Rotation.Yaw
    local Entry = { mActor = Actor, mClosedLocation = CopyLocation(Actor:GetActorLocation()), mClosedRotation = FRotator.new(Rotation.Pitch, Yaw, Rotation.Roll), mYaw = Yaw, mCosine = math.cos(math.rad(Yaw)), mSine = math.sin(math.rad(Yaw)), mHalfDepth = Extent.X, mHalfWidth = Extent.Y, mHalfHeight = Extent.Z, mHingeOffset = math.max(0.0, Extent.Y - HingeInset), mOpen = false, mRevision = 0, mAngle = 0.0, mSwingAngle = -90.0, mStartAngle = 0.0, mTargetAngle = 0.0, mMotionElapsed = 0.0, mMotionDuration = 0.0, mMoving = false }
    assert(Entry.mHalfDepth > 0.0 and Entry.mHalfWidth > 0.0 and Entry.mHalfHeight > 0.0, "[Door] A door requires a visible mesh and a blocking box")
    Doors[#Doors + 1] = Entry
    return Entry
end

function Door.Unregister(Actor)
    for Index, Entry in ipairs(Doors) do
        if Entry.mActor == Actor then
            DoorAudio.Stop(Entry)
            Entry.mActor:SetActorLocation(Entry.mClosedLocation)
            Entry.mActor:SetActorRotation(Entry.mClosedRotation)
            table.remove(Doors, Index)
            return
        end
    end
end

function Door.FindNearest(Location)
    local Nearest = nil
    local NearestDistance = PlayerInteractionRadius * PlayerInteractionRadius
    for _, Entry in ipairs(Doors) do
        if IsSameFloor(Entry, Location) then
            local Normal, Tangent = ToLocal(Entry, Location)
            local Distance = Normal * Normal + math.max(0.0, math.abs(Tangent) - Entry.mHalfWidth) ^ 2
            if math.abs(Tangent) <= Entry.mHalfWidth + 0.15 and Distance <= NearestDistance then
                Nearest = Entry
                NearestDistance = Distance
            end
        end
    end
    return Nearest
end

function Door.SetOpen(Entry, Open, OpenerLocation)
    if Entry.mOpen == Open then
        return true
    end
    if not Open and IsDoorwayOccupied(Entry) then
        return false
    end
    if Open and math.abs(Entry.mAngle) < 0.000001 then
        local Normal = OpenerLocation and ToLocal(Entry, OpenerLocation) or -1.0
        Entry.mSwingAngle = Normal <= 0.0 and -90.0 or 90.0
    end
    Entry.mOpen = Open
    Entry.mRevision = Entry.mRevision + 1
    Entry.mStartAngle = Entry.mAngle
    Entry.mTargetAngle = Open and Entry.mSwingAngle or 0.0
    Entry.mMotionElapsed = 0.0
    Entry.mMotionDuration = (Open and OpenDuration or CloseDuration) * math.abs(Entry.mTargetAngle - Entry.mAngle) / 90.0
    Entry.mMoving = Entry.mMotionDuration > 0.000001
    if Entry.mMoving then
        DoorAudio.PlayMovement(Entry, Open)
    else
        DoorAudio.StopKnocking(Entry)
        DoorAudio.StopMovement(Entry)
    end
    return true
end

function Door.Tick(Entry, DeltaTime)
    if Entry == nil or not Entry.mMoving or not GameManager.IsPlaying() or DeltaTime <= 0.0 then
        return
    end
    if not Entry.mOpen and IsDoorwayOccupied(Entry) then
        Door.SetOpen(Entry, true)
        return
    end
    Entry.mMotionElapsed = math.min(Entry.mMotionDuration, Entry.mMotionElapsed + DeltaTime)
    if Entry.mMotionElapsed + 0.000001 >= Entry.mMotionDuration then
        Entry.mMotionElapsed = Entry.mMotionDuration
    end
    local Alpha = Entry.mMotionElapsed / Entry.mMotionDuration
    local Blend = Alpha * Alpha * (3.0 - 2.0 * Alpha)
    local Angle = Entry.mStartAngle + (Entry.mTargetAngle - Entry.mStartAngle) * Blend
    if not ApplyAngle(Entry, Angle) or Entry.mMotionElapsed >= Entry.mMotionDuration then
        Entry.mMoving = false
        DoorAudio.StopMovement(Entry)
    end
end

function Door.CancelEnemyWait(Context)
    local Wait = Context.mData.mDoorWait
    if Wait ~= nil then
        DoorAudio.EndWait(Wait.mDoor, Context.mOwner)
    end
    Context.mData.mDoorWait = nil
end

function Door.AdvanceEnemy(Context, From, To, HalfLength, HalfWidth, Yaw, Speed, DeltaTime)
    local Distance = math.sqrt((To.X - From.X) ^ 2 + (To.Y - From.Y) ^ 2 + (To.Z - From.Z) ^ 2)
    if Distance < 0.000001 then
        local Waiting = Context.mData.mDoorWait ~= nil
        return Waiting and 0.0 or 1.0, Waiting
    end
    local Entry, Fraction = FindBlockingDoor(From, To, HalfLength, HalfWidth, Yaw)
    if Entry == nil then
        Door.CancelEnemyWait(Context)
        return 1.0, false
    end
    if Entry.mMoving then
        Door.CancelEnemyWait(Context)
        return Fraction, true
    end
    local Data = Context.mData
    local Wait = Data.mDoorWait
    if Wait == nil or Wait.mDoor ~= Entry or Wait.mRevision ~= Entry.mRevision then
        Door.CancelEnemyWait(Context)
        Wait = { mDoor = Entry, mElapsedTime = 0.0, mRevision = Entry.mRevision }
        Data.mDoorWait = Wait
    end
    local TravelTime = Speed > 0.0 and Distance * Fraction / Speed or DeltaTime
    Wait.mElapsedTime = Wait.mElapsedTime + math.max(0.0, DeltaTime - TravelTime)
    if Wait.mElapsedTime + 0.000001 >= EnemyOpenDelay then
        Door.SetOpen(Entry, true, From)
        Door.CancelEnemyWait(Context)
    else
        DoorAudio.BeginWait(Entry, Context.mOwner)
    end
    return Fraction, true
end

return Door
