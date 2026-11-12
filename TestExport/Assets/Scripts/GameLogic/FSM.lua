local FSM = {}
FSM.__index = FSM

local function ResetRuntime(Machine)
    Machine.mRunning = false
    Machine.mCurrentState = nil
    Machine.mPendingState = nil
    Machine.mElapsedTime = 0.0
    Machine.mStopRequested = false
    Machine.mIsFlushing = false
end

local function CallState(Machine, CallbackName, ...)
    local State = Machine.mStates[Machine.mCurrentState]
    local Callback = State and State[CallbackName]
    if Callback == nil then
        return
    end

    local StateName = Machine.mCurrentState
    local PreviousDepth = Machine.mCallbackDepth
    Machine.mCallbackDepth = PreviousDepth + 1
    local Success, Message = pcall(Callback, Machine.mContext, ...)
    Machine.mCallbackDepth = PreviousDepth

    if not Success then
        ResetRuntime(Machine)
        error("[FSM] " .. StateName .. "." .. CallbackName .. ": " .. tostring(Message), 0)
    end
end

local function StopNow(Machine)
    Machine.mStopRequested = true
    CallState(Machine, "Exit", nil)
    ResetRuntime(Machine)
end

local function FlushTransition(Machine)
    if Machine.mCallbackDepth > 0 or Machine.mIsFlushing or not Machine.mRunning then
        return
    end

    if Machine.mStopRequested then
        StopNow(Machine)
        return
    end

    local NextState = Machine.mPendingState
    if NextState == nil then
        return
    end

    local PreviousState = Machine.mCurrentState
    Machine.mPendingState = nil
    Machine.mIsFlushing = true
    CallState(Machine, "Exit", NextState)

    if Machine.mStopRequested then
        ResetRuntime(Machine)
        return
    end

    Machine.mCurrentState = NextState
    Machine.mElapsedTime = 0.0
    CallState(Machine, "Enter", PreviousState)
    Machine.mIsFlushing = false

    if Machine.mStopRequested then
        StopNow(Machine)
    end
end

local function ValidateState(Machine, StateName)
    if type(StateName) ~= "string" or type(Machine.mStates[StateName]) ~= "table" then
        error("[FSM] Unknown state: " .. tostring(StateName), 3)
    end
end

function FSM.New(Owner, States, Data)
    assert(type(States) == "table", "[FSM] States must be a table")
    local Machine = setmetatable({ mStates = States, mContext = {}, mRunning = false, mCurrentState = nil, mPendingState = nil, mElapsedTime = 0.0, mCallbackDepth = 0, mStopRequested = false, mIsFlushing = false }, FSM)
    Machine.mContext = { mOwner = Owner, mMachine = Machine, mData = Data or {} }
    return Machine
end

function FSM.Start(Machine, StateName)
    ValidateState(Machine, StateName)
    if Machine.mRunning or Machine.mCallbackDepth > 0 then
        return false
    end

    Machine.mRunning = true
    Machine.mCurrentState = StateName
    CallState(Machine, "Enter", nil)
    if Machine.mStopRequested then
        StopNow(Machine)
    end
    return Machine.mRunning
end

function FSM.RequestState(Machine, StateName)
    ValidateState(Machine, StateName)
    if not Machine.mRunning or Machine.mStopRequested or Machine.mPendingState ~= nil or Machine.mCurrentState == StateName then
        return false
    end

    Machine.mPendingState = StateName
    return true
end

function FSM.Tick(Machine, DeltaTime)
    if not Machine.mRunning or Machine.mStopRequested then
        return false
    end
    if Machine.mCallbackDepth > 0 or Machine.mIsFlushing then
        error("[FSM] Reentrant Tick is not allowed", 2)
    end
    if type(DeltaTime) ~= "number" or DeltaTime ~= DeltaTime or DeltaTime < 0.0 or DeltaTime == math.huge then
        error("[FSM] DeltaTime must be finite and nonnegative", 2)
    end

    Machine.mElapsedTime = Machine.mElapsedTime + DeltaTime
    CallState(Machine, "Tick", DeltaTime)
    FlushTransition(Machine)
    return true
end

function FSM.HandleEvent(Machine, EventName, ...)
    if not Machine.mRunning or Machine.mStopRequested then
        return false
    end

    CallState(Machine, "HandleEvent", EventName, ...)
    FlushTransition(Machine)
    return true
end

function FSM.Stop(Machine)
    if not Machine.mRunning then
        return false
    end

    Machine.mStopRequested = true
    FlushTransition(Machine)
    return true
end

function FSM.GetCurrentState(Machine)
    return Machine.mCurrentState
end

function FSM.GetElapsedTime(Machine)
    return Machine.mElapsedTime
end

function FSM.IsRunning(Machine)
    return Machine.mRunning
end

return FSM
