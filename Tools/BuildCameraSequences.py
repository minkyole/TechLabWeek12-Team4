import argparse as Argparse
import json as Json
import math as Math
from pathlib import Path


FPS = 480
Tolerances = (0.00005, 0.00005, 0.00005, 0.001, 0.001, 0.001, 0.0003)


def Smooth(Value):
    Progress = max(0.0, min(1.0, Value))
    return Progress * Progress * (3.0 - 2.0 * Progress)


def Blend(First, Last, Alpha):
    return First + (Last - First) * Alpha


def View(X, Y, Z, TargetX, TargetY, TargetZ, FOV):
    return (X, Y, Z, Math.degrees(Math.atan2(Z - TargetZ, Math.hypot(TargetX - X, TargetY - Y))), Math.degrees(Math.atan2(TargetY - Y, TargetX - X)), 0.0, FOV)


def CarCamera(Time):
    DrivingTime = max(0.0, Time - 2.2)
    if Time < 2.2:
        Progress = Smooth(Time / 2.2)
        return View(Blend(-3.8, -4.3, Progress), 3.4, Blend(2.2, 2.6, Progress), 0.0, 0.0, 1.55, Blend(52.0, 55.0, Progress))
    FollowTime = min(DrivingTime, 8.0)
    ReleaseTime = max(0.0, FollowTime - 6.0)
    CameraTime = FollowTime - ReleaseTime * ReleaseTime * 0.25
    CameraDistance = CameraTime * CameraTime * 0.72
    Progress = Smooth(CameraTime / 7.0)
    TargetDistance = CameraDistance + Blend(0.5, 30.0, Smooth(ReleaseTime / 2.0))
    return View(CameraDistance - 4.3, Blend(3.4, 2.2, Smooth(CameraTime / 4.0)), Blend(2.6, 3.6, Progress), TargetDistance, 0.0, 1.55, Blend(55.0, 62.0, Progress))


def DoorCamera(Time):
    if Time < 3.6:
        Progress = Smooth(Time / 3.6)
        return View(Blend(-2.6, -1.6, Progress), 0.0, 1.65, 0.0, 0.0, 1.5, 58.0)
    if Time < 7.8:
        Progress = Smooth((Time - 3.6) / 4.2)
        Step = Math.sin((Time - 3.6) * Math.pi * 4.0) * 0.018 * Math.sin(Progress * Math.pi)
        return View(Blend(-1.6, 5.0, Progress), 0.0, 1.65 + Step, 16.0, 0.0, 1.75, Blend(58.0, 66.0, Progress))
    Progress = Smooth((Time - 7.8) / 2.0)
    return View(Blend(5.0, 6.5, Progress), Blend(0.0, -2.8, Progress), Blend(1.65, 2.1, Progress), Blend(16.0, 0.0, Progress), 0.0, Blend(1.75, 1.5, Progress), Blend(66.0, 52.0, Progress))


def IntroCamera(Time, Config):
    Progress = Smooth(Time / Config["BlendStart"])
    return tuple(Blend(Config["CameraStart"][Axis], Config["CameraEnd"][Axis], Progress) for Axis in ["X", "Y", "Z", "Pitch", "Yaw"]) + (0.0, 60.0)


def IntroBob(Time, Config):
    Bob = Math.sin((Time - Config["WalkStart"]) * Math.pi * 4.0) * 0.012 * Math.sin(Math.pi * (Time - Config["WalkStart"]) / (Config["WalkEnd"] - Config["WalkStart"])) if Config["WalkStart"] < Time < Config["WalkEnd"] else 0.0
    return (0.0, 0.0, Bob, 0.0, 0.0, 0.0, 60.0)


def Interpolate(Values, Frames, Index, Frame, Cuts):
    First = Frames[Index]
    Last = Frames[Index + 1]
    if Last in Cuts:
        return Values[First]
    Before = Frames[Index - 1] if Index > 0 and First not in Cuts else First
    After = Frames[Index + 2] if Index + 2 < len(Frames) and Frames[Index + 2] not in Cuts else Last
    Span = Last - First
    Alpha = (Frame - First) / Span
    Square = Alpha * Alpha
    Cube = Square * Alpha
    Result = []
    Hold = Values[First][:3] == Values[Last][:3]
    for Axis in range(7):
        Start = Values[First][Axis]
        End = Values[Last][Axis]
        if Axis >= 3:
            Delta = Math.remainder(End - Start, 360.0) if Axis < 6 else End - Start
            Result.append(Start + Delta * Alpha)
        elif Hold:
            Result.append(Start)
        else:
            FirstTangent = (End - Values[Before][Axis]) / (Last - Before)
            LastTangent = (Values[After][Axis] - Start) / (After - First)
            Result.append((2.0 * Cube - 3.0 * Square + 1.0) * Start + (Cube - 2.0 * Square + Alpha) * Span * FirstTangent + (-2.0 * Cube + 3.0 * Square) * End + (Cube - Square) * Span * LastTangent)
    return tuple(Result)


def Sequence(Name, Evaluate, Duration, Boundaries=(), CutTimes=()):
    Values = [Evaluate(Frame / FPS) for Frame in range(round(Duration * FPS) + 1)]
    Cuts = {round(Time * FPS) for Time in CutTimes}
    Kept = {0, len(Values) - 1, *(round(Time * FPS) for Time in Boundaries)}
    for Frame in Cuts:
        Kept.update([Frame - 1, Frame])
    while True:
        Frames = sorted(Kept)
        Added = []
        for Index in range(len(Frames) - 1):
            Worst = 1.0
            Candidate = None
            for Frame in range(Frames[Index] + 1, Frames[Index + 1]):
                Predicted = Interpolate(Values, Frames, Index, Frame, Cuts)
                Error = max(abs(Predicted[Axis] - Values[Frame][Axis]) / Tolerances[Axis] for Axis in range(7))
                if Error > Worst:
                    Worst = Error
                    Candidate = Frame
            if Candidate is not None:
                Added.append(Candidate)
        if not Added:
            break
        Kept.update(Added)
    Sections = []
    for Frame in sorted(Kept):
        Value = Values[Frame]
        Key = {"Frame": Frame, "Location": dict(zip(["X", "Y", "Z"], Value[:3])), "Rotation": dict(zip(["Pitch", "Yaw", "Roll"], Value[3:6])), "FOV": Value[6]}
        Sections.append({"Curve": 0, "EaseExp": 2.0, "UseShortestRotation": True, "CutAtStart": Frame in Cuts, "Keys": [Key]})
    return {"StartFrame": 0, "DurationSeconds": Math.ceil(Duration), "FPS": FPS, "Tracks": [{"Name": Name, "Type": "Camera", "Color": {"R": 0.31, "G": 0.59, "B": 0.9, "A": 0.3}, "UseCatmullRom": True, "Sections": Sections}]}


def Actor(Name, Module, Data):
    Transform = {"Location": {"X": 0, "Y": 0, "Z": 0}, "Rotation": {"Pitch": 0, "Yaw": 0, "Roll": 0}, "Scale": {"X": 1, "Y": 1, "Z": 1}}
    return {"Class": "ASequenceActor", "Name": Name, "RootComponent": Name, "Properties": {"bAutoPlay": False}, "Components": [{"Class": "USceneComponent", "Name": Name, "Parent": "", "Properties": {"Transform": Transform}}, {"Class": "UScriptComponent", "Name": Name + "Script", "Parent": "", "Properties": {"ScriptPath": "Assets/Scripts/GameLogic/" + Module + ".lua"}}], "Sequence": Data}


def Upsert(Actors, Item):
    Index = next((Index for Index, Existing in enumerate(Actors) if Existing.get("Name") == Item["Name"]), None)
    if Index is None:
        Actors.append(Item)
    else:
        Actors[Index] = Item


def InstallIntroSequences(Actors, Config):
    Upsert(Actors, Actor("Campus_Intro_CameraSequence", "IntroCameraTrack", Sequence("Intro Camera", lambda Time: IntroCamera(Time, Config), Config["BlendStart"])))
    Upsert(Actors, Actor("Campus_Intro_BobSequence", "IntroBobTrack", Sequence("Intro Camera Bob", lambda Time: IntroBob(Time, Config), Config["Duration"], (Config["WalkStart"], Config["WalkEnd"]))))


def InstallEndingSequences(Actors, Routes=("Car", "Door")):
    for Route in Routes:
        Data = Sequence(Route + " Ending Camera", CarCamera if Route == "Car" else DoorCamera, 14.0 if Route == "Car" else 13.0, (8.2, 10.2) if Route == "Car" else (7.8, 9.8), (2.2,) if Route == "Car" else (3.6,))
        Upsert(Actors, Actor("Ending" + Route + "CameraSequence", "Ending" + Route + "CameraTrack", Data))


def Main():
    import BuildCampusMap as Campus
    Parser = Argparse.ArgumentParser()
    Parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parents[1])
    Root = Parser.parse_args().root.resolve()
    Config = Campus.LoadMap(Root)["Intro"]
    Target = Root / "Assets/Scenes/Demo/06_Game.scene"
    Scene = Json.loads(Target.read_text(encoding="utf-8-sig"))
    Actors = Scene["World"]["PersistentLevel"]["Actors"]
    InstallIntroSequences(Actors, Config)
    InstallEndingSequences(Actors)
    Target.write_text(Json.dumps(Scene, ensure_ascii=False, indent=4) + "\n", encoding="utf-8")
    print("Installed camera sequences in the gameplay scene")


if __name__ == "__main__":
    Main()
