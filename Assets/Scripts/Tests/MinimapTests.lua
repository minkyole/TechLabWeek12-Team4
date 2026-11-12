package.path = "Assets/Scripts/GameLogic/?.lua;" .. package.path

FVector2 = { new = function(X, Y) return { X = X, Y = Y } end }

local GameManager = require("GameManager")
local MapModule = require("Map")
local MapObstacles = require("MapObstacles")
local Minimap = require("Minimap")
local Runtime = { mCalls = {}, mPassed = 0 }
local Player = { mLocation = { X = -5.0, Y = 5.0, Z = 0.0 }, mCamera = {} }

function Player:GetActorLocation()
    return self.mLocation
end

function Player:GetCamera()
    return self.mCamera
end

function AddUIMinimap(Camera, Rows, PlayerCell, CellSize, Radius, Size, Offset)
    Runtime.mCalls[#Runtime.mCalls + 1] = { mCamera = Camera, mRows = Rows, mPlayerCell = PlayerCell, mCellSize = CellSize, mRadius = Radius, mSize = Size, mOffset = Offset }
end

local function Reset()
    GameManager.Reset()
    GameManager.StartGame()
    GameManager.Player = Player
    Player.mLocation = { X = -5.0, Y = 5.0, Z = 0.0 }
    Player.mCamera = {}
    Minimap.Config.Radius = 5.0
    Minimap.Config.Size = 184.0
    Minimap.Config.Margin = 20.0
    Runtime.mCalls = {}
    local Map = MapModule.New()
    Map:LoadFloor(1, "#############\n#...........#\n#...........#\n#...........#\n#...........#\n#...........#\n#...........#\n#...........#\n#...........#\n#...........#\n#...........#\n#...........#\n#############")
    GameManager.SetMap(Map)
    return Map
end

local function Test(Name, Run)
    local Map = Reset()
    Run(Map)
    Runtime.mPassed = Runtime.mPassed + 1
    print("PASS " .. Name)
end

local function Near(Actual, Expected)
    assert(math.abs(Actual - Expected) < 0.00001, tostring(Actual) .. " ~= " .. tostring(Expected))
end

local function Cell(Call, X, Y)
    local MinimumX = math.floor((Player.mLocation.Y - GameManager.Map.Origin.Y) / Call.mCellSize + 1.0 - Call.mRadius / Call.mCellSize)
    local MinimumY = math.floor((GameManager.Map.Origin.X - Player.mLocation.X) / Call.mCellSize + 1.0 - Call.mRadius / Call.mCellSize)
    return Call.mRows[Y - MinimumY + 1]:sub(X - MinimumX + 1, X - MinimumX + 1)
end

Test("draws a centered five metre crop with walls", function(Map)
    Minimap.Draw()
    assert(#Runtime.mCalls == 1)
    local Call = Runtime.mCalls[1]
    assert(Call.mCamera == Player.mCamera)
    assert(#Call.mRows == 11 and #Call.mRows[1] == 11)
    Near(Call.mRadius, 5.0)
    Near(Call.mPlayerCell.X, 5.5)
    Near(Call.mPlayerCell.Y, 5.5)
    Near(Call.mSize, 184.0)
    Near(Call.mOffset.X, 20.0)
    assert(Cell(Call, 1, 1) == "#" and Cell(Call, 6, 6) == ".")
end)

Test("includes facility obstacles without actor markers", function(Map)
    MapObstacles.Apply(Map, { Types = { Crate = { Center = { X = 0.0, Y = 0.0 }, Extent = { X = 0.4, Y = 0.4 } } }, Props = { { Type = "Crate", Location = { X = -4.0, Y = 5.0 }, Floor = 1 } } })
    GameManager.Enemies = setmetatable({}, { __index = function() error("Enemy data must not be read") end })
    GameManager.Items = setmetatable({}, { __index = function() error("Item data must not be read") end })
    Minimap.Draw()
    assert(Cell(Runtime.mCalls[1], 6, 5) == "#")
end)

Test("tracks fractional movement across crop boundaries", function(Map)
    Player.mLocation = { X = -5.25, Y = 5.75, Z = 0.0 }
    Minimap.Draw()
    local Call = Runtime.mCalls[1]
    Near(Call.mPlayerCell.X, 6.25)
    Near(Call.mPlayerCell.Y, 5.75)
    assert(#Call.mRows == 12 and #Call.mRows[1] == 12)
    Player.mLocation.Y = 6.25
    Minimap.Draw()
    Near(Runtime.mCalls[2].mPlayerCell.X, 5.75)
    assert(Cell(Runtime.mCalls[2], 6, 5) == ".")
end)

Test("uses the current floor and map origin", function(Map)
    Map.Origin = { X = 10.0, Y = 20.0, Z = 2.0 }
    Map.CellSize = 2.0
    Map:LoadFloor(2, "#############\n#############\n#############\n#############\n#############\n#############\n#############\n#############\n#############\n#############\n#############\n#############\n#############")
    Player.mLocation = { X = 0.0, Y = 30.0, Z = 6.0 }
    Minimap.Draw()
    local Call = Runtime.mCalls[1]
    Near(Call.mCellSize, 2.0)
    assert(Cell(Call, 6, 6) == "#")
end)

Test("hides after game over and without required references", function(Map)
    GameManager.GameOver("Test")
    Minimap.Draw()
    assert(#Runtime.mCalls == 0)
    GameManager.StartGame()
    GameManager.Player = nil
    Minimap.Draw()
    GameManager.Player = Player
    Player.mCamera = nil
    Minimap.Draw()
    Player.mCamera = {}
    GameManager.SetMap(nil)
    Minimap.Draw()
    assert(#Runtime.mCalls == 0)
end)

Test("does not manufacture walls beyond map edges", function(Map)
    Player.mLocation = { X = 0.0, Y = 0.0, Z = 0.0 }
    Minimap.Draw()
    local Call = Runtime.mCalls[1]
    assert(Call.mRows[1] == "...........")
    assert(Cell(Call, 1, 1) == "#")
end)

Test("bounds grid size for fine map cells", function(Map)
    Map.CellSize = 0.1
    Minimap.Draw()
    local Call = Runtime.mCalls[1]
    Near(Call.mRadius, 1.4)
    assert(#Call.mRows <= 32 and #Call.mRows[1] <= 32)
    Map.CellSize = 0.001
    Minimap.Draw()
    local FineCall = Runtime.mCalls[2]
    assert(#FineCall.mRows <= 32 and #FineCall.mRows[1] <= 32)
end)

GameManager.Reset()
print("Minimap tests passed: " .. Runtime.mPassed)
