local GameManager = require("GameManager")
local Inventory = require("Inventory")

local PICKUP_KEY = Key.E
local TOGGLE_KEY = Key.Tab
local NEXT_KEY = Key.Down
local PREV_KEY = Key.Up
local USE_KEY = Key.Enter
local PICKUP_SOUND = "Assets/Sounds/아이템획득.mp3"
local PICKUP_VOLUME = 0.8

local function LogSlots()
    local lines = {}
    for index, slot in ipairs(Inventory.GetSlots()) do
        local marker = index == Inventory.GetSelectedIndex() and ">" or " "
        table.insert(lines, string.format("%s %s x%d", marker, slot.Id, slot.Count))
    end
    print(string.format("[Inventory] open=%s weight=%.1f speed=%.2f\n%s",
        tostring(Inventory.IsOpen()), Inventory.GetTotalWeight(), Inventory.GetSpeedMultiplier(),
        #lines > 0 and table.concat(lines, "\n") or "  (empty)"))
end

local function TryPickup()
    local entry = GameManager.FindNearestItem(self:GetActorLocation(), Inventory.Config.PickupRadius)
    if not entry then
        return
    end

    if Inventory.Add(entry.ItemId) then
        local actor = entry.Actor
        GameManager.UnregisterItem(actor)
        actor:Destroy()
        PlaySound(PICKUP_SOUND, false, PICKUP_VOLUME)
        print("[Inventory] Picked up", entry.ItemId)
    end
end

function BeginPlay()
    Inventory.Reset()
end

function Tick(dt)
    if not GameManager.IsPlaying() then
        return
    end

    if IsKeyPressed(Key.LeftBracket) then
        Inventory.Add("CarKey")
        Inventory.Add("FuelCan")
        Inventory.Add("CardKey")
        LogSlots()
    end

    if IsKeyPressed(PICKUP_KEY) then
        TryPickup()
    end

    if IsKeyPressed(TOGGLE_KEY) then
        Inventory.Toggle()
        LogSlots()
    end

    if not Inventory.IsOpen() then
        return
    end

    if IsKeyPressed(NEXT_KEY) then
        Inventory.SelectNext()
        LogSlots()
    end

    if IsKeyPressed(PREV_KEY) then
        Inventory.SelectPrev()
        LogSlots()
    end

    if IsKeyPressed(USE_KEY) then
        local bUsed = Inventory.UseSelected()
        print("[Inventory] Use", bUsed and "ok" or "failed")
        LogSlots()
    end
end

function EndPlay()
    Inventory.Reset()
end
