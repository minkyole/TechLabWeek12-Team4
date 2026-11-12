local GameManager = require("GameManager")
local Inventory = require("Inventory")

local EXIT_ROUTE = "Car"
local INTERACTION_KEY = Key.E
local INTERACTION_RADIUS = 2.0
local MESSAGE_DURATION = 2.5
local MESSAGE_SIZE = 40
local MESSAGE_OFFSET = FVector2.new(0, -180)

local REQUIRED_ITEMS = {
    { Id = "CarKey", Name = "Key" },
    { Id = "FuelCan", Name = "Fuel" },
}

local feedbackMessage = nil
local feedbackTimeRemaining = 0.0

local function GetMissingItemNames()
    local missingItems = {}

    for _, item in ipairs(REQUIRED_ITEMS) do
        if not Inventory.Has(item.Id) then
            table.insert(missingItems, item.Name)
        end
    end

    return missingItems
end

local function IsPlayerInRange()
    local playerLocation = GameManager.GetPlayerLocation()
    if not playerLocation then
        return false
    end

    local exitLocation = self:GetActorLocation()
    local dx = exitLocation.X - playerLocation.X
    local dy = exitLocation.Y - playerLocation.Y
    return dx * dx + dy * dy <= INTERACTION_RADIUS * INTERACTION_RADIUS
end

local function TryEscape()
    local missingItems = GetMissingItemNames()
    if #missingItems > 0 then
        feedbackMessage = "You can't escape yet, Need Item : " .. table.concat(missingItems, ", ")
        feedbackTimeRemaining = MESSAGE_DURATION
        return false
    end

    GameManager.Escape(EXIT_ROUTE)
    return true
end

function BeginPlay()
    feedbackMessage = nil
    feedbackTimeRemaining = 0.0
end

function Tick(dt)
    if not GameManager.IsPlaying() then
        return
    end

    local bInRange = IsPlayerInRange()
    if bInRange and IsKeyPressed(INTERACTION_KEY) then
        if TryEscape() then
            return
        end
    end

    if feedbackTimeRemaining > 0.0 then
        feedbackTimeRemaining = math.max(0.0, feedbackTimeRemaining - dt)
        AddUIText(
            feedbackMessage,
            MESSAGE_SIZE,
            FVector2.new(0.5, 1),
            FVector2.new(0.5, 1),
            MESSAGE_OFFSET
        )
    elseif bInRange then
        AddUIText(
            "[E] Escape",
            MESSAGE_SIZE,
            FVector2.new(0.5, 1),
            FVector2.new(0.5, 1),
            MESSAGE_OFFSET
        )
    end
end

function EndPlay()
    feedbackMessage = nil
    feedbackTimeRemaining = 0.0
end
