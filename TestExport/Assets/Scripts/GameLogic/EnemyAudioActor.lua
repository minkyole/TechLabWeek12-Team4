local EnemyAudio = require("EnemyAudio")

function BeginPlay()
    EnemyAudio.Start()
end

function Tick(dt)
    EnemyAudio.Tick(dt)
end

function EndPlay()
    EnemyAudio.Stop()
end
