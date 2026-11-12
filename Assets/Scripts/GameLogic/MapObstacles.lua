local MapObstacles = {}

local function OverlapsCell(Map, Position, Extent, Cosine, Sine, X, Y)
    local CellX = Map.Origin.X - (Y - 1) * Map.CellSize
    local CellY = Map.Origin.Y + (X - 1) * Map.CellSize
    local DeltaX = CellX - Position.X
    local DeltaY = CellY - Position.Y
    local HalfCell = Map.CellSize * 0.5
    local AbsoluteCosine = math.abs(Cosine)
    local AbsoluteSine = math.abs(Sine)
    local Margin = 0.0001 * Map.CellSize
    if math.abs(DeltaX) >= Extent.X * AbsoluteCosine + Extent.Y * AbsoluteSine + HalfCell - Margin then
        return false
    end
    if math.abs(DeltaY) >= Extent.X * AbsoluteSine + Extent.Y * AbsoluteCosine + HalfCell - Margin then
        return false
    end
    local ProjectedCell = HalfCell * (AbsoluteCosine + AbsoluteSine)
    return math.abs(DeltaX * Cosine + DeltaY * Sine) < Extent.X + ProjectedCell - Margin and math.abs(-DeltaX * Sine + DeltaY * Cosine) < Extent.Y + ProjectedCell - Margin
end

local function CopyFloor(Data)
    local Result = { Width = Data.Width, Height = Data.Height, Walkable = {} }
    for Y, Row in pairs(Data.Walkable) do
        local Copy = {}
        for X, Value in pairs(Row) do
            Copy[X] = Value
        end
        Result.Walkable[Y] = Copy
    end
    return Result
end

function MapObstacles.Apply(Map, Layout)
    local Origin = Layout.Origin or Map.Origin
    local UpdatedFloors = {}
    local BlockedCells = 0
    for _, Prop in ipairs(Layout.Props or {}) do
        local Definition = assert(Layout.Types[Prop.Type], "Unknown facility prop type: " .. tostring(Prop.Type))
        local Floor = Prop.Floor or 1
        local Data = Map.Floors[Floor]
        if Data ~= nil then
            if not UpdatedFloors[Floor] then
                Data = CopyFloor(Data)
                Map.Floors[Floor] = Data
                UpdatedFloors[Floor] = true
            end
            local Angle = math.rad(Prop.Yaw or 0.0)
            local Cosine = math.cos(Angle)
            local Sine = math.sin(Angle)
            local Offset = Definition.Center
            local Position = { X = Origin.X + Prop.Location.X + Offset.X * Cosine - Offset.Y * Sine, Y = Origin.Y + Prop.Location.Y + Offset.X * Sine + Offset.Y * Cosine }
            local Extent = Definition.Extent
            local WorldExtentX = Extent.X * math.abs(Cosine) + Extent.Y * math.abs(Sine)
            local WorldExtentY = Extent.X * math.abs(Sine) + Extent.Y * math.abs(Cosine)
            local MinimumX = math.max(1, math.floor((Position.Y - Map.Origin.Y - WorldExtentY) / Map.CellSize + 0.5) + 1)
            local MaximumX = math.min(Data.Width, math.floor((Position.Y - Map.Origin.Y + WorldExtentY) / Map.CellSize + 0.5) + 1)
            local MinimumY = math.max(1, math.floor((Map.Origin.X - Position.X - WorldExtentX) / Map.CellSize + 0.5) + 1)
            local MaximumY = math.min(Data.Height, math.floor((Map.Origin.X - Position.X + WorldExtentX) / Map.CellSize + 0.5) + 1)
            for Y = MinimumY, MaximumY do
                local Row = Data.Walkable[Y]
                if Row ~= nil then
                    for X = MinimumX, MaximumX do
                        if Row[X] and OverlapsCell(Map, Position, Extent, Cosine, Sine, X, Y) then
                            Row[X] = nil
                            BlockedCells = BlockedCells + 1
                        end
                    end
                end
            end
        end
    end
    return BlockedCells
end

return MapObstacles
