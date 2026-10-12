#!/usr/bin/env python3
"""Find the scale of an engine-speed field by holding it against a recording of the engine.

    .venv/bin/python tools/5-rpm-from-audio.py capture.log.xz "motor 20261011-2016.mp3" \\
        --start 1791742586.2

Why this exists: an rpm anchor read off the dash is one number, late, and rounded by the
dash. The engine's sound is the engine. A four-stroke fires once per cylinder every two
turns, so a triple's firing frequency is rpm / 40, and a phone recording shows that
frequency and its multiples as lines. Take the raw field from the capture, guess a scale,
and the guess predicts where those lines are at every moment; the right scale is the one
whose predictions land on them. On 2026-10-11 that gave 0.3915 for 0x27C bytes 0-1.

What it can and cannot tell you:

  it finds a pure scale. raw * k, no offset. Over a narrow span of rpm a scale and a
             scale-plus-offset fit equally well, and an idle is a narrow span.
  it needs the speed to hold still for about a second. The spectrum is taken over 0.37 s
             and the two clocks are only matched to a second or so, so a blip of the
             throttle smears across the lines and lands in the wrong place besides. A
             slow drift is ideal; so is a steady hold at a few speeds.
  --start    is the epoch at which the audio begins. Android stamps a recording with when
             it ENDED: take creation_time and subtract the duration. Good to a second,
             on the phone's clock, which is enough for the above and for nothing finer.
  --cylinders  sets the firing frequency. A wrong value gives a clean, confident, wrong
             answer, off by the ratio of the two counts.

The harmonics table it prints is the check on that last one: the firing orders should
stand clear and the orders between them should not. If every order is strong the peak may
be a multiple or a fraction of the truth.

Needs ffmpeg (or ffmpeg.exe from WSL) for anything that is not already a WAV file.
"""

import argparse
import lzma
import shutil
import subprocess
import sys

import numpy as np
from scipy.ndimage import median_filter
from scipy.signal import stft

RATE = 22050
WINDOW = 8192          # 0.37 s: lines 2.7 Hz wide, narrow enough to split an idle's harmonics
PAD = 4


def read_audio(path):
    exe = shutil.which("ffmpeg") or shutil.which("ffmpeg.exe")
    if not exe:
        sys.exit("ffmpeg not found; convert the recording to WAV by hand or install it")
    if exe.endswith(".exe"):                                # a Windows ffmpeg wants a Windows path
        path = subprocess.run(["wslpath", "-w", path], capture_output=True, text=True, check=True).stdout.strip()
    raw = subprocess.run([exe, "-v", "error", "-i", path, "-f", "s16le", "-ac", "1", "-ar", str(RATE), "-"],
                         capture_output=True, check=True).stdout
    return np.frombuffer(raw, dtype=np.int16).astype(float)


def read_field(path, can_id, first, last):
    """Times and the big-endian value of bytes first..last of one id, from a candump log."""
    opener = lzma.open if path.endswith(".xz") else open
    want = f" {can_id}#"
    t, v = [], []
    with opener(path, "rt") as f:
        for line in f:
            if want in line:
                stamp, _, frame = line.split()
                data = bytes.fromhex(frame.split("#")[1])
                t.append(float(stamp[1:-1]))
                v.append(int.from_bytes(data[first:last + 1], "big"))
    return np.array(t), np.array(v, dtype=float)


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("capture", help="candump log, plain or .xz")
    ap.add_argument("audio")
    ap.add_argument("--start", type=float, required=True, help="epoch at which the audio begins")
    ap.add_argument("--id", default="27C")
    ap.add_argument("--bytes", default="0-1", help="first-last, big-endian (default 0-1)")
    ap.add_argument("--cylinders", type=int, default=3)
    ap.add_argument("--scales", default="0.08-1.3", help="range of rpm-per-count to try")
    args = ap.parse_args()
    first, last = (int(x) for x in args.bytes.split("-"))
    lo, hi = (float(x) for x in args.scales.split("-"))

    t, raw = read_field(args.capture, args.id.upper(), first, last)
    if not len(t):
        sys.exit(f"no {args.id} frames in {args.capture}")
    x = read_audio(args.audio)
    f, tt, Z = stft(x, RATE, nperseg=WINDOW, noverlap=WINDOW - 1024, nfft=PAD * WINDOW)
    f = f[f < 1500]
    P = np.log(np.abs(Z[:len(f)]) + 1e-9)
    W = P - median_filter(P, size=(301, 1))                 # each line's height above its surroundings
    df = f[1] - f[0]

    r = np.interp(args.start + tt, t, raw, left=0, right=0)
    running = r > 0
    cols = np.flatnonzero(running)
    if len(cols) < 20:
        sys.exit("the field is zero for the whole recording: wrong --start, or the engine was off")
    per_rev = args.cylinders / 2 / 60                       # firings per second, per rpm

    def height(scale, order):
        idx = np.rint(order * r[running] * scale * per_rev / df).astype(int)
        return np.where(idx < len(f), W[np.minimum(idx, len(f) - 1), cols], 0).mean()

    scales = np.arange(lo, hi, 0.0005)
    score = np.array([np.mean([height(k, n) for n in range(1, 11)]) for k in scales])
    best = scales[score.argmax()]
    near = scales[score >= score.max() - 0.1 * (score.max() - np.median(score))]
    near = near[np.abs(near - best) < 0.02]

    rpm = r[running] * best
    print(f"audio {len(x) / RATE:.1f} s, engine running for {running.mean() * 100:.0f}% of it")
    print(f"raw {r[running].min():.0f}..{r[running].max():.0f}, median {np.median(r[running]):.0f}")
    print(f"best scale {best:.4f} rpm per count  (within a tenth of the peak: {near.min():.4f}-{near.max():.4f})")
    print(f"   that is {np.percentile(rpm, 5):.0f}..{np.percentile(rpm, 95):.0f} rpm for the middle 90% of the recording,")
    print("   and the scale is only tested over that span")
    print("runners-up:")
    seen = [best]
    for i in np.argsort(score)[::-1]:
        if all(abs(scales[i] - s) > 0.008 for s in seen):
            seen.append(scales[i])
            print(f"   {scales[i]:.4f}  score {score[i]:.2f} against {score.max():.2f}")
        if len(seen) == 5:
            break
    print("line height by order of crankshaft rotation, at the best scale (firing orders marked):")
    step = 1 / args.cylinders
    for n in np.arange(step, 4 + 1e-9, step):
        order = n * args.cylinders / 2
        firing = abs(n - round(n)) < 1e-9
        print(f"   {order:4.1f} {'*' if firing else ' '} {height(best, n):+.2f}")


if __name__ == "__main__":
    main()
