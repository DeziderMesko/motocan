# captures/

One folder per session at the bike. All three so far are from 2026-10-11, on the R9's
diagnostic connector at 500 kbps, at a standstill.

| Session | What it was | What it found |
|---|---|---|
| [session-2026-10-11_115626](session-2026-10-11_115626/README.md) | listen-only; gears, TCS, ride modes, engine run | two ids only, one of them resent thousands of times a second because nothing acknowledged it; gear and TCS |
| [session-2026-10-11_130905](session-2026-10-11_130905/README.md) | the adapter acknowledging; engine off | 23 ids; the throttle grip and the valve |
| [session-2026-10-11_200550](session-2026-10-11_200550/README.md) | acknowledging; engine started, with a sound recording and a video of a lean-angle app | engine speed and its scale, both temperatures, grip against valve; lean and the rider-aid settings are not on this connector |

In each folder:

- `capture.log.xz` — the raw log in `candump -L` format, one frame a line:
  `(epoch seconds) can0 27C#00000006400030`. Unpack with `xz -dk`.
- `ref.csv` — the marks made during the session: what was done or what the dash showed,
  with the time. This is the reference the frames are held against.
- `notes.txt` — the session script's own summary: mode, id census, the marks in order.
- `SHA256SUMS` — hashes of the files as they came off the laptop, including media that is
  not in the repository.
- `README.md` — the write-up.

The current state of every signal is not here but in [r9-signals.md](../r9-signals.md).
