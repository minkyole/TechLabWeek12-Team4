local EndingCutscene = require("EndingCutscene")
local GameManager = require("GameManager")
local Black = "Assets/UI/Title/Black.png"
local Runtime = { mActionRequested = false, mCreditsTime = 0.0, mCreditsDone = false }

-- { 문구, 글자 크기 }. 빈 문구는 줄 간격만 차지한다.
local Credits = {
    { "The Basement", 56 },
    { "", 0 },
    { "김승범", 24 },
    { "권성욱", 24 },
    { "신용준", 24 },
    { "최효석", 24 },
    { "", 0 },
    { "", 0 },
    { "", 0 },
    { "Krafton Jungle TechLab 4 · Week 9 Team 1", 22 },
    { "", 0 },
    { "", 0 },
    { "", 0 },
    { "Thanks for playing", 34 },
}
local CreditsSpeed = 70.0
local CreditsLineGap = 1.6
-- 컷신 스킵 입력이 크레딧까지 넘기지 않도록 잠시 스킵을 막는다.
local CreditsSkipDelay = 0.5

local function V(X, Y)
    return FVector2.new(X, Y)
end

local function DrawButton(Label, X, Y, Width, Height, Viewport)
    local Mouse = GetMousePosition()
    local Hovered = IsMouseOverGameViewport() and Mouse.X >= X and Mouse.X < X + Width and Mouse.Y >= Y and Mouse.Y < Y + Height
    local Path = Hovered and "Assets/UI/Title/CellarButtonHover.png" or "Assets/UI/Title/CellarButton.png"
    local FaceHeight = Hovered and 408 or 404
    local Left = Hovered and 18 or 20
    local Top = Hovered and 152 or 156
    AddUIImage(Path, V(Width * 2172 / 2134, Height * 724 / FaceHeight), V(0, 0), V(0, 0), V(X - Width * Left / 2134, Y - Height * Top / FaceHeight))
    AddUIText(Label, Height * 0.42, V(0.5, 0.5), V(0.5, 0.5), V(X + Width * 0.5 - Viewport.X * 0.5, Y + Height * 0.5 - Viewport.Y * 0.5))
    return Hovered and IsMousePressed(MouseButton.Left)
end

local function DrawResult(State, Config, Viewport, Scale)
    AddUIImage(Black, Viewport, V(0, 0), V(0, 0), V(0, 0))
    AddUIText("탈출 성공  /  " .. Config.Title, 46 * Scale, V(0.5, 0.30), V(0.5, 0.5), V(0, 0))
    AddUIText(Config.Epilogue, 23 * Scale, V(0.5, 0.40), V(0.5, 0.5), V(0, 0))
    AddUIText(string.format("생존 시간 %.1f초   ·   사용한 배터리 %d개", State.mResult.ElapsedTime, State.mResult.BatteriesUsed), 21 * Scale, V(0.5, 0.48), V(0.5, 0.5), V(0, 0))
    local Width = math.min(320 * Scale, Viewport.X * 0.8)
    local Height = 56 * Scale
    local X = (Viewport.X - Width) * 0.5
    local Y = Viewport.Y * 0.59
    local Restart = DrawButton("[R] 다시 도전", X, Y, Width, Height, Viewport)
    local Title = DrawButton("[Enter] 타이틀로", X, Y + Height + 16 * Scale, Width, Height, Viewport)
    if not Runtime.mActionRequested and (Restart or Title or IsKeyPressed(Key.R) or IsKeyPressed(Key.Enter)) then
        Runtime.mActionRequested = true
        LoadScene((Restart or IsKeyPressed(Key.R)) and State.mResult.GameScene or "Assets/Scenes/Demo/05_TheBasement.scene")
    end
end

local function DrawCredits(DeltaTime, Viewport, Scale)
    Runtime.mCreditsTime = Runtime.mCreditsTime + DeltaTime
    AddUIImage(Black, Viewport, V(0, 0), V(0, 0), V(0, 0))
    -- 첫 줄의 윗변이 화면 아래쪽 끝에서 시작해 위로 올라간다.
    local Y = -Runtime.mCreditsTime * CreditsSpeed * Scale
    for _, Line in ipairs(Credits) do
        local Size = math.max(Line[2], 24) * Scale
        if Line[1] ~= "" and Y < 0 and Y > -Viewport.Y - Size then
            AddUIText(Line[1], Line[2] * Scale, V(0.5, 1.0), V(0.5, 0.0), V(0, Y))
        end
        Y = Y + Size * CreditsLineGap
    end
    local CanSkip = Runtime.mCreditsTime > CreditsSkipDelay
    if CanSkip then
        AddUIText("[Space / Enter] 건너뛰기", 16 * Scale, V(0.98, 0.985), V(1, 1), V(0, 0))
    end
    if Y < -Viewport.Y or (CanSkip and (IsKeyPressed(Key.Space) or IsKeyPressed(Key.Enter))) then
        Runtime.mCreditsDone = true
    end
end

local function DrawCutscene(State, Config, Viewport, Scale)
    local Time = State.mElapsed
    local BarHeight = Viewport.Y * 0.115
    AddUIImage(Black, V(Viewport.X, BarHeight), V(0, 0), V(0, 0), V(0, 0))
    AddUIImage(Black, V(Viewport.X, BarHeight), V(0, 1), V(0, 1), V(0, 0))
    if State.mFading then
        return
    end
    if Time > 1.2 and Time < 5.8 then
        AddUIText(Config.Subtitle, 24 * Scale, V(0.5, 0.925), V(0.5, 0.5), V(0, 0))
    elseif Time >= 7.8 and Time < Config.Duration - 0.7 then
        AddUIText(Config.Epilogue, 24 * Scale, V(0.5, 0.925), V(0.5, 0.5), V(0, 0))
    end
    AddUIText("[Space / Enter] 건너뛰기", 16 * Scale, V(0.98, 0.985), V(1, 1), V(0, 0))
end

function BeginPlay()
    Runtime.mActionRequested = false
    Runtime.mCreditsTime = 0.0
    Runtime.mCreditsDone = false
    EndingCutscene.RegisterView(self)
end

function Tick(DeltaTime)
    local State = EndingCutscene.GetState()
    if State == nil then
        if GameManager.Phase == GameManager.EPhase.Ending then
            ResetTimeEffects()
            EndingCutscene.StartPending()
        end
        State = EndingCutscene.GetState()
        if State == nil then
            return
        end
    end
    local WasFinished = State.mFinished
    if not State.mFinished and State.mElapsed > 0.35 and (IsKeyPressed(Key.Space) or IsKeyPressed(Key.Enter)) then
        EndingCutscene.BeginFadeOut()
    end
    EndingCutscene.Update(DeltaTime)
    local Viewport = GetViewportSize()
    if Viewport.X <= 0 or Viewport.Y <= 0 then
        return
    end
    local Scale = math.max(0.25, math.min(1.5, math.min(Viewport.X / 1280, Viewport.Y / 720)))
    local Config = EndingCutscene.Config[State.mRoute]
    if State.mFinished then
        if not WasFinished then
            -- 컷신이 끝난 프레임은 비워 두고, 다음 프레임부터 크레딧을 띄운다.
        elseif not Runtime.mCreditsDone then
            DrawCredits(DeltaTime, Viewport, Scale)
        else
            DrawResult(State, Config, Viewport, Scale)
        end
    else
        DrawCutscene(State, Config, Viewport, Scale)
    end
end

function EndPlay()
    EndingCutscene.UnregisterView(self)
end
