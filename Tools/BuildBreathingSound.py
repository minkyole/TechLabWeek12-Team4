import argparse as Argparse
import sys as Sys
import wave as Wave
from array import array as Array
from pathlib import Path


def PrepareRecording(Source):
    with Wave.open(str(Source), "rb") as Recording:
        if Recording.getnchannels() != 1 or Recording.getsampwidth() != 2 or Recording.getcomptype() != "NONE":
            raise ValueError("The source must be a mono 16-bit PCM recording")
        SampleRate = Recording.getframerate()
        Samples = Array("h", Recording.readframes(Recording.getnframes()))
    if Sys.byteorder != "little":
        Samples.byteswap()
    if not Samples:
        raise ValueError("The source recording is empty")
    Mean = sum(Samples) / len(Samples)
    Peak = max(abs(Value - Mean) for Value in Samples)
    if Peak == 0.0:
        raise ValueError("The source recording is silent")
    Gain = 0.88 * 32767 / Peak
    FadeFrames = max(1, round(SampleRate * 0.015))
    Prepared = Array("h", (round((Value - Mean) * Gain * min(1.0, Index / FadeFrames, (len(Samples) - 1 - Index) / FadeFrames)) for Index, Value in enumerate(Samples)))
    if Sys.byteorder != "little":
        Prepared.byteswap()
    return Prepared, SampleRate


def Main():
    Root = Path(__file__).resolve().parents[1]
    Parser = Argparse.ArgumentParser(description="Normalize a real human breathing recording for the recovery loop")
    Parser.add_argument("Source", nargs="?", type=Path, default=Root / "Build/AudioSources/BreathlessMan1103.wav")
    Arguments = Parser.parse_args()
    Frames, SampleRate = PrepareRecording(Arguments.Source)
    Target = Root / "Assets/Sounds/RecoveryBreathing.wav"
    with Wave.open(str(Target), "wb") as Sound:
        Sound.setnchannels(1)
        Sound.setsampwidth(2)
        Sound.setframerate(SampleRate)
        Sound.writeframes(Frames.tobytes())
    print(f"Prepared recorded breathing: {len(Frames) / SampleRate:.3f}s, mono {SampleRate} Hz, 16-bit PCM")


if __name__ == "__main__":
    Main()
