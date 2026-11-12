local Map = {}
Map.__index = Map

local KEY_STRIDE = 1024
local SQRT2 = math.sqrt(2.0)
local DEFAULT_MAX_NODES = 20000

local ORTHOGONAL_DIRECTIONS = {
    { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 },
}

local DIAGONAL_DIRECTIONS = {
    { 1, 1 }, { 1, -1 }, { -1, 1 }, { -1, -1 },
}

local function MakeVector(x, y, z)
    if FVector then
        return FVector.new(x, y, z)
    end
    return { X = x, Y = y, Z = z }
end

local function ToKey(floor, x, y)
    return (floor * KEY_STRIDE + y) * KEY_STRIDE + x
end

local function FromKey(key)
    local x = key % KEY_STRIDE
    local rest = (key - x) / KEY_STRIDE
    local y = rest % KEY_STRIDE
    local floor = (rest - y) / KEY_STRIDE
    return floor, x, y
end

local function SplitLines(text)
    local lines = {}
    for line in (text .. "\n"):gmatch("(.-)\r?\n") do
        lines[#lines + 1] = line
    end
    while #lines > 0 and lines[1]:match("^%s*$") do
        table.remove(lines, 1)
    end
    while #lines > 0 and lines[#lines]:match("^%s*$") do
        table.remove(lines)
    end
    return lines
end

local function HeapPush(heap, key, priority)
    local keys, priorities = heap.Keys, heap.Priorities
    local index = heap.Size + 1
    heap.Size = index
    while index > 1 do
        local parent = math.floor(index / 2)
        if priorities[parent] <= priority then
            break
        end
        keys[index], priorities[index] = keys[parent], priorities[parent]
        index = parent
    end
    keys[index], priorities[index] = key, priority
end

local function HeapPop(heap)
    local keys, priorities = heap.Keys, heap.Priorities
    local size = heap.Size
    local topKey = keys[1]
    local lastKey, lastPriority = keys[size], priorities[size]
    keys[size], priorities[size] = nil, nil
    size = size - 1
    heap.Size = size

    if size > 0 then
        local index = 1
        while true do
            local child = index * 2
            if child > size then
                break
            end
            if child < size and priorities[child + 1] < priorities[child] then
                child = child + 1
            end
            if priorities[child] >= lastPriority then
                break
            end
            keys[index], priorities[index] = keys[child], priorities[child]
            index = child
        end
        keys[index], priorities[index] = lastKey, lastPriority
    end

    return topKey
end

function Map.New(config)
    config = config or {}
    local origin = config.Origin or {}

    local map = setmetatable({}, Map)
    map.CellSize = config.CellSize or 1.0
    map.FloorHeight = config.FloorHeight or 4.0
    map.bAllowDiagonal = config.bAllowDiagonal or false
    map.Origin = {
        X = origin.X or 0.0,
        Y = origin.Y or 0.0,
        Z = origin.Z or 0.0,
    }
    map.Floors = {}
    map.MinFloor = nil
    map.MaxFloor = nil
    map.Links = {}
    return map
end

function Map:LoadFloor(floor, text)
    if type(floor) ~= "number" or floor < 1 or floor ~= math.floor(floor) or floor >= KEY_STRIDE then
        error("[Map] Floor index must be an integer from 1 to " .. (KEY_STRIDE - 1))
    end

    local lines = SplitLines(text)
    local floorData = { Width = 0, Height = #lines, Walkable = {} }

    for y, line in ipairs(lines) do
        if #line >= KEY_STRIDE or y >= KEY_STRIDE then
            error("[Map] Floor " .. floor .. " is larger than " .. (KEY_STRIDE - 1) .. " cells")
        end

        local row = {}
        for x = 1, #line do
            local character = line:sub(x, x)
            if character == "." then
                row[x] = true
            elseif character ~= "#" and character ~= " " then
                print("[Map] Unknown cell '" .. character .. "' at", floor, x, y, "treated as wall")
            end
        end
        floorData.Walkable[y] = row
        floorData.Width = math.max(floorData.Width, #line)
    end

    self.Floors[floor] = floorData
    self.MinFloor = self.MinFloor and math.min(self.MinFloor, floor) or floor
    self.MaxFloor = self.MaxFloor and math.max(self.MaxFloor, floor) or floor
    return floorData
end

function Map:AddLink(fromCell, toCell, cost, bOneWay)
    local fromKey = ToKey(fromCell.Floor, fromCell.X, fromCell.Y)
    local toKey = ToKey(toCell.Floor, toCell.X, toCell.Y)

    self.Links[fromKey] = self.Links[fromKey] or {}
    table.insert(self.Links[fromKey], { Key = toKey, Cost = cost })

    if not bOneWay then
        self.Links[toKey] = self.Links[toKey] or {}
        table.insert(self.Links[toKey], { Key = fromKey, Cost = cost })
    end
end

function Map:IsWalkableAt(floor, x, y)
    local floorData = self.Floors[floor]
    if not floorData then
        return false
    end
    local row = floorData.Walkable[y]
    return row ~= nil and row[x] == true
end

function Map:IsWalkable(cell)
    return self:IsWalkableAt(cell.Floor, cell.X, cell.Y)
end

function Map.IsSameCell(a, b)
    return a ~= nil and b ~= nil and a.Floor == b.Floor and a.X == b.X and a.Y == b.Y
end

function Map:CellToWorld(cell)
    return MakeVector(
        self.Origin.X - (cell.Y - 1) * self.CellSize,
        self.Origin.Y + (cell.X - 1) * self.CellSize,
        self.Origin.Z + (cell.Floor - 1) * self.FloorHeight
    )
end

function Map:WorldToCell(location)
    local x = math.floor((location.Y - self.Origin.Y) / self.CellSize + 0.5) + 1
    local y = math.floor((self.Origin.X - location.X) / self.CellSize + 0.5) + 1
    local floor = math.floor((location.Z - self.Origin.Z) / self.FloorHeight) + 1

    if self.MinFloor then
        floor = math.max(self.MinFloor, math.min(self.MaxFloor, floor))
    end

    return { Floor = floor, X = x, Y = y }
end

function Map:Heuristic(x, y, goalX, goalY, bAllowDiagonal)
    local dx = math.abs(x - goalX)
    local dy = math.abs(y - goalY)
    if bAllowDiagonal then
        local diagonal = math.min(dx, dy)
        return (dx + dy + (SQRT2 - 2.0) * diagonal) * self.CellSize
    end
    return (dx + dy) * self.CellSize
end

local function BuildPath(Key, CameFrom)
    local Path = {}
    while Key do
        local Floor, X, Y = FromKey(Key)
        table.insert(Path, 1, { Floor = Floor, X = X, Y = Y })
        Key = CameFrom[Key]
    end
    return Path
end

function Map:FindPath(startCell, goalCell, options)
    options = options or {}
    local bAllowDiagonal = options.bAllowDiagonal
    if bAllowDiagonal == nil then
        bAllowDiagonal = self.bAllowDiagonal
    end
    local isBlocked = options.IsBlocked
    local maxNodes = options.MaxNodes or DEFAULT_MAX_NODES

    if not self:IsWalkable(goalCell) then
        return nil, "GoalNotWalkable"
    end

    local startKey = ToKey(startCell.Floor, startCell.X, startCell.Y)
    local goalKey = ToKey(goalCell.Floor, goalCell.X, goalCell.Y)
    local goalX, goalY = goalCell.X, goalCell.Y
    local cellSize = self.CellSize
    local diagonalSize = cellSize * SQRT2

    local costSoFar = { [startKey] = 0.0 }
    local cameFrom = {}
    local closed = {}
    local open = { Keys = {}, Priorities = {}, Size = 0 }
    local expanded = 0

    HeapPush(open, startKey, self:Heuristic(startCell.X, startCell.Y, goalX, goalY, bAllowDiagonal))

    local function CanEnter(floor, x, y, key)
        if not self:IsWalkableAt(floor, x, y) then
            return false
        end
        if isBlocked and key ~= goalKey and isBlocked(floor, x, y) then
            return false
        end
        return true
    end

    local function Relax(currentKey, nextKey, x, y, stepCost)
        if closed[nextKey] then
            return
        end
        local newCost = costSoFar[currentKey] + stepCost
        local oldCost = costSoFar[nextKey]
        if oldCost == nil or newCost < oldCost then
            costSoFar[nextKey] = newCost
            cameFrom[nextKey] = currentKey
            HeapPush(open, nextKey, newCost + self:Heuristic(x, y, goalX, goalY, bAllowDiagonal))
        end
    end

    while open.Size > 0 do
        local currentKey = HeapPop(open)

        if not closed[currentKey] then
            if currentKey == goalKey then
                return BuildPath(goalKey, cameFrom), costSoFar[goalKey]
            end

            closed[currentKey] = true
            expanded = expanded + 1
            if expanded > maxNodes then
                return nil, "MaxNodesExceeded"
            end

            local floor, x, y = FromKey(currentKey)

            for _, direction in ipairs(ORTHOGONAL_DIRECTIONS) do
                local nx, ny = x + direction[1], y + direction[2]
                local nextKey = ToKey(floor, nx, ny)
                if CanEnter(floor, nx, ny, nextKey) then
                    Relax(currentKey, nextKey, nx, ny, cellSize)
                end
            end

            if bAllowDiagonal then
                for _, direction in ipairs(DIAGONAL_DIRECTIONS) do
                    local nx, ny = x + direction[1], y + direction[2]
                    local nextKey = ToKey(floor, nx, ny)
                    if CanEnter(floor, nx, ny, nextKey)
                        and CanEnter(floor, nx, y, ToKey(floor, nx, y))
                        and CanEnter(floor, x, ny, ToKey(floor, x, ny)) then
                        Relax(currentKey, nextKey, nx, ny, diagonalSize)
                    end
                end
            end

            local links = self.Links[currentKey]
            if links then
                for _, link in ipairs(links) do
                    local lf, lx, ly = FromKey(link.Key)
                    if CanEnter(lf, lx, ly, link.Key) then
                        Relax(currentKey, link.Key, lx, ly, link.Cost)
                    end
                end
            end
        end
    end

    local FallbackCell = options.GetFallbackCell and options.GetFallbackCell() or nil
    if FallbackCell ~= nil then
        local FallbackKey = ToKey(FallbackCell.Floor, FallbackCell.X, FallbackCell.Y)
        if closed[FallbackKey] then
            return BuildPath(FallbackKey, cameFrom), costSoFar[FallbackKey], "Unreachable"
        end
    end

    return nil, "Unreachable"
end

function Map:FindPathWorld(fromLocation, toLocation, options)
    return self:FindPath(self:WorldToCell(fromLocation), self:WorldToCell(toLocation), options)
end

return Map
