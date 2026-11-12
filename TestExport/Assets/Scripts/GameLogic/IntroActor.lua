local IntroCutscene = require("IntroCutscene")
local GameManager = require("GameManager")

function BeginPlay()
    IntroCutscene.RegisterView(self)
end

function Tick(DeltaTime)
    if GameManager.Phase ~= GameManager.EPhase.Intro then
        return
    end
    local State = IntroCutscene.GetState()
    if State == nil or State.mFinished then
        return
    end
    if State.mElapsed >= 0.5 and (IsKeyPressed(Key.Space) or IsKeyPressed(Key.Enter)) then
        IntroCutscene.Finish()
        return
    end
    local Viewport = GetViewportSize()
    if Viewport.X <= 0 or Viewport.Y <= 0 then
        return
    end
    local Config = IntroCutscene.Config
    local Time = State.mElapsed
    local Scale = math.max(0.25, math.min(1.5, math.min(Viewport.X / 1280, Viewport.Y / 720)))
    local Bars = Viewport.Y * 0.115 * (1.0 - math.max(0.0, math.min(1.0, (Time - Config.BarsFadeStart) / (Config.Duration - Config.BarsFadeStart))))
    AddUIImage("Assets/UI/Title/Black.png", FVector2.new(Viewport.X, Bars), FVector2.new(0, 0), FVector2.new(0, 0), FVector2.new(0, 0))
    AddUIImage("Assets/UI/Title/Black.png", FVector2.new(Viewport.X, Bars), FVector2.new(0, 1), FVector2.new(0, 1), FVector2.new(0, 0))
    for _, Dialogue in ipairs(Config.Dialogues) do
        if Time >= Dialogue.Start and Time < Dialogue.End then
            AddUIText(Dialogue.Text, 24 * Scale, FVector2.new(0.5, 0.925), FVector2.new(0.5, 0.5), FVector2.new(0, 0))
            break
        end
    end
    AddUIText("[Space / Enter] 건너뛰기", 16 * Scale, FVector2.new(0.98, 0.98), FVector2.new(1, 1), FVector2.new(0, 0))
end

function EndPlay()
    IntroCutscene.ReleaseActor(self)
end
