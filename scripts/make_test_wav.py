#!/usr/bin/env python3
"""Generate a tiny synthetic WAV for manual/headless testing.

The clip is produced from scratch (no copyrighted asset): a one-second mono
16-bit tone that is silent for its first half, then a 440 Hz sine at half
scale. That shape makes the loudness envelope easy to eyeball in the timeline
(silence, then a block of bars) and exercises the decoder's channel/rate
handling. Standard library only, so the CI smoke run needs no audio toolchain.
"""
import math
import struct
import sys


def write_wav(path: str, rate: int = 16000, seconds: float = 1.0) -> None:
    frames = int(rate * seconds)
    samples = []
    for i in range(frames):
        if i < frames // 2:
            samples.append(0.0)
        else:
            t = i / rate
            samples.append(0.5 * math.sin(2.0 * math.pi * 440.0 * t))

    data = bytearray()
    for s in samples:
        clamped = max(-1.0, min(1.0, s))
        data += struct.pack("<h", int(round(clamped * 32767.0)))

    byte_rate = rate * 1 * 16 // 8
    block_align = 1 * 16 // 8
    with open(path, "wb") as f:
        f.write(b"RIFF")
        f.write(struct.pack("<I", 36 + len(data)))
        f.write(b"WAVE")
        f.write(b"fmt ")
        f.write(struct.pack("<I", 16))          # PCM fmt chunk size
        f.write(struct.pack("<H", 1))           # PCM
        f.write(struct.pack("<H", 1))           # mono
        f.write(struct.pack("<I", rate))
        f.write(struct.pack("<I", byte_rate))
        f.write(struct.pack("<H", block_align))
        f.write(struct.pack("<H", 16))          # bits per sample
        f.write(b"data")
        f.write(struct.pack("<I", len(data)))
        f.write(bytes(data))


if __name__ == "__main__":
    out = sys.argv[1] if len(sys.argv) > 1 else "test_audio.wav"
    write_wav(out)
    print(f"wrote {out}")
