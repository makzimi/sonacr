#!/usr/bin/env python3
"""Deterministic music-like test audio: chords, melody, and percussion (stdlib only)."""
import argparse
import math
import random
import struct
import wave

SAMPLE_RATE = 22050
SCALE = [0, 2, 3, 5, 7, 8, 10]  # natural minor


def note_hz(base_midi: int, degree: int) -> float:
    octave, step = divmod(degree, len(SCALE))
    midi = base_midi + 12 * octave + SCALE[step]
    return 440.0 * 2.0 ** ((midi - 69) / 12.0)


def render(seed: int, seconds: float) -> list[float]:
    rng = random.Random(seed)
    total = int(seconds * SAMPLE_RATE)
    out = [0.0] * total
    beat = int(rng.uniform(0.18, 0.32) * SAMPLE_RATE)
    base = rng.randint(45, 57)
    for start in range(0, total, beat):
        length = min(beat * rng.choice([1, 1, 2]), total - start)
        voices = [note_hz(base, rng.randint(0, 6)), note_hz(base, rng.randint(7, 20))]
        if rng.random() < 0.5:
            voices.append(note_hz(base, rng.randint(14, 27)))
        for hz in voices:
            amp = rng.uniform(0.08, 0.18)
            for n in range(length):
                t = n / SAMPLE_RATE
                env = min(1.0, n / 200.0) * math.exp(-3.0 * t)
                s = math.sin(2 * math.pi * hz * t) + 0.5 * math.sin(4 * math.pi * hz * t) + 0.25 * math.sin(6 * math.pi * hz * t)
                out[start + n] += amp * env * s
        hit = min(int(0.04 * SAMPLE_RATE), total - start)
        drum = rng.uniform(0.1, 0.3)
        for n in range(hit):
            out[start + n] += drum * (rng.random() * 2 - 1) * math.exp(-n / (0.008 * SAMPLE_RATE))
    peak = max(1e-9, max(abs(v) for v in out))
    return [0.89 * v / peak for v in out]


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--seed", type=int, required=True)
    parser.add_argument("--seconds", type=float, default=30.0)
    parser.add_argument("output")
    args = parser.parse_args()
    samples = render(args.seed, args.seconds)
    with wave.open(args.output, "wb") as wav:
        wav.setnchannels(1)
        wav.setsampwidth(2)
        wav.setframerate(SAMPLE_RATE)
        wav.writeframes(b"".join(struct.pack("<h", int(v * 32767)) for v in samples))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
