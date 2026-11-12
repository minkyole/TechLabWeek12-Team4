package.path = "Assets/Scripts/GameLogic/?.lua;" .. package.path

local Runtime = { mTime = 0.0, mQueue = {}, mActors = {}, mSounds = {}, mEvents = {}, mPlaying = true, mPlayerLocation = { X = 0.0, Y = 0.0, Z = 0.0 }, mNextHandle = 0 }
local GameManager = {}
local Light = {}
local Actor = {}
Light.__index = Light
Actor.__index = Actor

function GameManager.IsPlaying()
    return Runtime.mPlaying
end

function GameManager.GetPlayerLocation()
    return Runtime.mPlayerLocation
end

package.loaded.GameManager = GameManager

function Light:GetIntensity()
    assert(not self.mDestroyed, "destroyed light accessed")
    return self.mIntensity
end

function Light:SetIntensity(Intensity)
    assert(not self.mDestroyed, "destroyed light accessed")
    assert(Intensity >= 0.0 and Intensity <= self.mBaseIntensity, "flicker exceeds the fixture's brightness range")
    if Intensity ~= self.mIntensity then
        self.mHistory[#self.mHistory + 1] = { mTime = Runtime.mTime, mScale = Intensity / self.mBaseIntensity }
    end
    self.mIntensity = Intensity
end

function Light:IsVisible()
    assert(not self.mDestroyed, "destroyed light accessed")
    return self.mVisible
end

function Light:GetWorldLocation()
    assert(not self.mDestroyed, "destroyed light accessed")
    return self.mLocation
end

function Actor:GetName()
    return self.mName
end

function Actor:GetPointLight()
    return self.mLight
end

function PlaySound(Path, Loop, Volume)
    assert(Path == "Assets/Sounds/CeilingLightFlicker.wav" and not Loop)
    assert(Volume > 0.0 and Volume <= 0.14, "flicker sound must stay quiet")
    Runtime.mNextHandle = Runtime.mNextHandle + 1
    local Sound = { mTime = Runtime.mTime, mVolume = Volume, mPlaying = true, mPitch = 1.0 }
    Runtime.mSounds[Runtime.mNextHandle] = Sound
    Runtime.mEvents[#Runtime.mEvents + 1] = Sound
    return Runtime.mNextHandle
end

function StopSound(Handle)
    Runtime.mSounds[Handle].mPlaying = false
end

function SetSoundVolume(Handle, Volume)
    Runtime.mSounds[Handle].mVolume = Volume
end

function SetSoundPitch(Handle, Pitch)
    assert(Pitch >= 0.94 and Pitch <= 1.06)
    Runtime.mSounds[Handle].mPitch = Pitch
end

function IsSoundPlaying(Handle)
    local Sound = Runtime.mSounds[Handle]
    return Sound.mPlaying and Runtime.mTime - Sound.mTime < 0.18
end

function Wait(Seconds)
    coroutine.yield("wait", Seconds)
end

local function Resume(Entry)
    local Success, Kind, Seconds = coroutine.resume(Entry.mCoroutine)
    assert(Success, Kind)
    if coroutine.status(Entry.mCoroutine) == "dead" then
        return false
    end
    assert(Kind == "wait" and Seconds >= 0.0)
    Entry.mRemaining = Seconds
    return true
end

function StartCoroutine(Coroutine)
    local Entry = { mCoroutine = Coroutine, mRemaining = 0.0 }
    if Resume(Entry) then
        Runtime.mQueue[#Runtime.mQueue + 1] = Entry
    end
end

local function NewActor(Name, Distance, Visible)
    local Component = setmetatable({ mIntensity = 0.256, mBaseIntensity = 0.256, mLocation = { X = Distance, Y = 0.0, Z = 3.5 }, mVisible = Visible ~= false, mDestroyed = false, mHistory = {} }, Light)
    local Fixture = setmetatable({ mName = Name, mLight = Component }, Actor)
    Runtime.mActors[#Runtime.mActors + 1] = Fixture
    return Fixture
end

local function Load()
    Runtime.mTime = 0.0
    Runtime.mQueue = {}
    Runtime.mActors = {}
    Runtime.mSounds = {}
    Runtime.mEvents = {}
    Runtime.mPlaying = true
    Runtime.mPlayerLocation = { X = 0.0, Y = 0.0, Z = 0.0 }
    Runtime.mNextHandle = 0
    package.loaded.CeilingLightFlicker = nil
    math.randomseed(417)
    return require("CeilingLightFlicker")
end

local function Advance(Flicker, Duration)
    for Frame = 1, math.ceil(Duration / 0.01) do
        Runtime.mTime = Runtime.mTime + 0.01
        for Index = #Runtime.mQueue, 1, -1 do
            local Entry = Runtime.mQueue[Index]
            Entry.mRemaining = Entry.mRemaining - 0.01
            if Entry.mRemaining <= 0.0 and not Resume(Entry) then
                table.remove(Runtime.mQueue, Index)
            end
        end
        local Dimmed = 0
        for _, Fixture in ipairs(Runtime.mActors) do
            Flicker.Tick(Fixture)
            if Fixture.mLight ~= nil and not Fixture.mLight.mDestroyed and Fixture.mLight.mIntensity < Fixture.mLight.mBaseIntensity then
                Dimmed = Dimmed + 1
            end
        end
        assert(Dimmed <= 1, "multiple ceiling fixtures flicker simultaneously")
    end
end

local Flicker = Load()
for Index = 1, 20 do
    Flicker.Start(NewActor("Ceiling" .. Index, 2.0))
end
local Far = NewActor("Far", 100.0)
local Hidden = NewActor("Hidden", 2.0, false)
Flicker.Start(Far)
Flicker.Start(Hidden)
Advance(Flicker, 0.59)
assert(#Runtime.mEvents == 0, "startup lights must stay steady")
Advance(Flicker, 160.0)
assert(#Runtime.mEvents > 80, "nearby fixtures still flicker too rarely")
local PulseCount = 0
local PreviousTime = -math.huge
local Intervals = {}
local BurstSizes = {}
for _, Event in ipairs(Runtime.mEvents) do
    local Interval = Event.mTime - PreviousTime
    if Interval >= Flicker.Config.MinCooldown then
        if PulseCount > 0 then
            assert(PulseCount >= 2 and PulseCount <= 5, "fluorescent bursts must contain two to five pulses")
            BurstSizes[PulseCount] = true
        end
        Intervals[#Intervals + 1] = Interval
        PulseCount = 1
    else
        PulseCount = PulseCount + 1
        assert(PulseCount <= 5, "fluorescent burst exceeds five pulses")
    end
    PreviousTime = Event.mTime
end
assert(#Intervals > 30 and math.abs(Intervals[2] - Intervals[3]) > 0.01, "flicker must be frequent and irregular")
local DifferentBurstSizes = 0
for Size in pairs(BurstSizes) do
    DifferentBurstSizes = DifferentBurstSizes + 1
end
assert(DifferentBurstSizes >= 2, "every burst repeats the same number of pulses")
local SawDeepDim = false
local SawPartialRecovery = false
local SawLongDim = false
local TargetCount = 0
for _, Fixture in ipairs(Runtime.mActors) do
    if #Fixture.mLight.mHistory > 0 then
        TargetCount = TargetCount + 1
    end
    for Index, Change in ipairs(Fixture.mLight.mHistory) do
        SawDeepDim = SawDeepDim or Change.mScale <= 0.12
        SawPartialRecovery = SawPartialRecovery or Change.mScale >= 0.55 and Change.mScale <= 0.9
        local NextChange = Fixture.mLight.mHistory[Index + 1]
        SawLongDim = SawLongDim or Change.mScale <= 0.12 and NextChange ~= nil and NextChange.mTime - Change.mTime >= 0.18
    end
end
assert(SawDeepDim and SawPartialRecovery and SawLongDim, "fluorescent bursts need visible dropouts, unstable restrikes and occasional longer darkness")
assert(TargetCount > 0 and TargetCount <= 3, "equal-distance fixtures must still be limited to three nearby targets")
assert(Far.mLight.mIntensity == 0.256 and Hidden.mLight.mIntensity == 0.256)
for _, Fixture in ipairs(Runtime.mActors) do
    Flicker.Stop(Fixture)
    assert(Fixture.mLight.mIntensity == 0.256, "stop did not restore the original 80-percent brightness")
    Fixture.mLight.mDestroyed = true
end
Advance(Flicker, 45.0)
assert(#Runtime.mQueue == 0, "stopped coroutines did not finish")
print(string.format("PASS %d irregular fluorescent bursts, %d pulses, visible dropouts, unstable restrikes, quiet audio and cleanup", #Intervals, #Runtime.mEvents))

Flicker = Load()
Flicker.Config.InitialMinDelay = 0.0
Flicker.Config.InitialMaxDelay = 0.0
Flicker.Config.MinPulseCount = 2
Flicker.Config.MaxPulseCount = 2
Flicker.Config.LongDimChance = 0.0
local Fixture = NewActor("Reloadable", 2.0)
Flicker.Start(Fixture)
Advance(Flicker, 0.01)
assert(Fixture.mLight.mIntensity < 0.256)
Runtime.mPlayerLocation = { X = 100.0, Y = 0.0, Z = 0.0 }
Advance(Flicker, 0.01)
assert(Fixture.mLight.mIntensity == 0.256 and not Runtime.mEvents[1].mPlaying, "moving away must immediately restore the fixture and stop its contact sound")
Runtime.mPlaying = false
Advance(Flicker, 0.01)
assert(Fixture.mLight.mIntensity == 0.256 and not Runtime.mEvents[1].mPlaying, "inactive gameplay did not restore light and stop audio")
Flicker.Stop(Fixture)
Advance(Flicker, 45.0)
assert(#Runtime.mQueue == 0)
Runtime.mPlaying = true
Runtime.mPlayerLocation = { X = 0.0, Y = 0.0, Z = 0.0 }
Flicker.Start(Fixture)
Advance(Flicker, 0.01)
local Replacement = setmetatable({ mName = Fixture.mName, mLight = Fixture.mLight }, Actor)
Flicker.Start(Replacement)
assert(Fixture.mLight.mIntensity == 0.256, "reload captured a dimmed baseline")
Advance(Flicker, 0.6)
assert(Fixture.mLight.mIntensity == 0.256, "double flicker did not restore brightness")
Flicker.Stop(Replacement)
Fixture.mLight.mDestroyed = true
Advance(Flicker, 45.0)
assert(#Runtime.mQueue == 0)
local Missing = setmetatable({ mName = "Missing" }, Actor)
Flicker.Start(Missing)
assert(#Runtime.mQueue == 0)
print("PASS phase transitions, distance updates, reload during flicker, destroyed-light safety and missing-light handling")

Flicker = Load()
local Nearby = { NewActor("Nearest", 1.0), NewActor("NearSecond", 3.0), NewActor("NearThird", 5.0) }
local Farther = NewActor("FourthNearest", 7.0)
local Outside = NewActor("OutsideRadius", 8.1)
local Below = NewActor("Downstairs", 1.0)
local Above = NewActor("Upstairs", 1.0)
local NextArea = NewActor("NextArea", 22.0)
Below.mLight.mLocation.Z = -0.5
Above.mLight.mLocation.Z = 7.5
for _, Candidate in ipairs(Runtime.mActors) do
    Flicker.Start(Candidate)
end
Advance(Flicker, 30.0)
for _, Candidate in ipairs(Nearby) do
    assert(#Candidate.mLight.mHistory > 0, "a nearest fixture was never selected")
end
assert(#Farther.mLight.mHistory == 0 and #Outside.mLight.mHistory == 0, "distant fixtures must not consume nearby flicker events")
assert(#Below.mLight.mHistory == 0 and #Above.mLight.mHistory == 0, "fixtures on other floors must not be selected")
assert(#NextArea.mLight.mHistory == 0, "an unexplored area started flickering")
local PreviousHistoryCounts = {}
for Index, Candidate in ipairs(Nearby) do
    PreviousHistoryCounts[Index] = #Candidate.mLight.mHistory
end
Runtime.mPlayerLocation = { X = 20.0, Y = 0.0, Z = 0.0 }
Advance(Flicker, 3.0)
assert(#NextArea.mLight.mHistory > 0, "flicker selection did not follow the player into a new area")
for Index, Candidate in ipairs(Nearby) do
    assert(Candidate.mLight.mIntensity == Candidate.mLight.mBaseIntensity, "leaving an area did not restore its lights")
    for ChangeIndex = PreviousHistoryCounts[Index] + 1, #Candidate.mLight.mHistory do
        assert(Candidate.mLight.mHistory[ChangeIndex].mScale == 1.0, "a previous-area fixture kept flickering after the player left")
    end
end
Runtime.mPlayerLocation = nil
local MissingPlayerHistory = #NextArea.mLight.mHistory
Advance(Flicker, 2.0)
assert(NextArea.mLight.mIntensity == NextArea.mLight.mBaseIntensity, "a missing player must restore the current fixture")
for Index = MissingPlayerHistory + 1, #NextArea.mLight.mHistory do
    assert(NextArea.mLight.mHistory[Index].mScale == 1.0, "flicker continued without a player")
end
for _, Candidate in ipairs(Runtime.mActors) do
    Flicker.Stop(Candidate)
    Candidate.mLight.mDestroyed = true
end
Advance(Flicker, 10.0)
assert(#Runtime.mQueue == 0, "nearby polling coroutines survived cleanup")
print("PASS nearest-three selection, eight-metre radius, floor filtering, moving-player retargeting and missing-player cleanup")

Flicker = Load()
Runtime.mPlayerLocation.Z = 0.5
local Garage = NewActor("TallGarageFixture", 2.0)
Garage.mLight.mLocation.Z = 4.8
Flicker.Start(Garage)
Advance(Flicker, 3.0)
assert(#Garage.mLight.mHistory > 0, "the garage's taller ceiling must remain eligible")
Flicker.Stop(Garage)
Garage.mLight.mDestroyed = true
Advance(Flicker, 10.0)
assert(#Runtime.mQueue == 0)
print("PASS nearby selection includes the garage's taller ceiling")
