local Support = {}
local Data = assert(loadfile("Build/CameraSequenceFixtures.lua"))()

local function Sample(Self, Time)
    local Frame = math.max(0.0, math.min(Time * Self.mFPS, Self.mEndFrame))
    local Keys = Self.mKeys
    local Index = 1
    while Index < #Keys and Keys[Index + 1].Frame <= Frame do
        Index = Index + 1
    end
    local First = Keys[Index]
    local Last = Keys[Index + 1]
    if Last == nil or Last.Cut then
        return First.Location, First.Rotation, First.FOV
    end
    local Before = not First.Cut and Keys[Index - 1] or nil
    local After = Keys[Index + 2]
    if After ~= nil and After.Cut then
        After = nil
    end
    local Span = Last.Frame - First.Frame
    local Alpha = (Frame - First.Frame) / Span
    local Square = Alpha * Alpha
    local Cube = Square * Alpha
    local Location = {}
    local Rotation = {}
    local Hold = First.Location.X == Last.Location.X and First.Location.Y == Last.Location.Y and First.Location.Z == Last.Location.Z
    for _, Axis in ipairs({ "X", "Y", "Z" }) do
        local FirstTangent = (Last.Location[Axis] - (Before or First).Location[Axis]) / (Last.Frame - (Before or First).Frame)
        local LastTangent = ((After or Last).Location[Axis] - First.Location[Axis]) / ((After or Last).Frame - First.Frame)
        Location[Axis] = Hold and First.Location[Axis] or (2.0 * Cube - 3.0 * Square + 1.0) * First.Location[Axis] + (Cube - 2.0 * Square + Alpha) * Span * FirstTangent + (-2.0 * Cube + 3.0 * Square) * Last.Location[Axis] + (Cube - Square) * Span * LastTangent
    end
    for _, Axis in ipairs({ "Pitch", "Yaw", "Roll" }) do
        local Delta = (Last.Rotation[Axis] - First.Rotation[Axis] + 180.0) % 360.0 - 180.0
        Rotation[Axis] = First.Rotation[Axis] + Delta * Alpha
    end
    return Location, Rotation, First.FOV + (Last.FOV - First.FOV) * Alpha
end

local function Evaluate(Self, Time, Camera, Origin, Yaw, Side, Relative, PositionOnly)
    local Location, Rotation, FOV = Sample(Self, Time)
    local Angle = math.rad(Yaw)
    local Position = FVector.new(Origin.X + Location.X * math.cos(Angle) - Location.Y * Side * math.sin(Angle), Origin.Y + Location.X * math.sin(Angle) + Location.Y * Side * math.cos(Angle), Origin.Z + Location.Z)
    if Relative then
        Camera:SetRelativeLocation(Position)
    else
        Self.mView:SetActorLocation(Position)
    end
    if not PositionOnly then
        Self.mView:SetActorRotation(FRotator.new(Rotation.Pitch, Yaw + Rotation.Yaw * Side, Rotation.Roll))
        if Camera.SetFieldOfView ~= nil then
            Camera:SetFieldOfView(FOV)
        end
    end
    return true
end

local function Stop(Self)
    return true
end

function Support.New(Name, View)
    local Sequence = Data[Name]
    local Keys = {}
    for _, Section in ipairs(Sequence.Tracks[1].Sections) do
        for Index, Key in ipairs(Section.Keys) do
            Keys[#Keys + 1] = { Frame = Key.Frame, Location = Key.Location, Rotation = Key.Rotation, FOV = Key.FOV, Cut = Index == 1 and Section.CutAtStart }
        end
    end
    return { mKeys = Keys, mFPS = Sequence.FPS, mEndFrame = Sequence.DurationSeconds * Sequence.FPS, mView = View, EvaluateCameraSequence = Evaluate, StopSequence = Stop }
end

return Support
