local Door = require("Door")
local DoorAudio = require("DoorAudio")
local GameManager = require("GameManager")
local Inventory = require("Inventory")
local Runtime = { mDoor = nil, mBlockedMessageTime = 0.0 }

function BeginPlay()
    Runtime.mDoor = Door.Register(self)
end

function Tick(DeltaTime)
    Door.Tick(Runtime.mDoor, DeltaTime)
    DoorAudio.Tick(Runtime.mDoor)
    if not GameManager.IsPlaying() or Inventory.IsOpen() then
        return
    end
    local PlayerLocation = GameManager.GetPlayerLocation()
    if PlayerLocation == nil or Door.FindNearest(PlayerLocation) ~= Runtime.mDoor then
        Runtime.mBlockedMessageTime = 0.0
        return
    end
    if IsKeyPressed(Key.E) then
        Runtime.mBlockedMessageTime = Door.SetOpen(Runtime.mDoor, not Runtime.mDoor.mOpen, PlayerLocation) and 0.0 or 1.0
    end
    local Message = Runtime.mDoor.mOpen and "[E] Close door" or "[E] Open door"
    if Runtime.mBlockedMessageTime > 0.0 then
        Runtime.mBlockedMessageTime = math.max(0.0, Runtime.mBlockedMessageTime - DeltaTime)
        Message = "Doorway blocked"
    end
    AddUIText(Message, 32, FVector2.new(0.5, 1.0), FVector2.new(0.5, 1.0), FVector2.new(0.0, -130.0))
end

function EndPlay()
    Door.Unregister(self)
    Runtime.mDoor = nil
end

function OnReload()
    Runtime.mDoor = Door.Register(self)
end
