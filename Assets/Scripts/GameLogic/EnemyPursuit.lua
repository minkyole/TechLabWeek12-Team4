local EnemyNavigation = require("EnemyNavigation")
local Door = require("Door")
local EnemyPursuit = {}

local WaypointTolerance = 0.001
local AngleTolerance = 0.01

function EnemyPursuit.HasTargetMoved(Data, Map, TargetLocation)
    local Previous = Data.mChaseTargetLocation
    if Previous == nil or TargetLocation == nil then
        return Previous ~= TargetLocation
    end
    local Spacing = Map and Map.CellSize * 0.5 or 0.5
    local Origin = Map and Map.Origin or { X = 0.0, Y = 0.0, Z = 0.0 }
    local FloorHeight = Map and Map.FloorHeight or 4.0
    return math.floor((Previous.X - Origin.X) / Spacing + 0.5) ~= math.floor((TargetLocation.X - Origin.X) / Spacing + 0.5) or math.floor((Previous.Y - Origin.Y) / Spacing + 0.5) ~= math.floor((TargetLocation.Y - Origin.Y) / Spacing + 0.5) or math.floor((Previous.Z - Origin.Z) / FloorHeight) ~= math.floor((TargetLocation.Z - Origin.Z) / FloorHeight)
end

function EnemyPursuit.BuildPath(Context, Map, TargetLocation)
    local Data = Context.mData
    Data.mChaseMap = Map
    Data.mChasePath = nil
    Data.mChasePathIndex = 1
    Data.mChaseReplanElapsed = 0.0
    Data.mChaseTurning = false
    Data.mChaseTargetLocation = TargetLocation and FVector.new(TargetLocation.X, TargetLocation.Y, TargetLocation.Z) or nil
    if Map == nil or TargetLocation == nil then
        return
    end

    local Navigation = Data.mChaseNavigation or Data.mPatrolNavigation
    if Navigation == nil or not Navigation:IsCurrent(Map) then
        Navigation = EnemyNavigation.New(Map, Data.mPatrolExtent or FVector.new(0.0, 0.0, 0.0), Data.mPatrolWallMargin or 0.0)
    end
    Data.mChaseNavigation = Navigation
    local Location = Context.mOwner:GetActorLocation()
    Data.mChasePath = Navigation:FindChasePath(Location, Context.mOwner:GetActorRotation().Yaw, TargetLocation)
    Data.mChaseHeightOffset = Location.Z - Map:CellToWorld(Map:WorldToCell(Location)).Z
end

local function MoveOwner(Context, Delta, Rotation)
    if not Context.mOwner:Move(Delta, Rotation) then
        error("[EnemyPursuit] Movement requires an actor with a root component")
    end
    return not Context.mData.mChaseInterrupted and Context.mMachine:IsRunning()
end

function EnemyPursuit.Advance(Context, DeltaTime)
    local Data = Context.mData
    local Owner = Context.mOwner
    local Navigation = Data.mChaseNavigation
    local RemainingTime = DeltaTime
    local Speed = Data.mChaseSpeed or 4.0
    local TurnSpeed = Data.mPatrolTurnSpeed or 360.0
    while Data.mChasePathIndex <= #Data.mChasePath do
        local Point = Data.mChasePath[Data.mChasePathIndex]
        local Target = Point.Location
        local Location = Owner:GetActorLocation()
        local Rotation = Owner:GetActorRotation()
        local TargetYaw = Point.Heading * 90.0
        local Angle = (TargetYaw - Rotation.Yaw + 180.0) % 360.0 - 180.0
        if math.abs(Angle) > AngleTolerance then
            Door.CancelEnemyWait(Context)
            if RemainingTime <= 0.0 or TurnSpeed <= 0.0 then
                return false
            end
            if not Navigation:CanTurn(Location, Point.Floor) then
                Data.mChasePath = nil
                Data.mChaseTurning = false
                return false
            end

            local Turn = math.min(math.abs(Angle), TurnSpeed * RemainingTime)
            Data.mChaseTurning = true
            local NewYaw = Rotation.Yaw + (Angle < 0.0 and -Turn or Turn)
            if not MoveOwner(Context, FVector.new(0.0, 0.0, 0.0), FRotator.new(Rotation.Pitch, NewYaw, Rotation.Roll)) then
                return false
            end
            RemainingTime = RemainingTime - Turn / TurnSpeed
        else
            Data.mChaseTurning = false
            local DeltaX = Target.X - Location.X
            local DeltaY = Target.Y - Location.Y
            local DeltaZ = Target.Z + Data.mChaseHeightOffset - Location.Z
            local Distance = math.sqrt(DeltaX * DeltaX + DeltaY * DeltaY + DeltaZ * DeltaZ)
            if Distance <= WaypointTolerance then
                Data.mChasePathIndex = Data.mChasePathIndex + 1
            elseif RemainingTime <= 0.0 or Speed <= 0.0 then
                return false
            else
                local Step = math.min(Distance, Speed * RemainingTime)
                local Scale = Step / Distance
                local Destination = FVector.new(Location.X + DeltaX * Scale, Location.Y + DeltaY * Scale, Location.Z + DeltaZ * Scale)
                if not Navigation:CanMove(Location, Destination, Point.Floor, Point.Heading) then
                    Door.CancelEnemyWait(Context)
                    Data.mChasePath = nil
                    return false
                end
                local Fraction, Waiting = Door.AdvanceEnemy(Context, Location, Destination, Navigation.mHalfLength, Navigation.mHalfWidth, TargetYaw, Speed, RemainingTime)
                if Waiting then
                    if Fraction > 0.0 then
                        MoveOwner(Context, FVector.new(DeltaX * Scale * Fraction, DeltaY * Scale * Fraction, DeltaZ * Scale * Fraction), FRotator.new(Rotation.Pitch, TargetYaw, Rotation.Roll))
                    end
                    return false
                end
                if not MoveOwner(Context, FVector.new(DeltaX * Scale, DeltaY * Scale, DeltaZ * Scale), FRotator.new(Rotation.Pitch, TargetYaw, Rotation.Roll)) then
                    return false
                end

                local NewLocation = Owner:GetActorLocation()
                local NewDistance = math.sqrt((Target.X - NewLocation.X) ^ 2 + (Target.Y - NewLocation.Y) ^ 2 + (Target.Z + Data.mChaseHeightOffset - NewLocation.Z) ^ 2)
                if Distance - NewDistance < Step - WaypointTolerance then
                    Door.CancelEnemyWait(Context)
                    Data.mChasePath = nil
                    return false
                end
                RemainingTime = RemainingTime - Step / Speed
            end
        end
    end

    Door.CancelEnemyWait(Context)
    return true
end

return EnemyPursuit
