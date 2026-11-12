local GAME_SCENE = "Assets/Scenes/Demo/06_Game.scene"
local BACKGROUND = "Assets/UI/Title/Background.png"
local BACKGROUND_WIDTH, BACKGROUND_HEIGHT = 1620, 864
local TITLE_IMAGE = "Assets/UI/Title/TitleWordmark-v2.png"

-- Keep the generated transparent images intact. These bounds align the visible
-- stone face with the actual click rectangle, excluding transparent margins.
local BUTTON = {
    Path = "Assets/UI/Title/CellarButton.png",
    Width = 2172, Height = 724,
    Left = 20, Top = 156, FaceWidth = 2134, FaceHeight = 404,
}
local BUTTON_HOVER = {
    Path = "Assets/UI/Title/CellarButtonHover.png",
    Width = 2172, Height = 724,
    Left = 18, Top = 152, FaceWidth = 2134, FaceHeight = 408,
}

local bActionRequested = false

local function V(x, y)
    return FVector2.new(x, y)
end

local function Contains(rect, point)
    return point.X >= rect.X and point.X < rect.X + rect.Width
        and point.Y >= rect.Y and point.Y < rect.Y + rect.Height
end

local function DrawButton(label, rect, hovered, textSize, viewport)
    local skin = hovered and BUTTON_HOVER or BUTTON
    local imageSize = V(rect.Width * skin.Width / skin.FaceWidth,
                        rect.Height * skin.Height / skin.FaceHeight)
    local imageOffset = V(rect.X - rect.Width * skin.Left / skin.FaceWidth,
                          rect.Y - rect.Height * skin.Top / skin.FaceHeight)
    AddUIImage(skin.Path, imageSize, V(0, 0), V(0, 0), imageOffset)
    AddUIText(label, textSize, V(0.5, 0.5), V(0.5, 0.5),
        V(rect.X + rect.Width * 0.5 - viewport.X * 0.5,
          rect.Y + rect.Height * 0.5 - viewport.Y * 0.5))
end

function BeginPlay()
    SetCursorMode(CursorMode.Free)
    bActionRequested = false
end

function Tick(dt)
    local viewport = GetViewportSize()
    if viewport.X <= 0 or viewport.Y <= 0 then
        return
    end

    local scale = math.max(0.5, math.min(1.5, math.min(viewport.X / 1280, viewport.Y / 720)))
    local left = math.max(12, viewport.X * 0.10)
    local width = math.max(1, math.min(360 * scale, viewport.X - left * 2))
    local height = 68 * scale
    local gap = 22 * scale
    local top = math.min(viewport.Y * 0.53, viewport.Y - height * 2 - gap - 24 * scale)
    local startButton = { X = left, Y = top, Width = width, Height = height }
    local quitButton = { X = left, Y = top + height + gap, Width = width, Height = height }
    local mouse = GetMousePosition()
    local acceptsInput = IsMouseOverGameViewport()
    local startHovered = acceptsInput and Contains(startButton, mouse)
    local quitHovered = acceptsInput and Contains(quitButton, mouse)

    -- Cover the viewport without stretching the supplied background image.
    local cover = math.max(viewport.X / BACKGROUND_WIDTH, viewport.Y / BACKGROUND_HEIGHT)
    AddUIImage(BACKGROUND, V(BACKGROUND_WIDTH * cover, BACKGROUND_HEIGHT * cover),
        V(0.5, 0.5), V(0.5, 0.5), V(0, 0))
    -- Align the visible lettering while preserving the logo aspect and alpha.
    local titleWidth = math.min(560 * scale, viewport.X * 0.52)
    local titleScale = titleWidth / 2120
    AddUIImage(TITLE_IMAGE, V(2172 * titleScale, 724 * titleScale),
        V(0, 0), V(0, 0),
        V(left - 28 * titleScale, viewport.Y * 0.30 - 362 * titleScale))
    DrawButton("게임 시작", startButton, startHovered, 30 * scale, viewport)
    DrawButton("종료", quitButton, quitHovered, 30 * scale, viewport)

    if bActionRequested or not acceptsInput or not IsMousePressed(MouseButton.Left) then
        return
    end

    if startHovered then
        bActionRequested = true
        LoadScene(GAME_SCENE)
    elseif quitHovered then
        bActionRequested = true
        QuitGame()
    end
end

function OnReload()
    SetCursorMode(CursorMode.Free)
    bActionRequested = false
end
