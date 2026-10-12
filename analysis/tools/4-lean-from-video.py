#!/usr/bin/env python3
"""Read a lean angle off a screen recording of a phone's level app.

    .venv/bin/python tools/4-lean-from-video.py screen-20261011-201115-1791742211459.mp4 \\
        --start 1791742211.459 -o lean-from-video.csv

Why this exists: 2-session.sh has no `value` kind because a rider cannot produce a
continuous reference by hand. A phone lying on the bike can. The app used on 2026-10-11
fills half the screen with colour and tilts the edge of it with the phone, so the angle is
the slope of that edge and no digit has to be read: the number the app prints agreed with
the slope to 0.05 deg in six frames checked. That is the only app this was written
against. A different one needs --check first, which writes a few frames with the fitted
edge drawn on them and the angle in the corner, to hold against the number on screen.

What it assumes, and gets wrong silently if untrue:

  the edge   runs top to bottom, colour on the left and white on the right, which is the
             app held in portrait. Rows where that is not so are skipped, and a frame
             with too few rows left is written without an angle.
  --start    is the epoch of the first frame. Android names the file with the time the
             recording ENDED and, after it, the start in epoch milliseconds; the first
             is the one that looks like a clock time and the second is the one wanted.
             It is the phone's clock, not the laptop's, and nothing here reconciles them.

Output is epoch;video_s;lean_deg, one row for every --every'th frame.

Needs OpenCV, which only `0-setup.sh --with-vision` installs.
"""

import argparse
import sys

import cv2
import numpy as np


def edge_angle(frame, rows):
    """Slope of the colour/white edge in degrees, and the worst residual of the fit in px."""
    px = frame[rows].astype(int)
    coloured = (px.max(axis=2) - px.min(axis=2)) > 60      # blue when tilted, green near level
    x = np.argmin(coloured, axis=1).astype(float)          # first white column in each row
    good = coloured[:, 0] & ~coloured[:, -1]
    if good.sum() < 20:
        return None
    r, xx = rows[good], x[good]
    for _ in range(3):                                      # the app's tick marks cross the edge
        k, q = np.polyfit(r, xx, 1)
        d = np.abs(k * r + q - xx)
        keep = d <= max(3.0, np.median(d) * 3)
        r, xx = r[keep], xx[keep]
    k, q = np.polyfit(r, xx, 1)
    return -np.degrees(np.arctan(k)), np.abs(k * r + q - xx).max(), k, q


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("video")
    ap.add_argument("-o", "--out", default="lean-from-video.csv")
    ap.add_argument("--start", type=float, default=0.0, help="epoch of the first frame")
    ap.add_argument("--every", type=int, default=3, help="use every Nth frame (default 3)")
    ap.add_argument("--check", metavar="DIR", help="also write six annotated frames here")
    args = ap.parse_args()

    cap = cv2.VideoCapture(args.video)
    if not cap.isOpened():
        sys.exit(f"cannot open {args.video}")
    height = int(cap.get(cv2.CAP_PROP_FRAME_HEIGHT))
    total = int(cap.get(cv2.CAP_PROP_FRAME_COUNT))
    # the middle of the screen: clear of the status bar, the app's buttons, and the
    # two tick marks that sit at mid-height
    rows = np.arange(int(height * 0.125), int(height * 0.77), 10)
    rows = rows[np.abs(rows - height * 0.446) > 40]
    check_at = set(np.linspace(0, max(total - 1, 0), 6).astype(int) // args.every * args.every)

    n = missed = 0
    worst = 0.0
    with open(args.out, "w") as out:
        out.write(f"# lean angle read off {args.video.split('/')[-1]} by tools/4-lean-from-video.py\n")
        out.write("epoch;video_s;lean_deg\n")
        i = 0
        while True:
            ok, frame = cap.read()
            if not ok:
                break
            t = cap.get(cv2.CAP_PROP_POS_MSEC) / 1000
            if i % args.every == 0:
                fit = edge_angle(frame, rows)
                if fit is None:
                    missed += 1
                else:
                    angle, resid, k, q = fit
                    worst = max(worst, resid)
                    out.write(f"{args.start + t:.3f};{t:.3f};{angle:.2f}\n")
                    n += 1
                    if args.check and i in check_at:
                        p0, p1 = (int(q), 0), (int(k * (height - 1) + q), height - 1)
                        cv2.line(frame, p0, p1, (0, 0, 255), 3)
                        cv2.putText(frame, f"{angle:.2f}", (30, 90), 0, 3, (0, 0, 255), 6)
                        cv2.imwrite(f"{args.check}/frame-{i:06d}.png", frame)
            i += 1
    print(f"{n} readings, {missed} frames with no edge, worst fit residual {worst:.1f} px -> {args.out}")
    if worst > 5:
        print("a residual above a few px means the edge was not a straight line somewhere: look with --check")


if __name__ == "__main__":
    main()
