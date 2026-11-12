import json as Json
import subprocess as Subprocess
import sys as Sys
from pathlib import Path

Root = Path(__file__).resolve().parents[2]
Sys.path.insert(0, str(Root / "Tools"))
import BuildCameraSequences as Camera
import BuildCampusMap as Campus


def Lua(Value):
    if isinstance(Value, dict):
        return "{" + ",".join("[" + Json.dumps(Key) + "]=" + Lua(Entry) for Key, Entry in Value.items()) + "}"
    if isinstance(Value, list):
        return "{" + ",".join(Lua(Entry) for Entry in Value) + "}"
    return Json.dumps(Value)


def Fixtures():
    Names = {"Campus_Intro_CameraSequence": "IntroCamera", "Campus_Intro_BobSequence": "IntroBob", "EndingCarCameraSequence": "Car", "EndingDoorCameraSequence": "Door"}
    Scene = Json.loads((Root / "Assets/Scenes/Demo/06_Game.scene").read_text(encoding="utf-8-sig"))
    Data = {Names[Actor.get("Name")]: Actor["Sequence"] for Actor in Scene["World"]["PersistentLevel"]["Actors"] if Actor.get("Name") in Names}
    assert len(Data) == 4
    for Sequence in Data.values():
        assert len(Sequence["Tracks"]) == 1 and Sequence["Tracks"][0]["Type"] == "Camera" and Sequence["Tracks"][0]["UseCatmullRom"]
    (Root / "Build").mkdir(exist_ok=True)
    (Root / "Build/CameraSequenceFixtures.lua").write_text("return " + Lua(Data), encoding="utf-8")
    Config = Campus.LoadMap(Root)["Intro"]
    Evaluators = {"IntroCamera": (lambda Time: Camera.IntroCamera(Time, Config), Config["BlendStart"]), "IntroBob": (lambda Time: Camera.IntroBob(Time, Config), Config["Duration"]), "Car": (Camera.CarCamera, 14.0), "Door": (Camera.DoorCamera, 13.0)}
    Reference = {}
    for Name, (Evaluate, Duration) in Evaluators.items():
        Times = [Index * Duration / 997 for Index in range(998)] + [2.19999, 2.2, 2.20001, 3.59999, 3.6, 3.60001, 7.79999, 7.8, 7.80001]
        Reference[Name] = [[Time, *Evaluate(min(Time, Duration))] for Time in Times]
    (Root / "Build/CameraSequenceReference.json").write_text(Json.dumps(Reference), encoding="utf-8")


def Main():
    Fixtures()
    LuaPath = Root / "vcpkg_installed/x64-windows/tools/luajit/luajit.exe"
    for Name in ["IntroTests", "EndingTests"]:
        Subprocess.run([str(LuaPath), "Assets/Scripts/Tests/" + Name + ".lua"], cwd=Root, check=True)


if __name__ == "__main__":
    Main()
