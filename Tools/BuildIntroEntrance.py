import argparse as Argparse
import json as Json
from pathlib import Path

import BuildCampusMap as Campus


def Script(Name, Module):
    return {"Class": "UScriptComponent", "Name": Name, "Parent": "", "Properties": {"ScriptPath": "Assets/Scripts/GameLogic/" + Module + ".lua"}}


def Bounds(Actor):
    Root = Campus.RootComponent(Actor)["Properties"]["Transform"]
    Collision = next((Component for Component in Actor["Components"] if Component["Class"] == "UBoxComponent"), None)
    if Collision is None:
        return None
    Center = Root["Location"]
    Extent = Collision["Properties"]["BoxExtent"]
    return tuple((Center[Axis] - Extent[Axis], Center[Axis] + Extent[Axis]) for Axis in ["X", "Y", "Z"])


def InstallIntroEntrance(Actors, Data):
    Retained = []
    Mapping = {}
    for Index, Actor in enumerate(Actors):
        Name = Actor["RootComponent"]
        Box = Bounds(Actor) if Name.startswith("Campus_Wall_") else None
        RoomWall = Box is not None and Box[0][0] >= 0.5 and Box[0][1] <= 12.5 and Box[1][0] >= 10.5 and Box[1][1] <= 19.5
        if Name.startswith("Campus_Intro_") or RoomWall:
            continue
        Mapping[Index] = len(Retained)
        Retained.append(Actor)
    for Actor in Retained:
        if "AttachParent" in Actor:
            Actor["AttachParent"]["Actor"] = Mapping[Actor["AttachParent"]["Actor"]]
    Actors[:] = Retained
    for Actor in list(Actors):
        Name = Actor["RootComponent"]
        Box = Bounds(Actor) if Name.startswith("Campus_Wall_") else None
        if Box is None or Box[0][1] <= -1.5 or Box[0][0] >= 0.5 or Box[1][1] <= 13.5 or Box[1][0] >= 15.5 or Box[2][0] >= 3.0:
            continue
        Surface = next(Component for Component in Actor["Components"] if Component["Class"] == "UStaticMeshComponent")["OverrideMaterials"][0]
        Root = Campus.RootComponent(Actor)["Properties"]["Transform"]["Location"]
        Parts = [(Box[1][0], 13.5, Box[2][0], Box[2][1]), (15.5, Box[1][1], Box[2][0], Box[2][1]), (max(13.5, Box[1][0]), min(15.5, Box[1][1]), 3.0, Box[2][1])]
        for Index, (FirstY, LastY, FirstZ, LastZ) in enumerate(Parts):
            if LastY > FirstY and LastZ > FirstZ:
                Actors.append(Campus.CubeActor(Name.removeprefix("Campus_") + "EntrySection" + str(Index), (Root["X"], (FirstY + LastY) * 0.5, (FirstZ + LastZ) * 0.5), (Box[0][1] - Box[0][0], LastY - FirstY, LastZ - FirstZ), Surface, True))
        Actor["Components"] = [Component for Component in Actor["Components"] if Component["Class"] not in ["UStaticMeshComponent", "UBoxComponent"]]
    Concrete = Campus.Material((0.63, 0.69, 0.7), "Assets/Textures/Campus/Concrete_BaseColor.png", "Assets/Textures/Campus/Concrete_Normal.png", 3)
    Steel = Campus.Material((0.65, 0.72, 0.76), Data["WallBaseColor"], Data["WallNormal"], 2)
    Dark = Campus.Material((0.055, 0.075, 0.085))
    Warning = Campus.Material((0.85, 0.53, 0.1))
    Asphalt = Campus.Material((0.62, 0.67, 0.69), "Assets/Textures/Campus/Asphalt_BaseColor.png", "Assets/Textures/Campus/Asphalt_Normal.png", 3)
    for Name, Location, Size in [("LeftCurb", (6.5, 11, 0.12), (12, 0.4, 0.24)), ("RightCurb", (6.5, 19, 0.12), (12, 0.4, 0.24)), ("RearLeft", (1, 12, 2), (1, 3, 4)), ("RearRight", (1, 17.5, 2), (1, 4, 4)), ("DoorLintel", (1, 14.5, 3.5), (1, 2, 1)), ("Foundation", (6, 15, -0.25), (13, 9, 0.5)), ("Roof", (2.2, 14.5, 3.55), (3.4, 3.4, 0.2))]:
        Actors.append(Campus.CubeActor("Intro_" + Name, Location, Size, Concrete, True))
    Actors.append(Campus.CubeActor("Intro_YardSurface", (6.5, 15, 0.006), (12, 8, 0.012), Asphalt))
    Actors.append(Campus.CubeActor("Intro_ApproachGround", (16, 15, -0.25), (8, 12, 0.5), Asphalt, True))
    for Name, Y in [("Left", 10.7), ("Right", 19.3)]:
        Fence = Campus.MeshActor("Intro_" + Name + "Fence", "Models/Campus/FencePanel.obj", (6.5, Y, 0), Scale=(1, 3, 1), Extent=(6, 0.08, 1.4), Center=(0, 0, 1.4))
        Fence["Components"][1]["Properties"]["Transform"]["Rotation"]["Yaw"] = 90
        Actors.append(Fence)
    for Y in [13.0, 16.0]:
        Actors.append(Campus.CubeActor("Intro_Bollard" + str(Y), (3.8, Y, 0.5), (0.18, 0.18, 1), Warning, True))
    Actors.append(Campus.CubeActor("Intro_LampPost", (8, 18.1, 1.85), (0.12, 0.12, 3.7), Dark, True))
    Actors.append(Campus.CubeActor("Intro_LampArm", (8, 17.8, 3.55), (0.12, 0.7, 0.12), Dark))
    Door = Campus.CubeActor("Intro_Door", (1, 14.5, 1.5), (0.18, 1.98, 3), Steel, True)
    Door["Components"].append(Script("IntroDoorScript", "IntroDoor"))
    Actors.append(Door)
    for Index in range(10):
        Trim = Campus.CubeActor("Intro_ShutterRib" + str(Index), (0, 0, -1.35 + Index * 0.3), (0.22, 1.94, 0.035), Dark)
        Campus.RootComponent(Trim)["Parent"] = Door["RootComponent"]
        Door["Components"].extend(Trim["Components"])
    for Y in [13.4, 15.6]:
        Actors.append(Campus.CubeActor("Intro_DoorFrame" + str(Y), (1.28, Y, 1.55), (0.25, 0.16, 3.1), Warning))
    for Y in [13.65, 15.35]:
        Actors.append(Campus.CubeActor("Intro_GuideLine" + str(Y), (5.8, Y, 0.012), (10.5, 0.08, 0.02), Warning))
    for X in [2.0, 2.3, 2.6]:
        Actors.append(Campus.CubeActor("Intro_Threshold" + str(X), (X, 14.5, 0.015), (0.12, 1.65, 0.025), Warning))
    Actors.append(Campus.MeshActor("Intro_Console", "Models/FacilityProps/ControlConsole.obj", (3.0, 17.2, 0), Yaw=90, Extent=(0.95, 0.47, 0.78), Center=(0, 0, 0.78)))
    Actors.append(Campus.MeshActor("Intro_Rack", "Models/FacilityProps/StorageRack.obj", (6.0, 12.0, 0), Yaw=90, Extent=(0.47, 1.45, 1.34), Center=(0, 0, 1.34)))
    for Name, Position, Color, Intensity in [("EntryLight", (8, 17.5, 3.5), 4294960860, 0.88), ("DoorLight", (2.6, 14.5, 3.35), 4285772287, 0.72)]:
        Light = Campus.MeshActor("Intro_" + Name, "Models/Facility/CeilingLight.obj", Position, Scale=(0.65, 0.65, 0.65))
        Light["Components"].append({"Class": "UPointLightComponent", "Name": Name + "Point", "Parent": Light["RootComponent"], "Properties": {"Transform": Campus.Transform(), "AttenuationRadius": 9, "Intensity": Intensity, "LightColor": Color, "LightFalloffExponent": 2, "bCastShadows": False, "bVisible": True}})
        Actors.append(Light)
    CameraStart = Data["Intro"]["CameraStart"]
    Camera = {"Class": "AActor", "Name": "Campus_Intro_Camera", "RootComponent": "Campus_Intro_Camera", "Properties": {}, "Components": [{"Class": "UCameraComponent", "Name": "Campus_Intro_Camera", "Parent": "", "Properties": {"Transform": Campus.Transform(tuple(CameraStart[Axis] for Axis in ["X", "Y", "Z"]), Yaw=CameraStart["Yaw"]), "mFieldOfView": 60, "mNearClipPlane": 0.05, "mFarClipPlane": 500, "mMoveSpeed": 0, "mMouseSensitivity": 0, "mWheelSpeed": 0}}, Script("IntroDirector", "IntroActor")]}
    Camera["Components"][0]["Properties"]["Transform"]["Rotation"]["Pitch"] = CameraStart["Pitch"]
    Actors.append(Camera)
    FadeDuration = Data["Intro"]["FadeDuration"]
    Actors.append({"Class": "ASequenceActor", "Name": "Campus_Intro_Fade", "RootComponent": "Campus_Intro_Fade", "Properties": {"bAutoPlay": True}, "Components": [{"Class": "USceneComponent", "Name": "Campus_Intro_Fade", "Parent": "", "Properties": {"Transform": Campus.Transform()}}, Script("IntroFadeScript", "IntroFade")], "Sequence": {"StartFrame": 0, "DurationSeconds": FadeDuration, "FPS": 30, "Tracks": [{"Name": "Facility Arrival Fade In", "Type": "Fade", "Color": {"R": 0.2, "G": 0.5, "B": 0.7, "A": 1}, "Sections": [{"Curve": 0, "EaseExp": 2, "Keys": [{"Frame": 0, "Alpha": 1}, {"Frame": 15, "Alpha": 1}]}, {"Curve": 2, "EaseExp": 2, "Keys": [{"Frame": 15, "Alpha": 1}, {"Frame": round(FadeDuration * 30), "Alpha": 0}]}]}]}})
    Player = next(Actor for Actor in Actors if Actor["Class"] == "APlayerActor")
    Campus.RootComponent(Player)["Properties"]["Transform"] = Campus.Transform(tuple(Data["Intro"]["Spawn"][Axis] for Axis in ["X", "Y", "Z"]), Yaw=Data["Intro"]["Yaw"])
    from BuildCameraSequences import InstallIntroSequences
    InstallIntroSequences(Actors, Data["Intro"])
    Campus.InstallCeilingLightFlicker(Actors)


def UpdateIntroEntrance(Root):
    Target = Root / "Assets/Scenes/Demo/06_Game.scene"
    Scene = Json.loads(Target.read_text(encoding="utf-8-sig"))
    InstallIntroEntrance(Scene["World"]["PersistentLevel"]["Actors"], Campus.LoadMap(Root))
    Target.write_text(Json.dumps(Scene, ensure_ascii=False, indent=4) + "\n", encoding="utf-8")
    print("Installed outdoor arrival yard, entrance canopy, shutter, intro camera and fade-in sequence")


def Main():
    Parser = Argparse.ArgumentParser()
    Parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parents[1])
    Arguments = Parser.parse_args()
    UpdateIntroEntrance(Arguments.root.resolve())


if __name__ == "__main__":
    Main()
