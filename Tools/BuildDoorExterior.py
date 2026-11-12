import argparse as Argparse
import json as Json
import math as Math
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

import BuildCampusMap as Campus


def NewMesh(Name):
    return {"Name": Name, "Lines": ["mtllib DoorExterior.mtl", "o " + Name], "Count": 0}


def SaveMesh(Root, Mesh):
    Target = Root / "Assets/Models/DoorExterior" / (Mesh["Name"] + ".obj")
    Target.write_text("\n".join(Mesh["Lines"]) + "\n", encoding="utf-8")
    Target.with_suffix(".obj.bin").unlink(missing_ok=True)


def BuildSigns(Root):
    Font = "C:/Windows/Fonts/bahnschrift.ttf"
    Canvas = Image.new("RGB", (1024, 768), (15, 92, 69))
    Draw = ImageDraw.Draw(Canvas)
    Draw.rectangle((18, 18, 1005, 749), outline=(228, 243, 221), width=12)
    for X, Y, FlipX, FlipY in [(145, 125, 1, 1), (879, 125, -1, 1), (145, 485, 1, -1), (879, 485, -1, -1)]:
        Draw.line((X, Y, X + FlipX * 110, Y + FlipY * 110), fill=(238, 249, 232), width=26)
        Draw.polygon([(X + FlipX * 125, Y + FlipY * 125), (X + FlipX * 30, Y + FlipY * 125), (X + FlipX * 125, Y + FlipY * 30)], fill=(238, 249, 232))
    for X, Y in [(420, 270), (604, 270), (512, 365)]:
        Draw.ellipse((X - 32, Y - 55, X + 32, Y + 9), fill=(238, 249, 232))
        Draw.rounded_rectangle((X - 49, Y + 20, X + 49, Y + 135), radius=26, fill=(238, 249, 232))
    Draw.text((512, 600), "ASSEMBLY POINT", anchor="mm", font=ImageFont.truetype(Font, 67), fill=(238, 249, 232))
    Draw.text((512, 690), "SAFE AREA  /  04", anchor="mm", font=ImageFont.truetype(Font, 40), fill=(204, 227, 210))
    Canvas.save(Root / "Assets/Textures/DoorExterior/AssemblyPoint.png")
    Canvas = Image.new("RGB", (1536, 256), (29, 47, 49))
    Draw = ImageDraw.Draw(Canvas)
    Draw.rectangle((10, 10, 1525, 245), outline=(198, 184, 126), width=7)
    Draw.text((55, 28), "04", font=ImageFont.truetype(Font, 164), fill=(222, 194, 119))
    Draw.line((302, 35, 302, 222), fill=(198, 184, 126), width=5)
    Draw.text((354, 24), "EMERGENCY ACCESS", font=ImageFont.truetype(Font, 91), fill=(222, 235, 224))
    Draw.text((360, 151), "SERVICE COURT  /  KEEP CLEAR", font=ImageFont.truetype(Font, 42), fill=(170, 198, 187))
    Canvas.save(Root / "Assets/Textures/DoorExterior/AccessSign.png")


def BuildMeshes(Root):
    Folder = Root / "Assets/Models/DoorExterior"
    Folder.mkdir(parents=True, exist_ok=True)
    (Root / "Assets/Textures/DoorExterior").mkdir(parents=True, exist_ok=True)
    Materials = []
    for Name, Color, Texture in [("Concrete", (0.72, 0.77, 0.73), "Campus/Concrete"), ("Steel", (0.48, 0.6, 0.6), "Facility/RustedSteel"), ("Dark", (0.055, 0.08, 0.085), None), ("Warning", (0.88, 0.61, 0.19), None), ("Ivory", (0.78, 0.83, 0.75), None), ("Lens", (1.0, 0.83, 0.48), None), ("Blue", (0.1, 0.2, 0.24), None)]:
        Materials.extend(["newmtl " + Name, "Kd " + " ".join(map(str, Color)), "Ks 0.12 0.12 0.12", "Ns 28", "d 1.0"])
        if Texture is not None:
            Materials.extend(["map_Kd ../../Textures/" + Texture + "_BaseColor.png", "map_bump ../../Textures/" + Texture + "_Normal.png"])
    for Name in ["AssemblyPoint", "AccessSign"]:
        Materials.extend(["newmtl " + Name, "Kd 1 1 1", "d 1.0", "map_Kd ../../Textures/DoorExterior/" + Name + ".png"])
    (Folder / "DoorExterior.mtl").write_text("\n".join(Materials) + "\n", encoding="utf-8")
    BuildSigns(Root)
    Mesh = NewMesh("ExitFacade")
    for Side in [-1, 1]:
        for Index in range(4):
            InnerY = 2.02 if Index == 0 else 1.05 + Index * 2.7
            OuterY = 3.75 + Index * 2.7
            Width = OuterY - InnerY
            Y = Side * (InnerY + OuterY) * 0.5
            Campus.AddBox(Mesh, (0, Y, 2.7), (0.42, Width, 5.4), "Concrete")
            Campus.AddBox(Mesh, (0.26, Y, 0.28), (0.12, Width - 0.06, 0.56), "Dark")
            Campus.AddBox(Mesh, (0.25, Y, 3.95), (0.09, Width - 0.06, 0.14), "Steel")
            Campus.AddBox(Mesh, (0.27, Side * (OuterY - 0.07), 2.65), (0.16, 0.13, 5.3), "Steel")
        for Y in [Side * 3.1, Side * 8.5]:
            Campus.AddBox(Mesh, (0.3, Y, 2.05), (0.3, 1.5, 1.25), "Dark")
            for Index in range(9):
                Campus.AddBox(Mesh, (0.48, Y, 1.54 + Index * 0.13), (0.1, 1.43, 0.055), "Steel")
    Campus.AddBox(Mesh, (0, 0, 4.7), (0.42, 2.1, 1.4), "Concrete")
    Campus.AddBox(Mesh, (0.32, 0, 5.37), (0.4, 23.8, 0.16), "Dark")
    for Y, Radius in [(-4.7, 0.11), (-5.15, 0.16), (4.7, 0.12)]:
        Campus.AddRod(Mesh, (0.55, Y, 0.1), (0.55, Y, 4.7), Radius, "Steel", 12)
        Campus.AddRod(Mesh, (0.55, Y, 4.7), (0.55, Y + 1.5, 4.7), Radius, "Steel", 12)
        for Z in [0.8, 2.4, 4.0]:
            Campus.AddRod(Mesh, (0.55, Y, Z - 0.06), (0.55, Y, Z + 0.06), Radius + 0.035, "Dark", 12)
    SaveMesh(Root, Mesh)
    Mesh = NewMesh("ExitCanopy")
    Campus.AddBox(Mesh, (1.45, 0, 3.72), (3.0, 5.0, 0.2), "Steel")
    for Y in [-2.35, 2.35]:
        Campus.AddBox(Mesh, (1.45, Y, 3.58), (3.0, 0.16, 0.2), "Dark")
        Campus.AddBox(Mesh, (2.65, Y, 1.74), (0.15, 0.15, 3.48), "Steel")
        Campus.AddBox(Mesh, (2.65, Y, 0.32), (0.2, 0.2, 0.64), "Warning")
        Campus.AddRod(Mesh, (2.65, Y, 2.8), (1.9, Y, 3.55), 0.045, "Dark", 8)
    for X in [0.25, 0.85, 1.45, 2.05, 2.65]:
        Campus.AddBox(Mesh, (X, 0, 3.83), (0.08, 4.9, 0.05), "Dark")
    Campus.AddBox(Mesh, (2.98, 0, 3.65), (0.09, 4.65, 0.65), "Dark")
    Campus.AddFace(Mesh, [(3.03, -2.2, 3.36), (3.03, 2.2, 3.36), (3.03, 2.2, 3.94), (3.03, -2.2, 3.94)], (1, 0, 0), "AccessSign", [(1, 1), (0, 1), (0, 0), (1, 0)])
    SaveMesh(Root, Mesh)
    Mesh = NewMesh("BulkheadLamp")
    Campus.AddBox(Mesh, (0.06, 0, 0), (0.12, 0.68, 0.3), "Dark")
    Campus.AddBox(Mesh, (0.15, 0, 0), (0.12, 0.57, 0.21), "Lens")
    for Y in [-0.24, -0.08, 0.08, 0.24]:
        Campus.AddRod(Mesh, (0.22, Y, -0.14), (0.22, Y, 0.14), 0.018, "Steel", 6)
    for Z in [-0.14, 0.14]:
        Campus.AddRod(Mesh, (0.22, -0.32, Z), (0.22, 0.32, Z), 0.018, "Steel", 6)
    SaveMesh(Root, Mesh)
    Mesh = NewMesh("AssemblyStation")
    for Y in [-0.72, 0.72]:
        Campus.AddBox(Mesh, (0, Y, 1.52), (0.1, 0.1, 3.04), "Steel")
        Campus.AddBox(Mesh, (0, Y, 0.06), (0.42, 0.42, 0.12), "Concrete")
    Campus.AddBox(Mesh, (0, 0, 2.35), (0.16, 2.1, 1.58), "Dark")
    Campus.AddFace(Mesh, [(-0.085, -1.0, 1.6), (-0.085, -1.0, 3.1), (-0.085, 1.0, 3.1), (-0.085, 1.0, 1.6)], (-1, 0, 0), "AssemblyPoint", [(1, 0), (1, 1), (0, 1), (0, 0)])
    SaveMesh(Root, Mesh)
    Mesh = NewMesh("DrainChannel")
    Campus.AddBox(Mesh, (0, 0, -0.012), (12.5, 0.3, 0.035), "Dark")
    for Index in range(84):
        Campus.AddBox(Mesh, (-6.15 + Index * 0.148, 0, 0.012), (0.055, 0.29, 0.025), "Steel")
    SaveMesh(Root, Mesh)
    Mesh = NewMesh("ServiceBuilding")
    Campus.AddBox(Mesh, (0, 0, 2.8), (10, 8, 5.6), "Concrete")
    Campus.AddBox(Mesh, (0, 0, 5.65), (10.5, 8.5, 0.22), "Dark")
    Campus.AddBox(Mesh, (-5.08, -0.6, 1.8), (0.12, 4.2, 3.6), "Blue")
    for Index in range(18):
        Campus.AddBox(Mesh, (-5.17, -0.6, 0.15 + Index * 0.2), (0.06, 4.15, 0.035), "Steel")
    for Y in [-3.7, 3.7]:
        Campus.AddBox(Mesh, (-5.1, Y, 2.8), (0.22, 0.16, 5.6), "Steel")
    for Y in [2.4, -2.8]:
        Campus.AddBox(Mesh, (0, Y, 6.2), (3.6, 1.5, 1.0), "Steel")
        for Index in range(12):
            Campus.AddBox(Mesh, (-1.84, Y - 0.65 + Index * 0.118, 6.2), (0.06, 0.04, 0.85), "Dark")
    SaveMesh(Root, Mesh)
    Mesh = NewMesh("CoolingPlant")
    Campus.AddBox(Mesh, (0, 0, 2.4), (10, 6, 4.8), "Steel")
    Campus.AddBox(Mesh, (0, 0, 4.95), (10.4, 6.4, 0.3), "Dark")
    for X, Height in [(-2.3, 11.0), (1.9, 9.3)]:
        Campus.AddRod(Mesh, (X, 0, 4.9), (X, 0, Height), 0.65, "Steel", 18)
        for Z in [5.1, 6.5, Height - 0.2]:
            Campus.AddRod(Mesh, (X, 0, Z - 0.09), (X, 0, Z + 0.09), 0.73, "Dark", 18)
        Campus.AddRod(Mesh, (X, 0, Height - 0.8), (X, 0, Height - 0.4), 0.66, "Ivory", 18)
    SaveMesh(Root, Mesh)


def InstallExterior(Actors, Origin, Yaw=0.0, IntensityScale=1.0):
    Mapping = {}
    Retained = []
    for Index, Actor in enumerate(Actors):
        Name = Actor["RootComponent"]
        Removed = Name.startswith(("Campus_DoorExterior_", "EndingWalkway", "EndingExteriorLight", "ExteriorBarrier", "WalkwayCurb")) or Name in ["EndingExteriorGround", "ExteriorGround", "Walkway", "ExteriorWallLeft", "ExteriorWallRight", "ExteriorHeader"]
        if not Removed:
            Mapping[Index] = len(Retained)
            Retained.append(Actor)
    for Actor in Retained:
        if "AttachParent" in Actor:
            Actor["AttachParent"]["Actor"] = Mapping[Actor["AttachParent"]["Actor"]]
    Actors[:] = Retained
    Cosine, Sine = Math.cos(Math.radians(Yaw)), Math.sin(Math.radians(Yaw))

    def Position(Location):
        X, Y, Z = Location
        return (Origin[0] + X * Cosine - Y * Sine, Origin[1] + X * Sine + Y * Cosine, Origin[2] + Z)

    def Add(Name, Mesh, Location, Scale=(1, 1, 1), Rotation=0, Surface=None):
        Actor = Campus.MeshActor("DoorExterior_" + Name, Mesh, Position(Location), Scale=Scale, Yaw=Yaw + Rotation, Surface=Surface)
        Actors.append(Actor)
        return Actor

    def Cube(Name, Location, Size, Surface):
        return Add(Name, "Cube", Location, Size, Surface=Surface)

    def Light(Actor, Color, Intensity, Radius, Shadows=False):
        Actor["Components"].append({"Class": "UPointLightComponent", "Name": Actor["RootComponent"] + "Light", "Parent": Actor["RootComponent"], "Properties": {"Transform": Campus.Transform((0.45, 0, -0.1)), "LightColor": Color, "Intensity": round(Intensity * IntensityScale, 6), "AttenuationRadius": Radius, "LightFalloffExponent": 2, "SourceRadius": 0.18, "ShadowMapResolution": 512, "ShadowBias": 0.001, "bCastShadows": Shadows, "bVisible": True}})

    Concrete = Campus.Material((0.68, 0.73, 0.69), "Assets/Textures/Campus/Concrete_BaseColor.png", "Assets/Textures/Campus/Concrete_Normal.png", 2)
    Asphalt = Campus.Material((0.65, 0.72, 0.73), "Assets/Textures/Campus/Asphalt_BaseColor.png", "Assets/Textures/Campus/Asphalt_Normal.png", 4)
    Ground = Campus.Material((0.3, 0.38, 0.32), "Assets/Textures/StoneGround/StoneGround_BaseColor.png", "Assets/Textures/StoneGround/StoneGround_Normal.png", 5)
    Dark = Campus.Material((0.07, 0.09, 0.09))
    Warning = Campus.Material((0.8, 0.57, 0.17))
    Ivory = Campus.Material((0.68, 0.73, 0.65))
    Cube("Ground", (26, 0, -0.36), (80, 64, 0.6), Ground)
    Cube("ServiceApron", (13.5, 0, -0.035), (27, 17, 0.04), Asphalt)
    Cube("AccessRoad", (39, 0, -0.03), (27, 5.5, 0.05), Asphalt)
    Cube("Walkway", (12, 0, -0.005), (24, 3.4, 0.05), Concrete)
    for Index in range(12):
        Cube("PavementJoint" + str(Index), (1.0 + Index * 2, 0, 0.022), (0.025, 3.4, 0.009), Dark)
    for Side in [-1, 1]:
        Cube("Curb" + str(Side), (12, Side * 1.85, 0.05), (24, 0.22, 0.16), Concrete)
        Add("Drain" + str(Side), "Models/DoorExterior/DrainChannel.obj", (7, Side * 2.16, 0))
        for Index in range(5):
            Add("FenceSide" + str(Side) + str(Index), "Models/Campus/FencePanel.obj", (4 + Index * 4, Side * 8.0, 0), Rotation=90)
        Add("FenceFront" + str(Side), "Models/Campus/FencePanel.obj", (24, Side * 5.8, 0), Scale=(1, 1.1, 1))
        Add("OpenGate" + str(Side), "Models/Campus/FencePanel.obj", (25.8, Side * 3.05, 0), Scale=(1, 0.9, 1), Rotation=Side * 72)
        for Index, X in enumerate([3.7, 11.5, 22.7]):
            Add("Bollard" + str(Side) + str(Index), "Models/Campus/Bollard.obj", (X, Side * 2.85, 0))
        for Index in range(6):
            Cube("ThresholdMark" + str(Side) + str(Index), (0.5 + Index * 0.22, Side * 1.42, 0.028), (0.12, 0.5, 0.015), Warning)
    Add("Facade", "Models/DoorExterior/ExitFacade.obj", (0, 0, 0))
    Add("Canopy", "Models/DoorExterior/ExitCanopy.obj", (0.15, 0, 0))
    for Side in [-1, 1]:
        Lamp = Add("DoorLamp" + str(Side), "Models/DoorExterior/BulkheadLamp.obj", (0.52, Side * 1.5, 2.95))
        Light(Lamp, 0xFFBCE6FF, 2.4, 9, True)
    for Name, Location, Rotation, Intensity in [("CourtLight", (10.5, -6.8, 0), -90, 4.0), ("GateLight", (22, 6.7, 0), 90, 3.5)]:
        Lamp = Add(Name, "Models/Campus/Floodlight.obj", Location, Rotation=Rotation)
        Lamp["Components"].append({"Class": "UPointLightComponent", "Name": Name + "Light", "Parent": Lamp["RootComponent"], "Properties": {"Transform": Campus.Transform((0, 0, 5.65)), "LightColor": 0xFFFFDBB8, "Intensity": round(Intensity * IntensityScale, 6), "AttenuationRadius": 20, "LightFalloffExponent": 2, "bCastShadows": False, "bVisible": True}})
    Station = Add("AssemblyStation", "Models/DoorExterior/AssemblyStation.obj", (17, -5.8, 0), Rotation=-12)
    Light(Station, 0xFFD5F4D1, 1.1, 5)
    Add("Workshop", "Models/DoorExterior/ServiceBuilding.obj", (32, 10.5, 0))
    Add("CoolingPlant", "Models/DoorExterior/CoolingPlant.obj", (40, -13.5, 0), Rotation=-8)
    for Name, Mesh, Location, Rotation in [("Generator", "GeneratorBank", (12, -5.8, 0), 90), ("Drums", "DrumPallet", (7.5, 5.7, 0), -18), ("Cargo", "CargoStack", (18.5, 5.5, 0), 8), ("Cable", "CableReel", (10.5, 5.2, 0), 15), ("PressureTank", "PressureTank", (21, -6.1, 0), 0)]:
        Add(Name, "Models/FacilityProps/" + Mesh + ".obj", Location, Rotation=Rotation)
    for Index in range(9):
        Cube("RoadDash" + str(Index), (27 + Index * 2.8, 0, 0.004), (1.4, 0.1, 0.012), Ivory)


def UpdateExterior(Root):
    BuildMeshes(Root)
    for Name, DoorName in [("Demo/06_Game.scene", "DoorExit")]:
        Target = Root / "Assets/Scenes" / Name
        Scene = Json.loads(Target.read_text(encoding="utf-8-sig"))
        Actors = Scene["World"]["PersistentLevel"]["Actors"]
        Door = next(Actor for Actor in Actors if Actor["RootComponent"] == DoorName)
        Transform = Campus.RootComponent(Door)["Properties"]["Transform"]
        Yaw = Transform["Rotation"]["Yaw"]
        Location = Transform["Location"]
        Origin = (Location["X"] - 0.92 * Math.sin(Math.radians(Yaw)), Location["Y"] + 0.92 * Math.cos(Math.radians(Yaw)), Location["Z"] - 1.5)
        InstallExterior(Actors, Origin, Yaw, IntensityScale=0.4)
        Target.write_text(Json.dumps(Scene, ensure_ascii=False, indent=4) + "\n", encoding="utf-8")
    print("Updated emergency exit exterior in the gameplay scene")


def Main():
    Parser = Argparse.ArgumentParser()
    Parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parents[1])
    Arguments = Parser.parse_args()
    UpdateExterior(Arguments.root.resolve())


if __name__ == "__main__":
    Main()
