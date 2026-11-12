local ItemData = {}

ItemData.Order = {
    "BatterySmall",
    "BatteryLarge",
    "CardKey",
    "CarKey",
    "FuelCan",
}

ItemData.Items = {
    BatterySmall = { Name = "배터리(소)", Weight = 0.5, BatterySeconds = 45.0 },
    BatteryLarge = { Name = "배터리(대)", Weight = 1.0, BatterySeconds = 90.0 },
    CardKey = { Name = "카드키", Weight = 0.1 },
    CarKey = { Name = "차키", Weight = 0.1 },
    FuelCan = { Name = "연료통", Weight = 6.0 },
}

function ItemData.Get(id)
    return ItemData.Items[id]
end

function ItemData.IsUsable(id)
    local item = ItemData.Items[id]
    return item ~= nil and item.BatterySeconds ~= nil
end

return ItemData
