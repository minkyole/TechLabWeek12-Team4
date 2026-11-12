local ItemData = require("ItemData")
local Flashlight = require("Flashlight")

local Inventory = {}

Inventory.Config = {
    SlowdownPerWeight = 0.025,
    MinSpeedMultiplier = 0.85,
    PickupRadius = 2.0,
}

local counts = {}
local bOpen = false
local selectedIndex = 0

local function ClampSelection()
    local slotCount = #Inventory.GetSlots()
    if slotCount == 0 then
        selectedIndex = 0
    elseif selectedIndex < 1 then
        selectedIndex = 1
    elseif selectedIndex > slotCount then
        selectedIndex = slotCount
    end
end

function Inventory.Reset()
    counts = {}
    bOpen = false
    selectedIndex = 0
end

function Inventory.Add(id, count)
    if not ItemData.Get(id) then
        print("[Inventory] Unknown item:", id)
        return false
    end
    counts[id] = (counts[id] or 0) + (count or 1)
    ClampSelection()
    return true
end

function Inventory.Remove(id, count)
    count = count or 1
    local current = counts[id] or 0
    if current < count then
        return false
    end
    counts[id] = current - count
    if counts[id] == 0 then
        counts[id] = nil
    end
    ClampSelection()
    return true
end

function Inventory.GetCount(id)
    return counts[id] or 0
end

function Inventory.Has(id)
    return Inventory.GetCount(id) > 0
end

function Inventory.GetSlots()
    local slots = {}
    for _, id in ipairs(ItemData.Order) do
        local count = counts[id]
        if count and count > 0 then
            local item = ItemData.Get(id)
            table.insert(slots, {
                Id = id,
                Name = item.Name,
                Count = count,
                Weight = item.Weight,
                bUsable = ItemData.IsUsable(id),
            })
        end
    end
    return slots
end

function Inventory.GetTotalWeight()
    local total = 0.0
    for id, count in pairs(counts) do
        total = total + ItemData.Get(id).Weight * count
    end
    return total
end

function Inventory.GetSpeedMultiplier()
    local config = Inventory.Config
    return math.max(config.MinSpeedMultiplier, 1.0 - Inventory.GetTotalWeight() * config.SlowdownPerWeight)
end

function Inventory.IsOpen()
    return bOpen
end

function Inventory.Open()
    bOpen = true
    ClampSelection()
end

function Inventory.Close()
    bOpen = false
end

function Inventory.Toggle()
    if bOpen then
        Inventory.Close()
    else
        Inventory.Open()
    end
    return bOpen
end

function Inventory.GetSelectedIndex()
    return selectedIndex
end

function Inventory.GetSelectedSlot()
    return Inventory.GetSlots()[selectedIndex]
end

function Inventory.Select(index)
    selectedIndex = index
    ClampSelection()
end

function Inventory.SelectNext()
    local slotCount = #Inventory.GetSlots()
    if slotCount == 0 then
        return
    end
    selectedIndex = selectedIndex % slotCount + 1
end

function Inventory.SelectPrev()
    local slotCount = #Inventory.GetSlots()
    if slotCount == 0 then
        return
    end
    selectedIndex = (selectedIndex - 2) % slotCount + 1
end

function Inventory.Use(id)
    local item = ItemData.Get(id)
    if not item or not Inventory.Has(id) then
        return false
    end

    if item.BatterySeconds then
        if not Flashlight.RequestBatterySwap(item.BatterySeconds) then
            return false
        end
        return Inventory.Remove(id)
    end

    return false
end

function Inventory.UseSelected()
    local slot = Inventory.GetSelectedSlot()
    if not slot then
        return false
    end
    return Inventory.Use(slot.Id)
end

return Inventory
