import argparse as Argparse
import json as Json
import math as Math
import random as Random
import subprocess as Subprocess
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont


def LoadMap(Root):
    Lua = Root / "vcpkg_installed/x64-windows/tools/luajit/luajit.exe"
    Export = r'''
package.path = "Assets/Scripts/GameLogic/?.lua;" .. package.path
local function Encode(Value)
    if type(Value) == "table" then
        local Parts = {}
        local Count = 0
        local Array = true
        for Key in pairs(Value) do
            Count = Count + 1
            if type(Key) ~= "number" or Key < 1 or Key ~= math.floor(Key) then
                Array = false
            end
        end
        if Array and Count > 0 and Count == #Value then
            for Index = 1, Count do
                Parts[#Parts + 1] = Encode(Value[Index])
            end
            return "[" .. table.concat(Parts, ",") .. "]"
        end
        for Key, Entry in pairs(Value) do
            Parts[#Parts + 1] = Encode(tostring(Key)) .. ":" .. Encode(Entry)
        end
        return "{" .. table.concat(Parts, ",") .. "}"
    elseif type(Value) == "string" then
        return '"' .. Value:gsub("\\", "\\\\"):gsub('"', '\\"'):gsub("\n", "\\n"):gsub("\r", "\\r"):gsub("\t", "\\t") .. '"'
    end
    return tostring(Value)
end
io.write(Encode(require("MapData")))
'''
    Result = Subprocess.run([str(Lua), "-e", Export], cwd=Root, capture_output=True, text=True, encoding="utf-8", check=True)
    return Json.loads(Result.stdout)


def Vector(X=0.0, Y=0.0, Z=0.0):
    return {"X": X, "Y": Y, "Z": Z}


def Transform(Location=(0, 0, 0), Scale=(1, 1, 1), Yaw=0):
    return {"Location": Vector(*Location), "Rotation": {"Pitch": 0, "Roll": 0, "Yaw": Yaw}, "Scale": Vector(*Scale)}


def Material(Color=(1, 1, 1), Texture=None, Normal=None, Tile=4):
    Textures = [Texture, Normal or "FlatNormalTexture"] if Texture else []
    return {"Base": "DefaultMaterial", "Instance": {"BaseColor": {**Vector(*Color), "W": 1}, "BlendState": "Opaque", "Properties": {}, "SamplerState": "LinearWrap", "Shininess": 20, "Textures": Textures, "UVScrollSpeed": [0, 0], "WorldUVTileSize": Tile}}


def MeshActor(Name, Mesh, Location, Scale=(1, 1, 1), Yaw=0, Surface=None, Extent=None, Center=(0, 0, 0)):
    Name = "Campus_" + Name
    Components = [{"Class": "USceneComponent", "Name": Name, "Parent": "", "Properties": {"Transform": Transform(Location, Yaw=Yaw)}}]
    Components.append({"Class": "UStaticMeshComponent", "Name": Name + "Mesh", "Parent": Name, "OverrideMaterials": [Surface] if Surface else [], "Properties": {"StaticMesh": Mesh, "Transform": Transform(Scale=Scale), "bBlockComponent": False, "bCastShadow": True, "bGenerateOverlapEvents": False, "bReceiveShadow": True, "bVisible": True}})
    if Extent is not None:
        Components.append({"Class": "UBoxComponent", "Name": Name + "Collision", "Parent": Name, "Properties": {"BoxExtent": Vector(*Extent), "ShapeColor": 4288517599, "Transform": Transform(Center), "bBlockComponent": True, "bDrawOnlyIfSelected": True, "bCastShadow": False, "bGenerateOverlapEvents": False, "bReceiveShadow": False, "bVisible": True}})
    return {"Class": "AActor", "Name": Name, "Components": Components, "Properties": {}, "RootComponent": Name}


def CubeActor(Name, Location, Size, Surface, Blocking=False):
    return MeshActor(Name, "Cube", Location, Size, Surface=Surface, Extent=tuple(Value * 0.5 for Value in Size) if Blocking else None)


def InstallEndingFadeSequences(Actors, Routes=("Car", "Door")):
    for Route in Routes:
        Name = "Ending" + Route + "Fade"
        Sequence = {"Class": "ASequenceActor", "Name": Name, "RootComponent": "SequenceRoot", "Properties": {"bAutoPlay": False}, "Components": [{"Class": "USceneComponent", "Name": "SequenceRoot", "Parent": "", "Properties": {"Transform": Transform()}}, {"Class": "UScriptComponent", "Name": Name + "Script", "Parent": "", "Properties": {"ScriptPath": "Assets/Scripts/GameLogic/EndingFade" + Route + ".lua"}}], "Sequence": {"StartFrame": 0, "DurationSeconds": 2, "FPS": 30, "Tracks": [{"Name": Route + " Escape Fade Out", "Color": {"R": 0.35 if Route == "Car" else 0.65, "G": 0.55, "B": 0.9, "A": 1.0}, "Type": "Fade", "Sections": [{"Curve": 0, "EaseExp": 2.0, "Keys": [{"Frame": 0, "Alpha": 0.0}, {"Frame": 60, "Alpha": 1.0}]}]}]}}
        Existing = next((Index for Index, Actor in enumerate(Actors) if Actor.get("Name") == Name), None)
        if Existing is None:
            Actors.append(Sequence)
        else:
            Actors[Existing] = Sequence
    from BuildCameraSequences import InstallEndingSequences
    InstallEndingSequences(Actors, Routes)


def RootComponent(Actor):
    return next(Component for Component in Actor["Components"] if Component["Name"] == Actor["RootComponent"])


def NewMesh(Name):
    return {"Name": Name, "Lines": ["mtllib Campus.mtl", "o " + Name], "Count": 0}


def AddFace(Mesh, Points, Normal, Surface, Coordinates=None):
    Mesh["Lines"].append("usemtl " + Surface)
    Points = list(reversed(Points))
    for X, Y, Z in Points:
        Mesh["Lines"].append(f"v {Y:.6f} {Z:.6f} {-X:.6f}")
    for U, V in (Coordinates or [(0, 1), (1, 1), (1, 0), (0, 0)])[:len(Points)]:
        Mesh["Lines"].append(f"vt {U} {V}")
    X, Y, Z = Normal
    for Point in Points:
        Mesh["Lines"].append(f"vn {Y:.6f} {Z:.6f} {-X:.6f}")
    Indices = [Mesh["Count"] + Index + 1 for Index in range(len(Points))]
    Mesh["Lines"].append("f " + " ".join(f"{Index}/{Index}/{Index}" for Index in Indices))
    Mesh["Count"] += len(Points)


def AddBox(Mesh, Center, Size, Surface):
    X, Y, Z = Center
    A, B, C = [Value * 0.5 for Value in Size]
    Points = [(X - A, Y - B, Z - C), (X + A, Y - B, Z - C), (X + A, Y + B, Z - C), (X - A, Y + B, Z - C), (X - A, Y - B, Z + C), (X + A, Y - B, Z + C), (X + A, Y + B, Z + C), (X - A, Y + B, Z + C)]
    for Indices, Normal in [([0, 3, 2, 1], (0, 0, -1)), ([4, 5, 6, 7], (0, 0, 1)), ([0, 4, 7, 3], (-1, 0, 0)), ([1, 2, 6, 5], (1, 0, 0)), ([0, 1, 5, 4], (0, -1, 0)), ([3, 7, 6, 2], (0, 1, 0))]:
        AddFace(Mesh, [Points[Index] for Index in Indices], Normal, Surface)


def AddRod(Mesh, Start, End, Radius, Surface, Sides=10):
    Axis = tuple(End[Index] - Start[Index] for Index in range(3))
    Length = Math.sqrt(sum(Value * Value for Value in Axis))
    Axis = tuple(Value / Length for Value in Axis)
    Helper = (0, 0, 1) if abs(Axis[2]) < 0.9 else (1, 0, 0)
    U = (Axis[1] * Helper[2] - Axis[2] * Helper[1], Axis[2] * Helper[0] - Axis[0] * Helper[2], Axis[0] * Helper[1] - Axis[1] * Helper[0])
    LengthU = Math.sqrt(sum(Value * Value for Value in U))
    U = tuple(Value / LengthU for Value in U)
    V = (Axis[1] * U[2] - Axis[2] * U[1], Axis[2] * U[0] - Axis[0] * U[2], Axis[0] * U[1] - Axis[1] * U[0])
    for Index in range(Sides):
        First = Index * Math.tau / Sides
        Second = (Index + 1) * Math.tau / Sides
        FirstOffset = tuple(Radius * (Math.cos(First) * U[Part] + Math.sin(First) * V[Part]) for Part in range(3))
        SecondOffset = tuple(Radius * (Math.cos(Second) * U[Part] + Math.sin(Second) * V[Part]) for Part in range(3))
        Normal = tuple(Math.cos((First + Second) * 0.5) * U[Part] + Math.sin((First + Second) * 0.5) * V[Part] for Part in range(3))
        A = tuple(Start[Part] + FirstOffset[Part] for Part in range(3))
        B = tuple(Start[Part] + SecondOffset[Part] for Part in range(3))
        C = tuple(End[Part] + SecondOffset[Part] for Part in range(3))
        D = tuple(End[Part] + FirstOffset[Part] for Part in range(3))
        AddFace(Mesh, [A, B, C, D], Normal, Surface)
        AddFace(Mesh, [Start, B, A], tuple(-Value for Value in Axis), Surface)
        AddFace(Mesh, [End, D, C], Axis, Surface)


def SaveMesh(Root, Mesh):
    Target = Root / "Assets/Models/Campus" / (Mesh["Name"] + ".obj")
    Target.write_text("\n".join(Mesh["Lines"]) + "\n", encoding="utf-8")
    Target.with_suffix(".obj.bin").unlink(missing_ok=True)


def ClipPolygon(Vertices, Boundary, Direction):
    Result = []
    Previous = Vertices[-1]
    PreviousDistance = Direction * (Previous[0] - Boundary)
    for Current in Vertices:
        Distance = Direction * (Current[0] - Boundary)
        if (Distance >= 0) != (PreviousDistance >= 0):
            Alpha = PreviousDistance / (PreviousDistance - Distance)
            Result.append(tuple(Previous[Index] + Alpha * (Current[Index] - Previous[Index]) for Index in range(len(Current))))
        if Distance >= 0:
            Result.append(Current)
        Previous = Current
        PreviousDistance = Distance
    return Result


def BuildYardStructure(Root, Number, Center):
    Name = "ServiceStructure" + Number + "Yard"
    Source = Root / "Assets/Models/Facility" / ("ServiceStructure" + Number + ".obj")
    Mesh = {"Name": Name, "Lines": ["mtllib ../Facility/Facility.mtl", "o " + Name], "Count": 0}
    Positions = []
    Coordinates = []
    Normals = []
    Minimum = Center - 2.0
    Maximum = Center + 2.0
    for Line in Source.read_text(encoding="utf-8").splitlines():
        Parts = Line.split()
        if not Parts:
            continue
        if Parts[0] == "v":
            Y, Z, NegativeX = map(float, Parts[1:4])
            Positions.append((-NegativeX, Y, Z))
        elif Parts[0] == "vt":
            Coordinates.append(tuple(map(float, Parts[1:3])))
        elif Parts[0] == "vn":
            Y, Z, NegativeX = map(float, Parts[1:4])
            Normals.append((-NegativeX, Y, Z))
        elif Parts[0] == "usemtl":
            Mesh["Lines"].append(Line)
        elif Parts[0] == "f":
            Vertices = []
            for Token in Parts[1:]:
                Position, Coordinate, Normal = [int(Value) - 1 for Value in Token.split("/")]
                Vertices.append(Positions[Position] + Coordinates[Coordinate] + Normals[Normal])
            Polygons = [Vertices]
            if min(Vertex[1] for Vertex in Vertices) >= 8.7 and max(Vertex[0] for Vertex in Vertices) > Minimum and min(Vertex[0] for Vertex in Vertices) < Maximum:
                Polygons = [ClipPolygon(Vertices, Minimum, -1), ClipPolygon(Vertices, Maximum, 1)]
            for Polygon in Polygons:
                if len(Polygon) < 3:
                    continue
                Area = 0.0
                for Index in range(1, len(Polygon) - 1):
                    A = tuple(Polygon[Index][Axis] - Polygon[0][Axis] for Axis in range(3))
                    B = tuple(Polygon[Index + 1][Axis] - Polygon[0][Axis] for Axis in range(3))
                    Cross = (A[1] * B[2] - A[2] * B[1], A[2] * B[0] - A[0] * B[2], A[0] * B[1] - A[1] * B[0])
                    Area += Math.sqrt(sum(Value * Value for Value in Cross))
                if Area < 0.000001:
                    continue
                for X, Y, Z, U, V, NormalX, NormalY, NormalZ in Polygon:
                    Mesh["Lines"].extend([f"v {Y:.6f} {Z:.6f} {-X:.6f}", f"vt {U:.6f} {V:.6f}", f"vn {NormalY:.6f} {NormalZ:.6f} {-NormalX:.6f}"])
                Indices = range(Mesh["Count"] + 1, Mesh["Count"] + len(Polygon) + 1)
                Mesh["Lines"].append("f " + " ".join(f"{Index}/{Index}/{Index}" for Index in Indices))
                Mesh["Count"] += len(Polygon)
    SaveMesh(Root, Mesh)


def BuildYardEntranceMeshes(Root):
    BuildYardStructure(Root, "05", 1.0)
    BuildYardStructure(Root, "08", -3.0)
    Mesh = NewMesh("RaisedServiceShutter")
    for Y in [-2.13, 2.13]:
        AddBox(Mesh, (0, Y, 1.76), (2.36, 0.26, 3.52), "Dark")
        for X in [-1.2, 1.2]:
            AddBox(Mesh, (X, Y, 1.76), (0.12, 0.26, 3.52), "Steel")
            AddBox(Mesh, (X * 1.06, Y, 0.63), (0.06, 0.26, 1.26), "Warning")
            for Z in [0.23, 0.63, 1.03]:
                AddBox(Mesh, (X * 1.09, Y, Z), (0.025, 0.26, 0.14), "Dark")
            for Z in [0.18, 1.5, 2.78, 3.4]:
                AddRod(Mesh, (X * 1.06, Y, Z), (X * 1.09, Y, Z), 0.032, "Ivory", 8)
        AddBox(Mesh, (0, Y * 0.98, 1.79), (0.19, 0.05, 3.42), "Steel")
    AddBox(Mesh, (0, 0, 3.75), (2.5, 4.58, 0.44), "Dark")
    for X in [-1.27, 1.27]:
        AddBox(Mesh, (X, 0, 3.8), (0.08, 4.52, 0.3), "Steel")
        AddBox(Mesh, (X * 1.04, 0, 3.58), (0.05, 4.55, 0.08), "Warning")
        for Y in [-1.93, -1.8, -1.67, 1.67, 1.8, 1.93]:
            AddBox(Mesh, (X * 1.04, Y, 3.8), (0.025, 0.035, 0.19), "Dark")
        AddBox(Mesh, (X * 1.05, 0, 3.8), (0.035, 1.8, 0.21), "Dark")
        for Y in [-0.67, 0, 0.67]:
            AddBox(Mesh, (X * 1.07, Y, 3.8), (0.02, 0.4, 0.035), "Ivory")
        AddBox(Mesh, (X * 1.06, -2.12, 3.95), (0.12, 0.23, 0.08), "Warning")
    AddRod(Mesh, (0, -2.0, 3.71), (0, 2.0, 3.71), 0.17, "Steel", 16)
    for Index in range(3):
        Z = 3.49 + Index * 0.095
        AddBox(Mesh, (0, 0, Z), (0.12, 3.98, 0.085), "Steel")
        for X in [-0.069, 0.069]:
            AddBox(Mesh, (X, 0, Z - 0.027), (0.018, 3.96, 0.018), "Dark")
    AddBox(Mesh, (0, 0, 3.435), (0.19, 4.0, 0.07), "Warning")
    AddBox(Mesh, (0, 0, 3.39), (0.14, 3.98, 0.025), "Dark")
    AddBox(Mesh, (0, 2.36, 3.72), (0.55, 0.25, 0.34), "Dark")
    for X in [-1.3, 1.3]:
        AddBox(Mesh, (X, 2.44, 1.45), (0.13, 0.3, 0.5), "Dark")
        AddBox(Mesh, (X * 1.06, 2.44, 1.54), (0.035, 0.19, 0.13), "Ivory")
        AddRod(Mesh, (X * 1.06, 2.44, 1.36), (X * 1.09, 2.44, 1.36), 0.045, "Warning", 10)
    AddBox(Mesh, (0, 0, 0.008), (2.45, 4.0, 0.016), "Steel")
    for X in [-1.15, 1.15]:
        AddBox(Mesh, (X, 0, 0.02), (0.08, 3.96, 0.012), "Warning")
    SaveMesh(Root, Mesh)


def InstallYardEntrances(Actors):
    Mapping = {}
    Retained = []
    for Index, Actor in enumerate(Actors):
        if Actor["RootComponent"].startswith("Campus_YardEntrance"):
            continue
        Mapping[Index] = len(Retained)
        Retained.append(Actor)
    for Actor in Retained:
        if "AttachParent" in Actor:
            Actor["AttachParent"]["Actor"] = Mapping[Actor["AttachParent"]["Actor"]]
    Actors[:] = Retained
    for Number, X in [("05", -29.5), ("08", -53.5)]:
        Structure = next(Actor for Actor in Actors if Actor["RootComponent"] == "FacilityStructure" + Number)
        for Component in Structure["Components"]:
            if Component["Class"] == "UStaticMeshComponent":
                Component["Properties"]["StaticMesh"] = "Models/Campus/ServiceStructure" + Number + "Yard.obj"
        Entrance = MeshActor("YardEntrance" + Number, "Models/Campus/RaisedServiceShutter.obj", (X, 40.5, 0), Yaw=90)
        for Label, Location, Extent in [("Left", (0, -2.13, 1.76), (1.26, 0.13, 1.76)), ("Right", (0, 2.13, 1.76), (1.26, 0.13, 1.76)), ("Header", (0, 0, 3.69), (1.3, 2.29, 0.31))]:
            Entrance["Components"].append({"Class": "UBoxComponent", "Name": Entrance["RootComponent"] + Label, "Parent": Entrance["RootComponent"], "Properties": {"BoxExtent": Vector(*Extent), "Transform": Transform(Location), "bBlockComponent": True, "bDrawOnlyIfSelected": True, "bCastShadow": False, "bGenerateOverlapEvents": False, "bReceiveShadow": False, "bVisible": True}})
        Actors.append(Entrance)


def UpdateYardEntrances(Root):
    ScenePath = Root / "Assets/Scenes/Demo/06_Game.scene"
    Scene = Json.loads(ScenePath.read_text(encoding="utf-8-sig"))
    Actors = Scene["World"]["PersistentLevel"]["Actors"]
    BuildYardEntranceMeshes(Root)
    InstallYardEntrances(Actors)
    ScenePath.write_text(Json.dumps(Scene, ensure_ascii=False, indent=4) + "\n", encoding="utf-8")
    print("Updated both yard entrances: clear 4m openings, raised service shutters and matching frame collision")


def BuildSurfaces(Root):
    Folder = Root / "Assets/Textures/Campus"
    Folder.mkdir(parents=True, exist_ok=True)
    for Index, Name, Levels, Strength in [(1, "Concrete", (99, 101, 99), 0.12), (2, "Asphalt", (42, 46, 49), 0.08)]:
        Noise = Image.frombytes("L", (512, 512), Random.Random(9100 + Index).randbytes(512 * 512))
        Channels = [Noise.point(lambda Value, Level=Level: int(Level + (Value - 128) * Strength)) for Level in Levels]
        Surface = Image.merge("RGB", Channels)
        if Name == "Concrete":
            Draw = ImageDraw.Draw(Surface)
            for Coordinate in [0, 256, 511]:
                Draw.line((Coordinate, 0, Coordinate, 511), fill=(72, 74, 72), width=2)
                Draw.line((0, Coordinate, 511, Coordinate), fill=(72, 74, 72), width=2)
        Surface.save(Folder / (Name + "_BaseColor.png"))
        Image.new("RGB", (16, 16), (128, 128, 255)).save(Folder / (Name + "_Normal.png"))


def BuildSigns(Root):
    TextureFolder = Root / "Assets/Textures/Campus"
    TextureFolder.mkdir(parents=True, exist_ok=True)
    Font = "C:/Windows/Fonts/bahnschrift.ttf"
    for Name, Number, Heading, Detail in [("GarageSign", "03", "GARAGE", "VEHICLE EXIT  /  KEY + FUEL"), ("YardSign", "06", "SERVICE YARD", "OPEN AIR  /  GARAGE AHEAD"), ("FuelSign", "F", "DIESEL", "EMERGENCY FUEL SUPPLY")]:
        Canvas = Image.new("RGB", (1024, 256), (25, 37, 43))
        Draw = ImageDraw.Draw(Canvas)
        Draw.rectangle((8, 8, 1015, 247), outline=(164, 158, 134), width=4)
        Draw.rectangle((24, 24, 188, 232), fill=(198, 141, 48))
        Draw.text((104, 125), Number, font=ImageFont.truetype(Font, 92), anchor="mm", fill=(25, 37, 43))
        Draw.text((218, 43), Heading, font=ImageFont.truetype(Font, 79 if Name != "YardSign" else 66), fill=(226, 222, 203))
        Draw.line((220, 141, 978, 141), fill=(198, 141, 48), width=3)
        Draw.text((221, 164), Detail, font=ImageFont.truetype(Font, 31), fill=(173, 187, 181))
        for X in (34, 990):
            for Y in (18, 238):
                Draw.ellipse((X - 3, Y - 3, X + 3, Y + 3), fill=(79, 89, 87))
        Canvas.save(TextureFolder / (Name + ".png"))
        Mesh = NewMesh(Name)
        AddBox(Mesh, (0, 0, 0), (0.08, 4.0, 1.0), "Dark")
        AddFace(Mesh, [(-0.045, -1.96, -0.46), (-0.045, -1.96, 0.46), (-0.045, 1.96, 0.46), (-0.045, 1.96, -0.46)], (-1, 0, 0), Name, [(1, 0), (1, 1), (0, 1), (0, 0)])
        SaveMesh(Root, Mesh)


def BuildSlidingGateMeshes(Root):
    Mesh = NewMesh("SlidingFenceGate")
    for Y in [-4.07, 4.07]:
        AddBox(Mesh, (0, Y, 1.43), (0.16, 0.16, 2.6), "Steel")
        AddBox(Mesh, (-0.09, Y, 1.4), (0.035, 0.16, 0.7), "Warning")
    for Z in [0.28, 1.45, 2.68]:
        AddBox(Mesh, (0, 0, Z), (0.1, 8.3, 0.1), "Steel")
    for Index in range(39):
        AddBox(Mesh, (0, -3.95 + Index * 7.9 / 38, 1.48), (0.035, 0.035, 2.36), "Dark")
    AddRod(Mesh, (0.06, -3.95, 0.36), (0.06, 3.95, 2.6), 0.028, "Steel", 8)
    for Y in [-3.5, 3.5]:
        AddRod(Mesh, (-0.11, Y, 0.15), (0.11, Y, 0.15), 0.1, "Dark", 12)
        AddBox(Mesh, (0, Y, 0.26), (0.28, 0.4, 0.1), "Steel")
    SaveMesh(Root, Mesh)
    Mesh = NewMesh("SlidingGateTrack")
    AddBox(Mesh, (0, 4.1, 0.025), (0.12, 17.0, 0.05), "Steel")
    for Y in [-4.3, 4.3]:
        AddBox(Mesh, (0.3, Y, 1.5), (0.22, 0.22, 3.0), "Steel")
        AddBox(Mesh, (0.3, Y, 0.07), (0.5, 0.5, 0.14), "Dark")
        AddBox(Mesh, (0.1, Y, 2.88), (0.7, 0.4, 0.12), "Steel")
        for Z in [0.6, 0.9]:
            AddBox(Mesh, (0.3, Y, Z), (0.24, 0.24, 0.13), "Warning")
        AddRod(Mesh, (0, Y, 2.72), (0, Y, 2.82), 0.1, "Dark", 12)
    AddBox(Mesh, (0.45, 4.9, 0.45), (0.7, 0.75, 0.9), "Dark")
    AddBox(Mesh, (0.45, 4.9, 0.93), (0.76, 0.81, 0.08), "Warning")
    AddBox(Mesh, (-0.065, 4.9, 0.18), (0.34, 0.12, 0.18), "Steel")
    SaveMesh(Root, Mesh)


def InstallSlidingGate(Actors, Data):
    Gate = next(Actor for Actor in Actors if Actor["RootComponent"] == "EndingGarageGate")
    Landmark = Data["Landmarks"]["GarageGate"]
    Location = tuple(Landmark["Location"][Axis] for Axis in ["X", "Y", "Z"])
    RootComponent(Gate)["Properties"]["Transform"] = Transform(Location, Yaw=Landmark["Yaw"])
    for Component in Gate["Components"]:
        if Component["Class"] == "UStaticMeshComponent":
            Component["Properties"]["StaticMesh"] = "Models/Campus/SlidingFenceGate.obj"
            Component["Properties"]["Transform"] = Transform()
            Component["OverrideMaterials"] = []
        elif Component["Class"] == "UBoxComponent":
            Component["Properties"]["BoxExtent"] = Vector(0.12, 4.15, 1.4)
            Component["Properties"]["Transform"] = Transform((0, 0, 1.4))
    Track = MeshActor("SlidingGateTrack", "Models/Campus/SlidingGateTrack.obj", Location, Yaw=Landmark["Yaw"])
    for Index, Y in enumerate([-4.3, 4.3]):
        Track["Components"].append({"Class": "UBoxComponent", "Name": "Campus_GateGuidePost" + str(Index), "Parent": Track["RootComponent"], "Properties": {"BoxExtent": Vector(0.12, 0.12, 1.5), "Transform": Transform((0.3, Y, 1.5)), "bBlockComponent": True, "bDrawOnlyIfSelected": True, "bCastShadow": False, "bGenerateOverlapEvents": False, "bVisible": True}})
    for Index, Actor in enumerate(Actors):
        if Actor["RootComponent"] == Track["RootComponent"]:
            Actors[Index] = Track
            break
    else:
        Actors.append(Track)


def BuildMeshes(Root):
    Folder = Root / "Assets/Models/Campus"
    Folder.mkdir(parents=True, exist_ok=True)
    Materials = []
    for Name, Color in [("Steel", (0.4, 0.48, 0.5)), ("Rust", (0.55, 0.25, 0.12)), ("Dark", (0.055, 0.075, 0.085)), ("Warning", (0.88, 0.55, 0.1)), ("Ivory", (0.74, 0.77, 0.7)), ("Lens", (0.6, 0.77, 0.83))]:
        Materials.extend(["newmtl " + Name, "Kd " + " ".join(map(str, Color)), "d 1.0"])
        if Name in ("Steel", "Rust"):
            Materials.extend(["map_Kd ../../Textures/Facility/RustedSteel_BaseColor.png", "map_bump ../../Textures/Facility/RustedSteel_Normal.png"])
    for Name in ["GarageSign", "YardSign", "FuelSign"]:
        Materials.extend(["newmtl " + Name, "Kd 1 1 1", "d 1.0", "map_Kd ../../Textures/Campus/" + Name + ".png"])
    (Folder / "Campus.mtl").write_text("\n".join(Materials) + "\n", encoding="utf-8")
    Mesh = NewMesh("GarageBayFrame")
    for Y in [-5.25, 5.25]:
        AddBox(Mesh, (0, Y, 2.7), (0.65, 0.45, 5.4), "Steel")
        AddBox(Mesh, (-0.36, Y, 0.5), (0.12, 0.5, 1.0), "Warning")
    AddBox(Mesh, (0, 0, 5.22), (0.7, 10.95, 0.46), "Steel")
    AddBox(Mesh, (-0.38, 0, 4.75), (0.08, 10.3, 0.18), "Warning")
    SaveMesh(Root, Mesh)
    Mesh = NewMesh("VehicleLift")
    for Y in [-1.0, 1.0]:
        AddBox(Mesh, (0, Y, 1.8), (0.48, 0.36, 3.6), "Rust")
        AddBox(Mesh, (0, Y, 0.075), (0.9, 0.65, 0.15), "Dark")
        AddBox(Mesh, (0, Y * 0.65, 0.35), (4.4, 0.16, 0.18), "Warning")
        AddBox(Mesh, (-0.28, Y, 1.4), (0.12, 0.4, 0.5), "Dark")
    AddBox(Mesh, (0, 0, 3.43), (0.38, 2.35, 0.24), "Steel")
    SaveMesh(Root, Mesh)
    Mesh = NewMesh("FuelPump")
    AddBox(Mesh, (0, 0, 0.12), (1.3, 1.2, 0.24), "Dark")
    AddBox(Mesh, (0, 0, 0.85), (0.9, 0.85, 1.3), "Rust")
    AddBox(Mesh, (0, 0, 1.7), (1.0, 1.0, 0.8), "Ivory")
    AddBox(Mesh, (-0.515, 0, 1.8), (0.045, 0.75, 0.27), "Dark")
    for Y in [-0.2, 0, 0.2]:
        AddBox(Mesh, (-0.55, Y, 1.8), (0.025, 0.1, 0.12), "Lens")
    Hose = [(0, 0.53, 1.9), (0, 0.8, 1.5), (0, 0.86, 0.65), (0, 0.72, 0.4), (0, 0.52, 0.7), (-0.2, 0.52, 1.3)]
    for Start, End in zip(Hose, Hose[1:]):
        AddRod(Mesh, Start, End, 0.035, "Dark", 8)
    AddBox(Mesh, (-0.2, 0.52, 1.35), (0.18, 0.12, 0.3), "Warning")
    SaveMesh(Root, Mesh)
    Mesh = NewMesh("Floodlight")
    AddBox(Mesh, (0, 0, 0.1), (0.56, 0.56, 0.2), "Dark")
    AddRod(Mesh, (0, 0, 0.2), (0, 0, 6.15), 0.085, "Steel")
    AddBox(Mesh, (0, 0, 6.1), (0.18, 2.1, 0.15), "Steel")
    for Y in [-0.72, 0.72]:
        AddBox(Mesh, (-0.16, Y, 5.97), (0.75, 0.58, 0.22), "Dark")
        AddBox(Mesh, (-0.16, Y, 5.845), (0.65, 0.48, 0.035), "Lens")
    SaveMesh(Root, Mesh)
    Mesh = NewMesh("Bollard")
    AddBox(Mesh, (0, 0, 0.07), (0.44, 0.44, 0.14), "Dark")
    AddRod(Mesh, (0, 0, 0.14), (0, 0, 0.96), 0.13, "Warning")
    AddRod(Mesh, (0, 0, 0.35), (0, 0, 0.48), 0.135, "Dark")
    AddRod(Mesh, (0, 0, 0.72), (0, 0, 0.85), 0.135, "Dark")
    SaveMesh(Root, Mesh)
    Mesh = NewMesh("FencePanel")
    for Y in [-1.95, 1.95]:
        AddBox(Mesh, (0, Y, 1.4), (0.12, 0.12, 2.8), "Steel")
    for Z in [0.25, 1.4, 2.65]:
        AddBox(Mesh, (0, 0, Z), (0.075, 4.0, 0.075), "Steel")
    for Index in range(19):
        AddBox(Mesh, (0, -1.8 + Index * 0.2, 1.45), (0.028, 0.028, 2.5), "Dark")
    SaveMesh(Root, Mesh)
    BuildSurfaces(Root)
    BuildSigns(Root)
    BuildSlidingGateMeshes(Root)
    BuildYardEntranceMeshes(Root)


def InstallCeilingLightFlicker(Actors):
    ScriptPath = "Assets/Scripts/GameLogic/CeilingLightFlickerActor.lua"
    for Actor in Actors:
        Components = Actor["Components"]
        if not any(Component["Properties"].get("StaticMesh") == "Models/Facility/CeilingLight.obj" for Component in Components) or not any(Component["Class"] == "UPointLightComponent" for Component in Components):
            continue
        if not any(Component["Properties"].get("ScriptPath") == ScriptPath for Component in Components):
            Components.append({"Class": "UScriptComponent", "Name": "CeilingLightFlickerScript", "Parent": "", "Properties": {"ScriptPath": ScriptPath}})


def BuildScene(Root, Data):
    ScenePath = Root / "Assets/Scenes/Demo/06_Game.scene"
    Scene = Json.loads(ScenePath.read_text(encoding="utf-8-sig"))
    Original = Scene["World"]["PersistentLevel"]["Actors"]
    Actors = []
    Mapping = {}
    for Index, Actor in enumerate(Original):
        Name = Actor["RootComponent"]
        Components = Actor["Components"]
        Position = RootComponent(Actor)["Properties"]["Transform"]["Location"]
        Generated = Name.startswith("Campus_") or any(Component["Name"].startswith("MapGen_") for Component in Components)
        Replaced = Name.startswith("FacilityProp") or any(Name.startswith(Prefix + Sector) for Prefix in ["FacilityStructure", "FacilitySectorSign", "FacilityVent", "FacilityCabinet", "FacilityCeilingLight"] for Sector in ["03", "06", "09"])
        RemovedDoor = any(Name in ["FacilityDoor" + Number, "FacilityDoor" + Number + "Frame"] for Number in ["05", "06", "08", "11"])
        OldPortal = Name.startswith("FacilityPortal") and Position["Y"] > 39
        OldTorch = Name.startswith("TorchRoot") and Position["Y"] > 41
        if Generated or Replaced or RemovedDoor or OldPortal or OldTorch:
            continue
        Mapping[Index] = len(Actors)
        Actors.append(Actor)
    for Actor in Actors:
        if "AttachParent" in Actor:
            Actor["AttachParent"]["Actor"] = Mapping[Actor["AttachParent"]["Actor"]]
        Name = Actor["RootComponent"]
        RootData = RootComponent(Actor)["Properties"]["Transform"]
        Scripts = [Component["Properties"].get("ScriptPath", "") for Component in Actor["Components"]]
        Landmark = "CarExit" if Name == "CarExit" else "GarageGate" if Name == "EndingGarageGate" else "FuelCan" if any(Script.endswith("Item_FuelCan.lua") for Script in Scripts) else "CardKey" if any(Script.endswith("Item_CardKey.lua") for Script in Scripts) else None
        if Landmark:
            RootData["Location"] = Vector(*(Data["Landmarks"][Landmark]["Location"][Axis] for Axis in ["X", "Y", "Z"]))
            RootData["Rotation"]["Yaw"] = Data["Landmarks"][Landmark]["Yaw"]
        if Name.startswith("EndingRoad") or Name == "EndingEscapeRoad":
            if RootData["Location"]["Y"] < 30:
                RootData["Location"]["X"] = -20.5 - RootData["Location"]["X"]
                RootData["Location"]["Y"] += 35.0
                RootData["Location"]["Z"] += 0.065
            if Name in ["EndingEscapeRoad", "EndingRoadCurbLeft", "EndingRoadCurbRight"]:
                RootData["Location"]["X"] = -140.5
                RootData["Scale"]["X"] = 241.0
        if Name == "FacilityAtmosphere":
            for Component in Actor["Components"]:
                if Component["Class"] == "USkyLightComponent":
                    Component["Properties"]["Intensity"] = 0.03
                elif Component["Class"] == "UExponentialHeightFogComponent":
                    Component["Properties"]["FogColor"] = {"X": 0.19, "Y": 0.27, "Z": 0.31, "W": 1}
                    Component["Properties"]["FogDensity"] = 0.08
                    Component["Properties"]["FogMaxOpacity"] = 0.98
                    Component["Properties"]["FogHeightFalloff"] = 0.03
                    Component["Properties"]["StartDistance"] = 45.0
    Rows = Data["Floors"][0].strip("\r\n").splitlines()
    Origin = Data.get("Origin", Vector())
    WallMaterial = Material(Texture=Data["WallBaseColor"], Normal=Data["WallNormal"], Tile=4)
    FloorMaterial = Material(Texture=Data["FloorBaseColor"], Normal=Data["FloorNormal"], Tile=2)
    RoofMaterial = Material((0.64, 0.69, 0.72), Data["CeilingBaseColor"], Data["CeilingNormal"], 4)
    Visited = set()

    def WallHeight(Column, Row):
        if Row >= len(Rows) or Column >= len(Rows[Row]) or Rows[Row][Column] != "#":
            return 0
        OriginalRow = Row - int(Origin["X"])
        if (Column >= 60 and OriginalRow >= 22) or (Column >= 42 and OriginalRow >= 60):
            return 0
        return 5.5 if Column >= 40 and 0 <= OriginalRow <= 21 else 4.0

    for Row, Line in enumerate(Rows):
        for Column in range(len(Line)):
            Height = WallHeight(Column, Row)
            if not Height or (Column, Row) in Visited:
                continue
            EndColumn = Column
            while EndColumn + 1 < len(Line) and (EndColumn + 1, Row) not in Visited and WallHeight(EndColumn + 1, Row) == Height:
                EndColumn += 1
            EndRow = Row
            while EndRow + 1 < len(Rows) and all((X, EndRow + 1) not in Visited and WallHeight(X, EndRow + 1) == Height for X in range(Column, EndColumn + 1)):
                EndRow += 1
            Visited.update((X, Y) for X in range(Column, EndColumn + 1) for Y in range(Row, EndRow + 1))
            Actors.append(CubeActor(f"Wall_{Row}_{Column}", (Origin["X"] - (Row + EndRow) * 0.5, Origin["Y"] + (Column + EndColumn) * 0.5, Height * 0.5), (EndRow - Row + 1, EndColumn - Column + 1, Height), WallMaterial, True))
    Actors.append(CubeActor("Foundation", (-30.5, 30.5, -0.5), (62, 62, 1), FloorMaterial))
    for Region in Data["SurfaceRegions"]:
        FirstColumn, LastColumn = Region["MinimumColumn"] - 1, Region["MaximumColumn"] - 1
        FirstRow, LastRow = Region["MinimumRow"] - 1, Region["MaximumRow"] - 1
        CenterX, CenterY = Origin["X"] - (FirstRow + LastRow) * 0.5, Origin["Y"] + (FirstColumn + LastColumn) * 0.5
        Width, Length = LastColumn - FirstColumn + 1, LastRow - FirstRow + 1
        if "BaseColor" in Region:
            Surface = Material((0.9, 0.93, 0.95) if Region.get("bOutdoor") else (0.72, 0.73, 0.69), Region["BaseColor"], Region["Normal"], 3)
            Actors.append(CubeActor(Region["Id"] + "Surface", (CenterX, CenterY, 0.006), (Length, Width, 0.012), Surface))
        if "RoofHeight" in Region:
            Actors.append(CubeActor(Region["Id"] + "Roof", (CenterX, CenterY, Region["RoofHeight"] + 0.1), (Length, Width, 0.2), RoofMaterial))
    for Prop in Data["Dressing"]["Props"]:
        if "SceneActor" in Prop:
            continue
        Definition = Data["Dressing"]["Types"][Prop["Type"]]
        Actors.append(MeshActor(Prop["Name"], Definition["Mesh"], tuple(Prop["Location"][Axis] for Axis in ["X", "Y", "Z"]), Yaw=Prop.get("Yaw", 0), Extent=tuple(Definition["Extent"][Axis] for Axis in ["X", "Y", "Z"]), Center=tuple(Definition["Center"][Axis] for Axis in ["X", "Y", "Z"])))
    Actors.append(MeshActor("GarageBayFrame", "Models/Campus/GarageBayFrame.obj", (-20.5, 50.5, 0)))
    Actors.append(MeshActor("GarageSign", "Models/Campus/GarageSign.obj", (-20.9, 50.5, 4.32)))
    Actors.append(MeshActor("YardSign", "Models/Campus/YardSign.obj", (-27.5, 41.9, 2.7), Scale=(1, 0.8, 0.8), Yaw=90))
    Actors.append(MeshActor("FuelSign", "Models/Campus/FuelSign.obj", (-31, 44.1, 2.8), Scale=(1, 0.5, 0.5), Yaw=-90))
    Warning = Material((0.82, 0.55, 0.12))
    Dark = Material((0.065, 0.085, 0.095))
    Ivory = Material((0.72, 0.72, 0.61))
    for X in [-4.0, -10.5, -17.0]:
        Actors.append(CubeActor(f"GarageRoofBeam{X}", (X, 50.5, 5.22), (0.24, 18, 0.35), Dark))
    for Y in [47.0, 54.0]:
        Actors.append(CubeActor(f"ParkingLine{Y}", (-10.5, Y, 0.025), (12, 0.085, 0.018), Ivory))
    for X in [-4.4, -16.6]:
        Actors.append(CubeActor(f"ParkingStop{X}", (X, 50.5, 0.025), (0.085, 7.0, 0.018), Warning))
    for X in [-21.8, -22.2, -22.6]:
        Actors.append(CubeActor(f"BayThreshold{X}", (X, 50.5, 0.025), (0.18, 10, 0.018), Warning))
    InstallYardEntrances(Actors)
    for Label, First, Last, Yaw in [("East", (-21.5, 59.6), (-61.5, 59.6), 90), ("SouthWest", (-60.5, 41.5), (-60.5, 46.5), 0), ("SouthEast", (-60.5, 54.5), (-60.5, 61.5), 0)]:
        Distance = Math.hypot(Last[0] - First[0], Last[1] - First[1])
        Count = Math.ceil(Distance / 4)
        Segment = Distance / Count
        for Index in range(Count):
            Progress = (Index + 0.5) / Count
            Location = (First[0] + (Last[0] - First[0]) * Progress, First[1] + (Last[1] - First[1]) * Progress, 0)
            Actors.append(MeshActor(f"Fence{Label}{Index}", "Models/Campus/FencePanel.obj", Location, Scale=(1, Segment / 4, 1), Yaw=Yaw, Extent=(0.08, Segment * 0.5, 1.4), Center=(0, 0, 1.4)))
    Actors.append(CubeActor("SouthExteriorGround", (-162.5, 50.5, -0.36), (210, 36, 0.6), Material((0.21, 0.24, 0.22), "Assets/Textures/StoneGround/StoneGround_BaseColor.png", "Assets/Textures/StoneGround/StoneGround_Normal.png", 5)))
    SunTransform = Transform((-35, 50, 20), Yaw=-30)
    SunTransform["Rotation"]["Pitch"] = 55
    Actors.append({"Class": "AActor", "RootComponent": "Campus_ExteriorSun", "Properties": {}, "Components": [{"Class": "UDirectionalLightComponent", "Name": "Campus_ExteriorSun", "Parent": "", "Properties": {"Transform": SunTransform, "Intensity": 0.16, "LightColor": 4294960860, "ShadowDistance": 90, "ShadowMapResolution": 1024, "ShadowBias": 0.001, "ShadowPCFKernelSize": 2, "SourceAngle": 3.0, "bUseCSM": False, "bCastShadows": True, "bVisible": True}}]})
    for Name, Location, Color, Intensity, Radius in [("GarageWarm", (-9, 50.5, 4.8), 4291880959, 1.12, 15), ("GarageService", (-14, 44.5, 4.8), 4291559423, 0.72, 10), ("YardNorth", (-26, 55, 5.8), 4294960860, 0.88, 21), ("YardSouth", (-46, 55, 5.8), 4294960860, 0.8, 23), ("FuelWarm", (-31, 45, 3.5), 4285772287, 0.44, 9)]:
        Actor = MeshActor(Name, "Models/Facility/CeilingLight.obj", Location, Scale=(0.7, 0.7, 0.7)) if Name.startswith("Garage") else {"Class": "AActor", "Components": [{"Class": "USceneComponent", "Name": "Campus_" + Name, "Parent": "", "Properties": {"Transform": Transform(Location)}}], "Properties": {}, "RootComponent": "Campus_" + Name}
        Actor["Components"].append({"Class": "UPointLightComponent", "Name": "Campus_" + Name + "Light", "Parent": Actor["RootComponent"], "Properties": {"AttenuationRadius": Radius, "Intensity": Intensity, "LightColor": Color, "LightFalloffExponent": 2.0, "SourceRadius": 0.15, "ShadowBias": 0.001, "ShadowMapResolution": 256, "ShadowPCFKernelSize": 1, "Transform": Transform(), "bCastShadows": False, "bUsePCSS": False, "bVisible": True}})
        Actors.append(Actor)
    InstallSlidingGate(Actors, Data)
    InstallEndingFadeSequences(Actors)
    from BuildIntroEntrance import InstallIntroEntrance
    InstallIntroEntrance(Actors, Data)
    Scene["World"]["PersistentLevel"]["Actors"] = Actors
    ScenePath.write_text(Json.dumps(Scene, ensure_ascii=False, indent=4) + "\n", encoding="utf-8")
    print(f"Built Demo/06_Game.scene: {len(Actors)} actors, {len(Visited)} wall cells, 2 roofs, open service yard and garage vehicle exit")


def Main():
    Parser = Argparse.ArgumentParser()
    Parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parents[1])
    Parser.add_argument("--yard-entrances-only", dest="YardEntrancesOnly", action="store_true")
    Arguments = Parser.parse_args()
    Root = Arguments.root.resolve()
    if Arguments.YardEntrancesOnly:
        UpdateYardEntrances(Root)
        return
    Data = LoadMap(Root)
    BuildMeshes(Root)
    BuildScene(Root, Data)
    from BuildEscapeRoute import UpdateEscapeRoute
    UpdateEscapeRoute(Root)


if __name__ == "__main__":
    Main()
