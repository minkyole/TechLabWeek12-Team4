import copy as Copy
import json as Json
import sys as Sys
import tempfile as Tempfile
from pathlib import Path

Root = Path(__file__).resolve().parents[2]
Sys.path.insert(0, str(Root / "Tools"))

import BuildCampusMap as Campus
import BuildIntroEntrance as Intro


def Verify(Actors, Data):
    Named = {Actor["RootComponent"]: Actor for Actor in Actors}
    IntroNames = [Actor["RootComponent"] for Actor in Actors if Actor["RootComponent"].startswith("Campus_Intro_")]
    assert len(IntroNames) == len(set(IntroNames))
    Player = next(Actor for Actor in Actors if Actor["Class"] == "APlayerActor")
    Transform = Campus.RootComponent(Player)["Properties"]["Transform"]
    assert Transform["Location"] == Data["Intro"]["Spawn"] and Transform["Rotation"]["Yaw"] == Data["Intro"]["Yaw"]
    assert Named["Campus_Intro_Fade"]["Properties"]["bAutoPlay"]
    Keys = Named["Campus_Intro_Fade"]["Sequence"]["Tracks"][0]["Sections"]
    assert Keys[0]["Keys"][0]["Alpha"] == 1 and Keys[-1]["Keys"][-1]["Alpha"] == 0
    assert "Campus_Intro_Roof" in Named and "Campus_Intro_Foundation" in Named
    Camera = Named["Campus_Intro_Camera"]["Components"][0]["Properties"]
    assert Camera["mMoveSpeed"] == 0 and Camera["mMouseSensitivity"] == 0
    for Actor in Actors:
        Names = {Component["Name"] for Component in Actor["Components"]}
        assert len(Names) == len(Actor["Components"])
        for Component in Actor["Components"]:
            assert not Component.get("Parent") or Component["Parent"] in Names
        if "AttachParent" in Actor:
            Parent = Actors[Actor["AttachParent"]["Actor"]]
            assert any(Component["Name"] == Actor["AttachParent"]["Component"] for Component in Parent["Components"])
        Box = Intro.Bounds(Actor)
        if Box is None or Actor["RootComponent"] == "Campus_Intro_Door":
            continue
        if Box[2][0] >= 1.0 or Box[2][1] <= 0.0:
            continue
        for Index in range(61):
            X = Data["Intro"]["Spawn"]["X"] - Index * 0.25
            Y = Data["Intro"]["Spawn"]["Y"]
            assert not (Box[0][0] - 0.25 < X < Box[0][1] + 0.25 and Box[1][0] - 0.25 < Y < Box[1][1] + 0.25), Actor["RootComponent"]


def Main():
    Data = Campus.LoadMap(Root)
    Target = Root / "Assets/Scenes/Demo/06_Game.scene"
    Actors = Json.loads(Target.read_text(encoding="utf-8-sig"))["World"]["PersistentLevel"]["Actors"]
    Verify(Actors, Data)
    print("PASS spawn, fade endpoints, camera input, room collision and component/actor attachments")
    Intro.InstallIntroEntrance(Actors, Data)
    Verify(Actors, Data)
    Before = Copy.deepcopy(Actors)
    Intro.InstallIntroEntrance(Actors, Data)
    assert Actors == Before
    print("PASS repeated intro generation is idempotent")
    with Tempfile.TemporaryDirectory(prefix="HitoriIntro") as Folder:
        Project = Path(Folder)
        Scenes = Project / "Assets/Scenes"
        (Scenes / "Demo").mkdir(parents=True)
        (Scenes / "Demo/06_Game.scene").write_bytes(Target.read_bytes())
        Campus.BuildScene(Project, Data)
        Rebuilt = Json.loads((Scenes / "Demo/06_Game.scene").read_text())["World"]["PersistentLevel"]["Actors"]
        Verify(Rebuilt, Data)
    print("PASS full campus regeneration preserves the arrival room and open entry passage")


if __name__ == "__main__":
    Main()
