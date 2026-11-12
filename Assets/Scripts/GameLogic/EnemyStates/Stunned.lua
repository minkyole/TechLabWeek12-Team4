local Common = require("EnemyStates.Common")
local Stunned = {}

function Stunned.Enter(Context, PreviousState)
    Common.ReportState(Context, PreviousState, "Stunned")
end

function Stunned.Tick(Context, DeltaTime)
    if Context.mMachine:GetElapsedTime() >= Context.mData.mStunDuration then
        if not Common.TryChase(Context) then
            local ResumeState = Context.mData.mResumeState
            Context.mMachine:RequestState(ResumeState)
        end
    end
end

return Stunned
