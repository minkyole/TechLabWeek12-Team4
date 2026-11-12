package.path = "Assets/Scripts/GameLogic/?.lua;" .. package.path

local Runtime = { mHeartbeatStrength = 1.0 }
local Audio = {}

function Audio.GetHeartbeatStrength()
    return Runtime.mHeartbeatStrength
end

package.loaded.EnemyAudio = Audio

local HorrorCamera = require("HorrorCamera")
local Tests = {}

function Tests.Initialize()
    HorrorCamera.Start("NativePlayer", HorrorCameraTestActor)
    HorrorCamera.Update("NativePlayer", FVector.new(0.0, 0.0, 0.0), 1.0 / 60.0, false)
end

function Tests.RunAndRecover()
    Runtime.mHeartbeatStrength = 0.0
    for Index = 1, 120 do
        HorrorCamera.Update("NativePlayer", FVector.new(10.0 / 120.0, 0.0, 0.0), 1.0 / 60.0, true)
    end
    for Index = 1, 15 do
        HorrorCamera.Update("NativePlayer", FVector.new(0.0, 0.0, 0.0), 1.0 / 60.0, false)
    end
end

function Tests.Deactivate()
    HorrorCamera.Update("NativePlayer", FVector.new(0.0, 0.0, 0.0), 0.1, false, false)
end

function Tests.Stop()
    HorrorCamera.Stop("NativePlayer")
end

return Tests
