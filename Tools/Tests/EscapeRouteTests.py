import json as Json
import sys as Sys
import tempfile as Tempfile
from pathlib import Path

Root = Path(__file__).resolve().parents[2]
Sys.path.insert(0, str(Root / "Tools"))

import BuildCampusMap as Campus
import BuildEscapeRoute as EscapeRoute


def ReadActors(Project, Scene):
    return Json.loads((Project / "Assets/Scenes" / Scene).read_text(encoding="utf-8-sig"))["World"]["PersistentLevel"]["Actors"]


def VerifyScene(Project, Data):
    Actors = ReadActors(Project, "Demo/06_Game.scene")
    Named = {Actor["RootComponent"]: Actor for Actor in Actors}
    Door = Named["DoorExit"]
    Location = Campus.RootComponent(Door)["Properties"]["Transform"]["Location"]
    assert all(abs(Location[Axis] - Data["Landmarks"]["DoorExit"]["Location"][Axis]) < 0.0001 for Axis in ["X", "Y", "Z"])
    assert any(Component["Properties"].get("StaticMesh") == "Models/EscapeRoute/EmergencyExitDoor.obj" for Component in Door["Components"])
    Frame = Named["DoorExitFrame"]
    FrameLocation = Campus.RootComponent(Frame)["Properties"]["Transform"]["Location"]
    ExpectedFrameLocation = Campus.Vector(Location["X"], Location["Y"] + 0.92, 0)
    assert all(abs(FrameLocation[Axis] - ExpectedFrameLocation[Axis]) < 0.0001 for Axis in ["X", "Y", "Z"])
    for Component in Frame["Components"]:
        if Component["Class"] == "UBoxComponent" and not Component["Name"].endswith("Header"):
            Properties = Component["Properties"]
            assert abs(abs(Properties["Transform"]["Location"]["Y"]) - Properties["BoxExtent"]["Y"] - 0.92) < 0.0001
    for Label in ["Left", "Right"]:
        Wall = Named["Campus_EscapeCorridorWall" + Label]
        Collision = next(Component for Component in Wall["Components"] if Component["Class"] == "UBoxComponent")
        assert Collision["Properties"]["bBlockComponent"]
        assert Collision["Properties"]["BoxExtent"] == Campus.Vector(12, 0.5, 2)
    assert "Campus_EscapeCorridorRoof" in Named and "Campus_EscapeCorridorFoundation" in Named
    assert sum(Name.startswith("Campus_EscapeFloorArrow") for Name in Named) == 5
    MapActor = next(Actor for Actor in Actors if any(Component["Properties"].get("ScriptPath", "").endswith("/MapActor.lua") for Component in Actor["Components"]))
    assert Campus.RootComponent(MapActor)["Properties"]["Transform"]["Location"] == Data["Origin"]
    assert Campus.RootComponent(Named["CarExit"])["Properties"]["Transform"]["Location"] == Data["Landmarks"]["CarExit"]["Location"]
    for Actor in Actors:
        if "AttachParent" in Actor:
            assert 0 <= Actor["AttachParent"]["Actor"] < len(Actors)
    for Actor in Actors:
        for Component in Actor["Components"]:
            Mesh = Component["Properties"].get("StaticMesh", "")
            if Mesh.startswith("Models/"):
                assert (Root / "Assets" / Mesh).is_file(), Mesh


def Main():
    Data = Campus.LoadMap(Root)
    VerifyScene(Root, Data)
    print("PASS scene door, frame collision, corridor, navigation origin, car exit and asset references")
    with Tempfile.TemporaryDirectory(prefix="HitoriEscapeRoute") as Folder:
        Project = Path(Folder)
        Scenes = Project / "Assets/Scenes"
        (Scenes / "Demo").mkdir(parents=True)
        (Scenes / "Demo/06_Game.scene").write_bytes((Root / "Assets/Scenes/Demo/06_Game.scene").read_bytes())
        Campus.BuildScene(Project, Data)
        EscapeRoute.InstallScene(Project, Data)
        VerifyScene(Project, Data)
        print("PASS complete campus regeneration preserves the escape corridor and emergency door")
        Before = (Scenes / "Demo/06_Game.scene").read_bytes()
        EscapeRoute.InstallScene(Project, Data)
        assert (Scenes / "Demo/06_Game.scene").read_bytes() == Before
        print("PASS repeated route generation leaves scenes and attachment indices unchanged")


if __name__ == "__main__":
    Main()
