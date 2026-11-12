import argparse as Argparse
import json as Json
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

import BuildCampusMap as Campus


def NewMesh(Name):
    return {"Name": Name, "Lines": ["mtllib EscapeRoute.mtl", "o " + Name], "Count": 0}


def AddDoorPaintPanel(Mesh, Center, Size):
    X, Y, Z = Center
    HalfX, HalfY, HalfZ = [Value * 0.5 for Value in Size]
    Points = [(X - HalfX, Y - HalfY, Z - HalfZ), (X + HalfX, Y - HalfY, Z - HalfZ), (X + HalfX, Y + HalfY, Z - HalfZ), (X - HalfX, Y + HalfY, Z - HalfZ), (X - HalfX, Y - HalfY, Z + HalfZ), (X + HalfX, Y - HalfY, Z + HalfZ), (X + HalfX, Y + HalfY, Z + HalfZ), (X - HalfX, Y + HalfY, Z + HalfZ)]
    for Indices, Normal in [([0, 3, 2, 1], (0, 0, -1)), ([4, 5, 6, 7], (0, 0, 1)), ([0, 4, 7, 3], (-1, 0, 0)), ([1, 2, 6, 5], (1, 0, 0)), ([0, 1, 5, 4], (0, -1, 0)), ([3, 7, 6, 2], (0, 1, 0))]:
        Face = [Points[Index] for Index in Indices]
        Coordinates = [(Point[1] / 1.8, Point[2] / 1.8) if Normal[0] != 0 else (Point[0] / 1.8, Point[2] / 1.8) if Normal[1] != 0 else (Point[0] / 1.8, Point[1] / 1.8) for Point in reversed(Face)]
        Campus.AddFace(Mesh, Face, Normal, "DoorPaint", Coordinates)


def SaveMesh(Root, Mesh):
    Target = Root / "Assets/Models/EscapeRoute" / (Mesh["Name"] + ".obj")
    Target.write_text("\n".join(Mesh["Lines"]) + "\n", encoding="utf-8")
    Target.with_suffix(".obj.bin").unlink(missing_ok=True)


def BuildSign(Root, Name, Heading, Detail):
    Canvas = Image.new("RGB", (1024, 256), (12, 94, 66))
    Draw = ImageDraw.Draw(Canvas)
    Font = "C:/Windows/Fonts/bahnschrift.ttf"
    Draw.rounded_rectangle((8, 8, 1015, 247), radius=14, outline=(222, 248, 223), width=7)
    Draw.line((112, 206, 112, 54), fill=(238, 255, 235), width=19)
    Draw.line((54, 112, 112, 54, 170, 112), fill=(238, 255, 235), width=19)
    Draw.text((225, 20), Heading, font=ImageFont.truetype(Font, 96), fill=(238, 255, 235))
    Draw.text((230, 159), Detail, font=ImageFont.truetype(Font, 31), fill=(212, 235, 219))
    Canvas.save(Root / "Assets/Textures/EscapeRoute" / (Name + ".png"))
    Mesh = NewMesh(Name)
    Campus.AddBox(Mesh, (0, 0, 0), (0.12, 2.8, 0.7), "Dark")
    Campus.AddFace(Mesh, [(-0.065, -1.36, -0.31), (-0.065, -1.36, 0.31), (-0.065, 1.36, 0.31), (-0.065, 1.36, -0.31)], (-1, 0, 0), Name, [(1, 0), (1, 1), (0, 1), (0, 0)])
    SaveMesh(Root, Mesh)


def BuildExteriorNotice(Root):
    Canvas = Image.new("RGB", (1024, 320), (215, 220, 205))
    Draw = ImageDraw.Draw(Canvas)
    Font = "C:/Windows/Fonts/bahnschrift.ttf"
    Draw.rectangle((10, 10, 1013, 309), outline=(27, 42, 39), width=10)
    Draw.rectangle((22, 22, 1001, 113), fill=(214, 166, 52))
    Draw.text((512, 69), "EMERGENCY EXIT", anchor="mm", font=ImageFont.truetype(Font, 78), fill=(24, 35, 31))
    Draw.text((512, 174), "KEEP CLEAR", anchor="mm", font=ImageFont.truetype(Font, 79), fill=(28, 43, 38))
    Draw.text((512, 268), "NO RE-ENTRY / AUTHORIZED ACCESS ONLY", anchor="mm", font=ImageFont.truetype(Font, 33), fill=(53, 65, 57))
    Canvas.save(Root / "Assets/Textures/EscapeRoute/ExteriorNotice.png")


def BuildMeshes(Root):
    (Root / "Assets/Models/EscapeRoute").mkdir(parents=True, exist_ok=True)
    (Root / "Assets/Textures/EscapeRoute").mkdir(parents=True, exist_ok=True)
    Materials = []
    for Name, Color in [("EscapeGreen", (0.06, 0.4, 0.27)), ("DoorPaint", (1.0, 1.0, 1.0)), ("FrameSteel", (0.24, 0.31, 0.29)), ("Dark", (0.035, 0.055, 0.05)), ("Rim", (0.62, 0.68, 0.65)), ("White", (0.86, 0.94, 0.85)), ("Glass", (0.15, 0.32, 0.34)), ("Screen", (0.22, 0.94, 0.45))]:
        Materials.extend(["newmtl " + Name, "Kd " + " ".join(map(str, Color)), "Ks 0.1 0.1 0.1", "Ns 24", "d 1.0"])
        if Name == "DoorPaint":
            Materials.append("map_Kd ../../Textures/EscapeRoute/EmergencyDoorPaint_BaseColor.png")
    for Name in ["ExitSign", "RouteSign", "ExteriorNotice"]:
        Materials.extend(["newmtl " + Name, "Kd 1 1 1", "d 1.0", "map_Kd ../../Textures/EscapeRoute/" + Name + ".png"])
    (Root / "Assets/Models/EscapeRoute/EscapeRoute.mtl").write_text("\n".join(Materials) + "\n", encoding="utf-8")
    BuildSign(Root, "ExitSign", "EXIT", "EMERGENCY EXIT  /  CARD ACCESS")
    BuildSign(Root, "RouteSign", "EXIT  24m", "FOLLOW THE GREEN LINE")
    BuildExteriorNotice(Root)
    Mesh = NewMesh("EmergencyExitDoor")
    AddDoorPaintPanel(Mesh, (0, 0, 0), (0.12, 1.8, 2.96))
    for Y in [-0.87, 0.87]:
        Campus.AddBox(Mesh, (-0.067, Y, 0), (0.025, 0.05, 2.9), "Rim")
    Campus.AddBox(Mesh, (-0.075, 0, -1.17), (0.035, 1.7, 0.44), "Rim")
    Campus.AddBox(Mesh, (-0.076, 0, 0.73), (0.04, 0.55, 0.98), "Rim")
    Campus.AddBox(Mesh, (-0.101, 0, 0.73), (0.018, 0.43, 0.86), "Glass")
    for Z in [0.43, 0.73, 1.03]:
        Campus.AddBox(Mesh, (-0.12, 0, Z), (0.02, 0.43, 0.025), "Dark")
    for Y in [-0.64, 0.64]:
        Campus.AddBox(Mesh, (-0.13, Y, -0.34), (0.15, 0.16, 0.18), "Dark")
    Campus.AddBox(Mesh, (-0.22, 0, -0.34), (0.12, 1.48, 0.12), "Rim")
    Campus.AddBox(Mesh, (-0.105, 0, -0.65), (0.05, 0.8, 0.15), "White")
    for Y in [-0.28, 0, 0.28]:
        Campus.AddBox(Mesh, (-0.135, Y, -0.65), (0.025, 0.1, 0.04), "EscapeGreen")
    for Z in [-1.15, 0, 1.15]:
        Campus.AddRod(Mesh, (0, -0.92, Z - 0.1), (0, -0.92, Z + 0.1), 0.055, "Rim", 10)
    Campus.AddBox(Mesh, (-0.13, -0.42, 1.29), (0.12, 0.65, 0.19), "Rim")
    Campus.AddRod(Mesh, (-0.2, -0.42, 1.36), (-0.3, 0.2, 1.44), 0.025, "Dark", 8)
    AddDoorPaintPanel(Mesh, (0.078, 0, 0), (0.024, 1.64, 2.76))
    for Y in [-0.78, 0.78]:
        Campus.AddBox(Mesh, (0.115, Y, 0), (0.034, 0.07, 2.72), "Rim")
        for Z in [-1.25, -0.7, 0.25, 1.25]:
            Campus.AddRod(Mesh, (0.137, Y, Z), (0.155, Y, Z), 0.022, "Dark", 6)
    for Z in [-0.91, 1.3]:
        Campus.AddBox(Mesh, (0.115, 0, Z), (0.034, 1.49, 0.08), "Rim")
    Campus.AddBox(Mesh, (0.12, 0, -1.2), (0.035, 1.48, 0.4), "FrameSteel")
    for Y in [-0.62, 0.62]:
        Campus.AddRod(Mesh, (0.142, Y, -1.2), (0.16, Y, -1.2), 0.022, "Rim", 6)
    Campus.AddBox(Mesh, (0.122, 0, 0.73), (0.04, 0.57, 1.0), "Rim")
    Campus.AddBox(Mesh, (0.152, 0, 0.73), (0.018, 0.43, 0.86), "Glass")
    for Z in [0.43, 0.73, 1.03]:
        Campus.AddBox(Mesh, (0.18, 0, Z), (0.026, 0.43, 0.026), "Dark")
    Campus.AddBox(Mesh, (0.12, 0.61, -0.2), (0.04, 0.16, 0.6), "FrameSteel")
    for Z in [-0.42, 0.02]:
        Campus.AddRod(Mesh, (0.15, 0.61, Z), (0.27, 0.61, Z), 0.025, "Rim", 10)
    Campus.AddRod(Mesh, (0.27, 0.61, -0.42), (0.27, 0.61, 0.02), 0.035, "Rim", 12)
    Campus.AddBox(Mesh, (0.15, 0, -0.68), (0.035, 1.25, 0.39), "Dark")
    Campus.AddFace(Mesh, [(0.174, -0.59, -0.85), (0.174, 0.59, -0.85), (0.174, 0.59, -0.51), (0.174, -0.59, -0.51)], (1, 0, 0), "ExteriorNotice", [(1, 1), (0, 1), (0, 0), (1, 0)])
    SaveMesh(Root, Mesh)
    Mesh = NewMesh("EmergencyExitFrame")
    for Y in [-1.46, 1.46]:
        Campus.AddBox(Mesh, (0, Y, 1.5), (0.3, 1.08, 3), "FrameSteel")
    Campus.AddBox(Mesh, (0, 0, 3.5), (0.3, 4, 1), "FrameSteel")
    for Facing in [-1, 1]:
        for Y in [-0.97, 0.97]:
            Campus.AddBox(Mesh, (Facing * 0.20, Y, 1.5), (0.06, 0.10, 3.0), "Rim")
            for Z in [0.35, 1.5, 2.65]:
                Campus.AddRod(Mesh, (Facing * 0.237, Y, Z), (Facing * 0.255, Y, Z), 0.02, "Dark", 6)
        Campus.AddBox(Mesh, (Facing * 0.20, 0, 3.05), (0.06, 2.04, 0.1), "Rim")
        for Y in [-2.0, 2.0]:
            Campus.AddBox(Mesh, (Facing * 0.29, Y, 1.97), (0.10, 0.14, 3.94), "Dark")
        Campus.AddBox(Mesh, (Facing * 0.29, 0, 4.01), (0.10, 4.14, 0.14), "Dark")
    Campus.AddBox(Mesh, (-0.22, 1.25, 1.3), (0.16, 0.22, 0.4), "Dark")
    Campus.AddBox(Mesh, (-0.309, 1.25, 1.4), (0.02, 0.14, 0.1), "Screen")
    Campus.AddBox(Mesh, (-0.309, 1.25, 1.21), (0.02, 0.14, 0.035), "Rim")
    SaveMesh(Root, Mesh)
    Mesh = NewMesh("CorridorRib")
    for Y in [-1.93, 1.93]:
        Campus.AddBox(Mesh, (0, Y, 2), (0.2, 0.12, 4), "Dark")
        Campus.AddBox(Mesh, (-0.11, Y, 0.55), (0.025, 0.13, 0.7), "Rim")
    Campus.AddBox(Mesh, (0, 0, 3.91), (0.2, 4, 0.18), "Dark")
    SaveMesh(Root, Mesh)
    Mesh = NewMesh("FloorArrow")
    Campus.AddBox(Mesh, (-0.05, 0, 0), (0.7, 0.4, 0.01), "White")
    for Direction in [-1, 1]:
        Points = [(0.3, -0.67, 0), (0.95, 0, 0), (0.3, 0.67, 0)]
        if Direction < 0:
            Points = list(reversed(Points))
        for Index in range(1, len(Points) - 1):
            Campus.AddFace(Mesh, [Points[0], Points[Index], Points[Index + 1]], (0, 0, Direction), "White", [(0, 0), (1, 0), (1, 1)])
    SaveMesh(Root, Mesh)

    from BuildDoorExterior import BuildMeshes as BuildExteriorMeshes
    BuildExteriorMeshes(Root)


def MeshActor(Name, Mesh, Location, **Options):
    return Campus.MeshActor("Escape" + Name, "Models/EscapeRoute/" + Mesh + ".obj", Location, **Options)


def CubeActor(Name, Location, Size, Surface, Blocking=False):
    return Campus.CubeActor("Escape" + Name, Location, Size, Surface, Blocking)


def AddPointLight(Actor, Color, Intensity, Radius):
    Actor["Components"].append({"Class": "UPointLightComponent", "Name": Actor["RootComponent"] + "Light", "Parent": Actor["RootComponent"], "Properties": {"Transform": Campus.Transform(), "LightColor": Color, "Intensity": Intensity, "AttenuationRadius": Radius, "LightFalloffExponent": 2, "SourceRadius": 0.15, "ShadowMapResolution": 256, "bCastShadows": False, "bVisible": True}})


def ReplaceActors(Actors, Removed):
    Mapping = {}
    Retained = []
    for Index, Actor in enumerate(Actors):
        if not Removed(Actor):
            Mapping[Index] = len(Retained)
            Retained.append(Actor)
    for Actor in Retained:
        if "AttachParent" in Actor:
            Actor["AttachParent"]["Actor"] = Mapping[Actor["AttachParent"]["Actor"]]
    Actors[:] = Retained


def InstallDoorPaintMaterial(Component):
    Root = Path(__file__).resolve().parents[1]
    MaterialNames = ["Default"] + [Line.split(maxsplit=1)[1] for Line in (Root / "Assets/Models/EscapeRoute/EscapeRoute.mtl").read_text(encoding="utf-8").splitlines() if Line.startswith("newmtl ")]
    Slot = MaterialNames.index("DoorPaint")
    Overrides = Component.setdefault("OverrideMaterials", [])
    while len(Overrides) <= Slot:
        Overrides.append({})
    Material = Campus.Material((1, 1, 1), "Assets/Textures/EscapeRoute/EmergencyDoorPaint_BaseColor.png", Tile=0)
    Material["Instance"]["Shininess"] = 32
    Overrides[Slot] = Material


def InstallDoor(Actor, Location):
    Campus.RootComponent(Actor)["Properties"]["Transform"] = Campus.Transform(Location)
    for Component in Actor["Components"]:
        if Component["Class"] == "UStaticMeshComponent":
            Component["Properties"]["StaticMesh"] = "Models/EscapeRoute/EmergencyExitDoor.obj"
            Component["Properties"]["Transform"] = Campus.Transform((0, 0.92, 0))
            InstallDoorPaintMaterial(Component)


def InstallFrame(Actor, Location):
    Campus.RootComponent(Actor)["Properties"]["Transform"] = Campus.Transform(Location)
    for Component in Actor["Components"]:
        if Component["Class"] == "UStaticMeshComponent":
            Component["Properties"]["StaticMesh"] = "Models/EscapeRoute/EmergencyExitFrame.obj"
            Component["OverrideMaterials"] = []
        elif Component["Class"] == "UBoxComponent":
            Name = Component["Name"]
            Component["Properties"]["BoxExtent"] = Campus.Vector(0.15, 2 if Name.endswith("Header") else 0.54, 0.5 if Name.endswith("Header") else 1.5)
            Component["Properties"]["Transform"] = Campus.Transform((0, 0 if Name.endswith("Header") else -1.46 if Name.endswith("Left") else 1.46, 3.5 if Name.endswith("Header") else 1.5))


def InstallScene(Root, Data):
    Target = Root / "Assets/Scenes/Demo/06_Game.scene"
    Scene = Json.loads(Target.read_text(encoding="utf-8-sig"))
    Actors = Scene["World"]["PersistentLevel"]["Actors"]
    ReplaceActors(Actors, lambda Actor: Actor["RootComponent"].startswith("Campus_Escape") or Actor["RootComponent"].startswith("Campus_Wall_") and Campus.RootComponent(Actor)["Properties"]["Transform"]["Location"]["X"] > 0)
    Door = Data["Landmarks"]["DoorExit"]["Location"]
    CenterY = Door["Y"] + 0.92
    EndX = Door["X"]
    PreviousDoor = next(Actor for Actor in Actors if Actor["RootComponent"] == "DoorExit")
    DeltaX = EndX - Campus.RootComponent(PreviousDoor)["Properties"]["Transform"]["Location"]["X"]
    for Actor in Actors:
        Name = Actor["RootComponent"]
        if Name == "DoorExit":
            InstallDoor(Actor, (EndX, Door["Y"], Door["Z"]))
        elif Name == "DoorExitFrame":
            InstallFrame(Actor, (EndX, CenterY, 0))
        elif Name.startswith("EndingWalkway") or Name.startswith("EndingExteriorLight") or Name == "EndingExteriorGround":
            Transform = Campus.RootComponent(Actor)["Properties"]["Transform"]
            Transform["Location"]["X"] += DeltaX
        elif any(Component["Properties"].get("ScriptPath", "").endswith("/MapActor.lua") for Component in Actor["Components"]):
            Campus.RootComponent(Actor)["Properties"]["Transform"]["Location"] = Data["Origin"]
    Wall = Campus.Material((0.64, 0.72, 0.68), "Assets/Textures/Campus/Concrete_BaseColor.png", "Assets/Textures/Campus/Concrete_Normal.png", 3)
    Floor = Campus.Material((0.6, 0.65, 0.62), "Assets/Textures/Facility/TreadPlate_BaseColor.png", "Assets/Textures/Facility/TreadPlate_Normal.png", 2)
    Dark = Campus.Material((0.08, 0.12, 0.11))
    Green = Campus.Material((0.09, 0.56, 0.31))
    Actors.append(CubeActor("CorridorFoundation", (12, CenterY, -0.5), (25, 6, 1), Floor))
    Actors.append(CubeActor("CorridorRoof", (12.5, CenterY, 4.1), (24, 6, 0.2), Wall))
    for Label, Y in [("Left", CenterY - 2.5), ("Right", CenterY + 2.5)]:
        Actors.append(CubeActor("CorridorWall" + Label, (12.5, Y, 2), (24, 1, 4), Wall, True))
        Actors.append(CubeActor("GuideLine" + Label, (12, CenterY + (-1.9 if Label == "Left" else 1.9), 0.018), (23, 0.12, 0.025), Green))
        Actors.append(CubeActor("WallStripe" + Label, (12, CenterY + (-1.99 if Label == "Left" else 1.99), 0.65), (23, 0.02, 0.13), Green))
        Actors.append(CubeActor("WallBase" + Label, (12, CenterY + (-1.97 if Label == "Left" else 1.97), 0.16), (23, 0.06, 0.3), Dark))
    for Index, X in enumerate([2, 8, 14, 20]):
        Actors.append(MeshActor("CorridorRib" + str(Index), "CorridorRib", (X, CenterY, 0)))
        Fixture = Campus.MeshActor("EscapeCorridorLamp" + str(Index), "Models/Facility/CeilingLight.obj", (X + 1, CenterY, 3.75), Scale=(0.75, 0.75, 0.75))
        AddPointLight(Fixture, 4291624908, 0.72, 7)
        Actors.append(Fixture)
    for Index, X in enumerate([3, 7, 11, 15, 19]):
        Actors.append(MeshActor("FloorArrow" + str(Index), "FloorArrow", (X, CenterY, 0.03)))
    Actors.append(MeshActor("RouteSign", "RouteSign", (-1.6, CenterY, 3.35)))
    Sign = MeshActor("ExitSign", "ExitSign", (EndX - 0.24, CenterY, 3.5))
    AddPointLight(Sign, 4286095242, 0.48, 5)
    Actors.append(Sign)
    from BuildDoorExterior import InstallExterior
    InstallExterior(Actors, (EndX, CenterY, 0), IntensityScale=0.4)
    Campus.InstallCeilingLightFlicker(Actors)
    Scene["World"]["PersistentLevel"]["Actors"] = Actors
    Target.write_text(Json.dumps(Scene, ensure_ascii=False, indent=4) + "\n", encoding="utf-8")


def UpdateEscapeRoute(Root):
    Data = Campus.LoadMap(Root)
    BuildMeshes(Root)
    InstallScene(Root, Data)
    print("Updated escape route: 24m enclosed corridor, green emergency door, push bar, card reader and EXIT signs")


def Main():
    Parser = Argparse.ArgumentParser()
    Parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parents[1])
    Arguments = Parser.parse_args()
    UpdateEscapeRoute(Arguments.root.resolve())


if __name__ == "__main__":
    Main()
