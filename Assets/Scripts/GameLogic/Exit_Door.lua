local GameManager = require("GameManager")
local Inventory = require("Inventory")
local EndingCutscene = require("EndingCutscene")

local ExitRoute = "Door"
local InteractionKey = Key.E
local InteractionRadius = 2.0
local InteractionHeight = 2.0
local MessageDuration = 2.5
local MessageSize = 40
local MessageOffset = FVector2.new(0, -180)

local RequiredItems = {
    { Id = "CardKey", Name = "카드키" },
}

local FeedbackMessage = nil
local FeedbackTimeRemaining = 0.0

local function GetMissingItemNames()
    local MissingItems = {}

    for _, Item in ipairs(RequiredItems) do
        if not Inventory.Has(Item.Id) then
            table.insert(MissingItems, Item.Name)
        end
    end

    return MissingItems
end

local function IsPlayerInRange()
    local PlayerLocation = GameManager.GetPlayerLocation()
    if not PlayerLocation then
        return false
    end

    local ExitLocation = self:GetActorLocation()
    local DeltaX = ExitLocation.X - PlayerLocation.X
    local DeltaY = ExitLocation.Y - PlayerLocation.Y
    return math.abs(ExitLocation.Z - PlayerLocation.Z) <= InteractionHeight and DeltaX * DeltaX + DeltaY * DeltaY <= InteractionRadius * InteractionRadius
end

local function TryEscape()
    local MissingItems = GetMissingItemNames()
    if #MissingItems > 0 then
        FeedbackMessage = "아직은 탈출할 수 없습니다, 필요한 아이템 : " .. table.concat(MissingItems, ", ")
        FeedbackTimeRemaining = MessageDuration
        return false
    end

    GameManager.Escape(ExitRoute)
    return true
end

function BeginPlay()
    EndingCutscene.RegisterSubject(self, ExitRoute)
    FeedbackMessage = nil
    FeedbackTimeRemaining = 0.0
end

function Tick(DeltaTime)
    if not GameManager.IsPlaying() then
        return
    end

    local InRange = IsPlayerInRange()
    if InRange and IsKeyPressed(InteractionKey) then
        if TryEscape() then
            return
        end
    end

    if FeedbackTimeRemaining > 0.0 then
        FeedbackTimeRemaining = math.max(0.0, FeedbackTimeRemaining - DeltaTime)
        AddUIText(FeedbackMessage, MessageSize, FVector2.new(0.5, 1), FVector2.new(0.5, 1), MessageOffset)
    elseif InRange then
        AddUIText("[E] 비상문으로 탈출", MessageSize, FVector2.new(0.5, 1), FVector2.new(0.5, 1), MessageOffset)
    end
end

function EndPlay()
    EndingCutscene.UnregisterSubject(self)
    FeedbackMessage = nil
    FeedbackTimeRemaining = 0.0
end
