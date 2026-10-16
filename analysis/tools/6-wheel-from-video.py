#!/usr/bin/env python3
"""Read a wheel's revolutions per second off a video of it turning, and hold a field against it.

    .venv/bin/python tools/6-wheel-from-video.py PXL_20261016_171946620.TS.mp4 \\
        --start 1792171186.620 --from 62 --to 75.5 -o wheel-from-video.csv \\
        --log captures/session-2026-10-16_191234/capture.log.xz

Why this exists: the dash speed is one rounded number, read late, from a wheel nobody has
named. The wheel itself is the reference. A camera standing still beside it sees the same
picture again every time the wheel has gone round once, so the frame lag at which a patch
of the tyre's sidewall matches itself best is one revolution, and 30 frames a second
divided by that lag is revolutions per second. No mark has to be tracked: the lettering on
the sidewall makes one turn look different from a fraction of a turn. On 2026-10-16 that
gave 0x756 at 700 counts per revolution per second, which is 0.01 km/h a count on a
1.944 m wheel.

What it can and cannot tell you:

  the speed has to hold for a window (--win, 1.5 s by default). A slowly changing speed
             is averaged over it; a step inside it gives a lag that is neither.
  --lmin/--lmax bound the lag searched, in frames. They must include one revolution and
             exclude half of one: a wheel with spokes looks nearly the same after a
             fraction of a turn, and with the bound too low the answer is that fraction.
             At 2.3 rev/s and 30 fps one turn is 13 frames, so 7-20; at 0.2 rev/s it is
             about 140, and 80-300 works. The contrast column (median difference over the
             best one) says how sharp the match was: under about 1.5, distrust the row.
  above about 7 rev/s (50 km/h on this wheel) one turn is under 4 frames at 30 fps and
             the method has nothing to work with. A slow-motion video moves that limit up.
  --start    is the epoch of the first frame. A Pixel names the file with when the
             recording began, in UTC, to the millisecond, and creation_time is when it
             ended. It is the phone's clock. The bus is lined up against the video by when
             the wheel starts and stops, which on 2026-10-16 agreed to half a second.
  --box      is the patch, x0 x1 y0 y1 in a 540x960 frame (the video as displayed, scaled).
             It has to be on the tyre and wheel and clear of anything that does not turn.

With --log, each window also gets the mean of 0x756 (b1 << 8 | (b0 & 0xF0) | b2 >> 4) and
the pulses counted in 0x27C byte 2 over the same stretch of the capture, and their ratios
to the video's revolutions.

Needs ffmpeg.
"""

import argparse
import lzma
import subprocess
import sys

import numpy as np

W, H = 540, 960


def frame_times(video):
    out = subprocess.run(["ffprobe", "-v", "error", "-select_streams", "v:0", "-show_entries",
                          "frame=pts_time", "-of", "csv=p=0", video],
                         capture_output=True, text=True, check=True).stdout
    return np.array([float(x.strip(",")) for x in out.split()])


def patches(video, box):
    x0, x1, y0, y1 = box
    p = subprocess.Popen(["ffmpeg", "-v", "error", "-i", video, "-vf", f"scale={W}:{H}",
                          "-f", "rawvideo", "-pix_fmt", "gray", "-"], stdout=subprocess.PIPE)
    out = []
    while True:
        b = p.stdout.read(W * H)
        if len(b) < W * H:
            break
        f = np.frombuffer(b, np.uint8).reshape(H, W)
        out.append(f[y0:y1:2, x0:x1:2].astype(np.float32).ravel())
    return np.stack(out)


def period(X, i0, i1, lmin, lmax):
    """Lag in frames, interpolated, at which the patch repeats best, and the contrast."""
    d = np.array([np.abs(X[i0 + L:i1 + L] - X[i0:i1]).mean() for L in range(lmin, lmax + 1)])
    k = int(np.argmin(d))
    lag = float(lmin + k)
    if 0 < k < len(d) - 1:
        y0, y1, y2 = d[k - 1], d[k], d[k + 1]
        lag += 0.5 * (y0 - y2) / (y0 - 2 * y1 + y2)
    return lag, np.median(d) / d[k]


def read_wheels(path):
    """Times and values of the 0x756 field, and times and counts of 0x27C byte 2."""
    opener = lzma.open if path.endswith(".xz") else open
    r, p = [], []
    with opener(path, "rt") as f:
        for line in f:
            stamp, _, frame = line.split()
            cid, data = frame.split("#")
            if cid == "756":
                b = bytes.fromhex(data)
                r.append((float(stamp[1:-1]), b[1] << 8 | (b[0] & 0xF0) | b[2] >> 4))
            elif cid == "27C":
                p.append((float(stamp[1:-1]), bytes.fromhex(data)[2]))
    return np.array(r), np.array(p)


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("video")
    ap.add_argument("--start", type=float, required=True, help="epoch of the first frame")
    ap.add_argument("--from", dest="t0", type=float, required=True, help="seconds into the video")
    ap.add_argument("--to", dest="t1", type=float, required=True)
    ap.add_argument("--win", type=float, default=1.5, help="window, seconds (default 1.5)")
    ap.add_argument("--step", type=float, default=0.5, help="step between windows (default 0.5)")
    ap.add_argument("--lmin", type=int, default=7)
    ap.add_argument("--lmax", type=int, default=20)
    ap.add_argument("--box", type=int, nargs=4, default=[220, 470, 330, 900], metavar=("X0", "X1", "Y0", "Y1"))
    ap.add_argument("--log", help="candump log, plain or .xz, to hold 0x756 and 0x27C byte 2 against")
    ap.add_argument("--min-contrast", type=float, default=1.7, help="rows below this are left out of the summary")
    ap.add_argument("-o", "--out", default="wheel-from-video.csv")
    a = ap.parse_args()

    t = frame_times(a.video)
    X = patches(a.video, a.box)
    t = t[:len(X)]
    fps = (len(t) - 1) / (t[-1] - t[0])
    wheels = read_wheels(a.log) if a.log else None

    rows = []
    for s in np.arange(a.t0, a.t1 - a.win + 1e-9, a.step):
        i0, i1 = np.searchsorted(t, [s, s + a.win])
        if i1 + a.lmax >= len(X):
            break
        lag, contrast = period(X, i0, i1, a.lmin, a.lmax)
        mid = s + a.win / 2
        row = [a.start + mid, mid, fps / lag, lag, contrast]
        if wheels is not None:
            e0, e1 = a.start + s, a.start + s + a.win
            r, p = wheels
            rr = r[(r[:, 0] >= e0) & (r[:, 0] < e1), 1]
            pp = p[(p[:, 0] >= e0) & (p[:, 0] < e1), 1]
            row += [rr.mean(), pp.sum(), rr.mean() / row[2], pp.sum() / (row[2] * a.win)]
        rows.append(row)

    head = "epoch;video_s;rev_per_s;lag_frames;contrast"
    if wheels is not None:
        head += ";r756_mean;pulses_27C;r756_per_rev_per_s;pulses_per_rev"
    with open(a.out, "w") as f:
        f.write(f"# wheel revolutions read off {a.video.split('/')[-1]} by tools/6-wheel-from-video.py\n")
        f.write(f"# {' '.join(sys.argv[1:])}\n")
        f.write(f"# epoch = {a.start} (the phone's clock, from the file name) + seconds into the video\n")
        f.write(head + "\n")
        for row in rows:
            f.write(";".join(f"{x:.3f}" for x in row) + "\n")

    good = [r for r in rows if r[4] >= a.min_contrast]
    print(f"{len(rows)} windows, {len(good)} with contrast >= {a.min_contrast}; written to {a.out}")
    if good and wheels is not None:
        k = np.array([r[7] for r in good])
        n = np.array([r[8] for r in good])
        print(f"0x756 per rev/s: mean {k.mean():.1f}, sd {100 * k.std() / k.mean():.2f} %")
        print(f"pulses per revolution: mean {n.mean():.2f}, sd {100 * n.std() / n.mean():.2f} %")
        print(f"as 0.01 km/h a count, the wheel's circumference would be {k.mean() / 360:.3f} m")


if __name__ == "__main__":
    main()
