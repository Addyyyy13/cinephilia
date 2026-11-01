#!/usr/bin/env python3
"""Objective checks on the synthesised sound effects.

This cannot judge how the sounds *feel*; it checks the properties the sound
design promises: valid 44.1 kHz 16-bit stereo WAV, quiet peaks, no clipping,
no clicks at the edges, no DC offset, little energy above 6 kHz, and short
durations. Uses only the standard library (numpy is used if present for the
spectral check, which is skipped otherwise).

Usage: check_sounds.py ASSET_DIR
"""
import math
import struct
import sys
import wave
from pathlib import Path

LIMITS = {  # file: (max seconds, max peak dBFS)
    "startup.wav": (3.0, -11.0),
    "menu_move.wav": (0.06, -25.0),
    "section_open.wav": (0.5, -19.0),
    "film_added.wav": (0.8, -17.0),
    "confirm.wav": (0.4, -23.0),
    "film_reel.wav": (1.0, -19.0),
    "reveal.wav": (1.0, -19.0),
    "delete.wav": (0.4, -21.0),
    "error.wav": (0.3, -23.0),
    "projector_stop.wav": (1.6, -15.0),
}


def db(x):
    return 20 * math.log10(x) if x > 0 else -999.0


def main():
    folder = Path(sys.argv[1] if len(sys.argv) > 1 else "assets/audio")
    try:
        import numpy as np
    except ImportError:
        np = None
    failures = 0
    print(f"{'file':20} {'secs':>5} {'peak':>7} {'rms':>7} {'edge':>8} {'>6kHz':>6}")
    for name, (max_secs, max_peak) in LIMITS.items():
        path = folder / name
        problems = []
        if not path.exists():
            print(f"{name:20} MISSING")
            failures += 1
            continue
        with wave.open(str(path)) as w:
            if (w.getnchannels(), w.getsampwidth(), w.getframerate()) != (2, 2, 44100):
                problems.append("format is not 44.1 kHz 16-bit stereo")
            frames = w.readframes(w.getnframes())
            n = w.getnframes()
        samples = struct.unpack("<%dh" % (n * 2), frames)
        peak = max(abs(s) for s in samples) / 32768
        rms = math.sqrt(sum(s * s for s in samples) / len(samples)) / 32768
        edge = max(abs(s) for s in samples[:4] + samples[-4:]) / 32768
        dc = sum(samples) / len(samples) / 32768
        secs = n / 44100
        if secs > max_secs:
            problems.append(f"too long ({secs:.2f}s)")
        if db(peak) > max_peak + 0.01:
            problems.append(f"too loud ({db(peak):.1f} dBFS)")
        if peak >= 0.999:
            problems.append("clips")
        if edge > 0.002:
            problems.append("audible click at an edge")
        if abs(dc) > 0.001:
            problems.append("DC offset")
        if rms < 1e-4:
            problems.append("silent")
        hf = "-"
        if np is not None:
            mono = (np.array(samples[0::2], dtype=float) + np.array(samples[1::2], dtype=float)) / 2
            spec = np.abs(np.fft.rfft(mono)) ** 2
            freqs = np.fft.rfftfreq(len(mono), 1 / 44100)
            frac = spec[freqs > 6000].sum() / max(spec.sum(), 1e-12)
            hf = f"{frac * 100:.1f}%"
            if frac > 0.05:
                problems.append(f"harsh: {frac * 100:.1f}% of energy above 6 kHz")
        print(f"{name:20} {secs:5.2f} {db(peak):7.1f} {db(rms):7.1f} {db(edge):8.1f} {hf:>6}"
              + ("   " + "; ".join(problems) if problems else ""))
        failures += bool(problems)
    print("all sound checks passed" if not failures else f"{failures} file(s) failed")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
