local StartLocation = FVector.new(0, 0, 0)
local ElapsedTime = 0
local BounceHeight = 1.2
local BounceSpeed = 2.0
local HasReportedMovement = false

function BeginPlay()
    StartLocation = self:GetActorLocation()
    ElapsedTime = 0
    HasReportedMovement = false
    Log("[SceneTest] BeginPlay", self:GetName())
end

function Tick(DeltaTime)
    ElapsedTime = ElapsedTime + DeltaTime
    local HeightOffset = BounceHeight * (1 - math.cos(ElapsedTime * BounceSpeed)) * 0.5
    self:SetActorLocation(FVector.new(StartLocation.X, StartLocation.Y, StartLocation.Z + HeightOffset))

    if not HasReportedMovement and ElapsedTime >= 0.5 then
        HasReportedMovement = true
        Log("[SceneTest] Lua bounce active", self:GetActorLocation().Z)
    end
end

function EndPlay()
    self:SetActorLocation(StartLocation)
    Log("[SceneTest] EndPlay", self:GetName())
end
