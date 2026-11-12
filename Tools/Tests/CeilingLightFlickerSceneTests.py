import copy as Copy
import json as Json
import sys as Sys
import tempfile as Tempfile
import wave as Wave
from pathlib import Path

Root = Path(__file__).resolve().parents[2]
Sys.path.insert(0, str(Root / "Tools"))

import BuildCampusMap as Campus
import BuildEscapeRoute as Escape
import BuildIntroEntrance as Intro


def ReadActors(Project):
    return Json.loads((Project / "Assets/Scenes/Demo/06_Game.scene").read_text(encoding="utf-8-sig"))["World"]["PersistentLevel"]["Actors"]


def LightIntensities(Actors):
    return {(Actor["RootComponent"], Component["Name"]): Component["Properties"]["Intensity"] for Actor in Actors for Component in Actor["Components"] if Component["Class"] in ["UPointLightComponent", "USpotLightComponent", "UDirectionalLightComponent", "USkyLightComponent"]}


def Verify(Actors):
    Count = 0
    for Actor in Actors:
        Components = Actor["Components"]
        Ceiling = any(Component["Properties"].get("StaticMesh") == "Models/Facility/CeilingLight.obj" for Component in Components)
        Scripts = [Component for Component in Components if Component["Properties"].get("ScriptPath") == "Assets/Scripts/GameLogic/CeilingLightFlickerActor.lua"]
        assert len(Scripts) == (1 if Ceiling else 0), Actor["RootComponent"]
        Count += len(Scripts)
        Names = [Component["Name"] for Component in Components]
        assert len(Names) == len(set(Names))
    assert Count == 20


def Main():
    Actors = ReadActors(Root)
    Verify(Actors)
    Before = Copy.deepcopy(Actors)
    Campus.InstallCeilingLightFlicker(Actors)
    Campus.InstallCeilingLightFlicker(Actors)
    assert Actors == Before
    print("PASS all 20 ceiling fixtures have one script; other lights are excluded; repeated installation is unchanged")
    ExpectedIntensities = LightIntensities(Actors)
    Data = Campus.LoadMap(Root)
    with Tempfile.TemporaryDirectory(prefix="HitoriCeilingFlicker") as Folder:
        Project = Path(Folder)
        Scenes = Project / "Assets/Scenes/Demo"
        Scenes.mkdir(parents=True)
        (Scenes / "06_Game.scene").write_bytes((Root / "Assets/Scenes/Demo/06_Game.scene").read_bytes())
        Campus.BuildScene(Project, Data)
        Escape.InstallScene(Project, Data)
        Rebuilt = ReadActors(Project)
        Verify(Rebuilt)
        assert LightIntensities(Rebuilt) == ExpectedIntensities
        Intro.InstallIntroEntrance(Rebuilt, Data)
        Verify(Rebuilt)
        assert LightIntensities(Rebuilt) == ExpectedIntensities
    print("PASS full map, escape route and intro regeneration preserve flicker scripts and all reduced light intensities")
    with Wave.open(str(Root / "Assets/Sounds/CeilingLightFlicker.wav"), "rb") as Sound:
        assert Sound.getnchannels() == 1 and Sound.getsampwidth() == 2 and Sound.getframerate() == 44100
        assert abs(Sound.getnframes() / Sound.getframerate() - 0.16) < 0.001
    print("PASS short electrical contact sound is valid mono 16-bit PCM")


if __name__ == "__main__":
    Main()
