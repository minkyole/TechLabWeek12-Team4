local GameManager = require("GameManager")
local Minimap = {}

Minimap.Config = { Radius = 5.0, Size = 184.0, Margin = 20.0 }

function Minimap.Draw()
    local Player = GameManager.GetPlayer()
    local Map = GameManager.GetMap()
    if not GameManager.IsPlaying() or Player == nil or Map == nil or Map.CellSize <= 0.0 then
        return
    end

    local Camera = Player:GetCamera()
    if Camera == nil then
        return
    end
    local Location = Player:GetActorLocation()
    local Floor = Map:WorldToCell(Location).Floor
    local Data = Map.Floors[Floor]
    if Data == nil then
        return
    end

    local Radius = math.min(math.max(0.1, Minimap.Config.Radius), Map.CellSize * 14.0)
    local CellRadius = Radius / Map.CellSize
    local PlayerX = (Location.Y - Map.Origin.Y) / Map.CellSize + 1.0
    local PlayerY = (Map.Origin.X - Location.X) / Map.CellSize + 1.0
    local MinimumX = math.floor(PlayerX - CellRadius)
    local MaximumX = math.ceil(PlayerX + CellRadius)
    local MinimumY = math.floor(PlayerY - CellRadius)
    local MaximumY = math.ceil(PlayerY + CellRadius)
    local Rows = {}
    for Y = MinimumY, MaximumY do
        local Cells = {}
        for X = MinimumX, MaximumX do
            local Blocked = X >= 1 and X <= Data.Width and Y >= 1 and Y <= Data.Height and not Map:IsWalkableAt(Floor, X, Y)
            Cells[#Cells + 1] = Blocked and "#" or "."
        end
        Rows[#Rows + 1] = table.concat(Cells)
    end

    AddUIMinimap(Camera, Rows, FVector2.new(PlayerX - MinimumX + 0.5, PlayerY - MinimumY + 0.5), Map.CellSize, Radius, Minimap.Config.Size, FVector2.new(Minimap.Config.Margin, Minimap.Config.Margin))
end

return Minimap
