# analysis/ — turning a capture into a confirmed mapping

Everything about **reading the bus and working out what the bytes mean** lives here, the
way the sibling `bluecan/` folder holds everything about the device that streams to
RaceChrono. The two halves meet at the CAN bus and nowhere else: that one is telemetry
over Bluetooth, this one is a laptop, a CANable and a log file.

**This folder is self-contained on purpose.** Nothing in it links or shells out to the
rest of the repository, so a session spent on the analysis reads only what is in here.
Where a fact from elsewhere mattered, it is written out here rather than pointed at.
(Since 2026-10-12 a few pages link out to the front page and to `bluecan/`; the tools
still depend on nothing outside this folder.)

The chain below is proved end to end against a synthetic bus whose answer was chosen in
advance. The diagnostic-connector pigtail that blocked the bike arrived on 2026-09-21, and
[at-the-bike.md](at-the-bike.md) is the first session as a checklist.

That session has since happened, three times over on 2026-10-11, and
[r9-signals.md](r9-signals.md) holds what they found. The next one, with the rear
wheel on a stand, is [stand-session.md](stand-session.md).

## The order of operations

The numbered scripts are the whole workflow, and the numbers are when they run rather
than how important they are.

| | | Needs |
|---|---|---|
| [`tools/0-setup.sh`](tools/0-setup.sh) | build `.venv` | once, at a desk |
| [`tools/1-selftest.sh`](tools/1-selftest.sh) | prove the chain works, on a virtual bus | nothing — no adapter, no bike |
| [`tools/2-session.sh`](tools/2-session.sh) | the session at the bike: capture, tag, convert | the bike and a CANable |
| [`tools/3-candump-to-webcan.py`](tools/3-candump-to-webcan.py) | convert a log the CSS scripts can read | only on its own, for a log captured some other way — step 2 runs it for you |
| [`tools/4-lean-from-video.py`](tools/4-lean-from-video.py) | read a lean angle off a screen recording of a phone's level app | the video; OpenCV, so `0-setup.sh --with-vision` |
| [`tools/5-rpm-from-audio.py`](tools/5-rpm-from-audio.py) | find the scale of an engine-speed field from a recording of the engine | the capture, the audio, and `ffmpeg` |

[`tools/can-up-listen-only.sh`](tools/can-up-listen-only.sh) is not a step. It is the one
privileged thing a session does, split out so `sudo` covers that line and nothing else,
and step 2 calls it for you.

[`tools/can-up-ack.sh`](tools/can-up-ack.sh) is its counterpart for `2-session.sh --ack`,
where the adapter acknowledges frames, and it is never run by hand: it will not start
without a session to watch, and it takes the interface down when that session ends.
[at-the-bike.md](at-the-bike.md) says when that mode is worth it.

[`tools/bootstrap-wsl.sh`](tools/bootstrap-wsl.sh) is not a step either, and runs before
there is a checkout to run a step from: on a fresh WSL2 Ubuntu it installs the packages
the chain needs, Claude Code and `gh`, authenticates git, clones this repository and
hands over to `0-setup.sh`. It installs nothing for the bluecan half. Since
2026-10-12 the repository is public, so the script can be fetched on its own; the
command is in its header.

Then the analysis itself, which is not one of ours: `survey` → `correlate` → `bitsearch` →
`build_dbc` / `calibrate` → `verify`, from [`css-skill/`](css-skill/). It is a
conversation rather than a script — you pick an id, look at what came back, and decide
what to refine — which is why there is no `4-analyse.sh`.
[`tools/1-selftest.sh`](tools/1-selftest.sh) runs that chain end to end with every
argument spelled out, so it doubles as the worked example of how to drive it.

```bash
cd analysis && ./tools/0-setup.sh && ./tools/1-selftest.sh
```

## What is speculation and what is measured

This distinction is the point of the folder, and it is worth keeping sharp.

- [`r9-signals.md`](r9-signals.md) and [`r9.dbc`](r9.dbc) — **measured on the R9.** What
  every id on the diagnostic connector carries, each row with what it was held against and
  how many times. The evidence is in the write-up beside each log under
  [`captures/`](captures/README.md).
- [`open-questions.md`](open-questions.md) — what is left, and
  [`R9_CAN_unknowns_vs_manual.md`](R9_CAN_unknowns_vs_manual.md), the **candidates** for the
  unnamed fields read out of the service manual. Candidates, graded by their own author,
  and checked against the captures only where it says so.
- [`connector.md`](connector.md) — the connector and its pins, metered on the bike.
- [`R9_CANbus.md`](R9_CANbus.md) — the R9 service manual's CAN wiring, summarised: three
  pairs, and the diagnostic connector on the one with only the ECU on it. Not speculation
  where it quotes the manual; its deductions are marked.
- [`stand-session.md`](stand-session.md) and [`at-the-bike.md`](at-the-bike.md) — the
  checklists: the next session, and the first one, whose parts A and B are still how the
  laptop and adapter are set up.
- [`background/`](background/) — **from before the bike, and mostly prediction.**
  [`riding-data-source.md`](background/riding-data-source.md) is Yamaha's own list of what
  Y-TRAC Rev displays, 27 items, not speculation.
  [`pre-bike-mapping.md`](background/pre-bike-mapping.md) is the mapping predicted from
  other Yamahas, with 🔴 🟡 🟢 markers and the source list; the ids it predicted are not on
  this connector. [`analysis-tips.md`](background/analysis-tips.md) is method: how to
  excite a signal so it can be identified, how to annotate a capture, and the failure
  modes two outside sources ran into.
- [`sources/aim-yamaha/`](sources/aim-yamaha/) — AiM's logger protocols for the R9's
  platform siblings, as DBC, and two of its user guides. The strongest outside source and
  still not an R9: kept here against link rot, with the reasons to doubt them written up in
  [pre-bike-mapping.md](background/pre-bike-mapping.md#how-much-to-trust-the-aim-reading).

## `css-skill/` — vendored, not ours

[CSS-Electronics/can-bus-reverse-engineering-skills](https://github.com/CSS-Electronics/can-bus-reverse-engineering-skills),
MIT, copied in at the commit recorded in [`css-skill/PINNED-COMMIT`](css-skill/PINNED-COMMIT).
Vendored for the same reason the AiM DBCs are: a session should not depend on a checkout
somewhere else on the machine, or on the upstream still being there.

Three skills ship in it. Only the first matters here:

- **`cansub-reverse-engineering`** — the chain above. Works from an existing log with a
  reference; the reference can be a human's tags, another decoded signal in the same log,
  or OCR of a video of the dash.
- **`cansub-knowledge`** — reference for their own hardware. Not applicable.
- **`combine-dbc`** — merges per-signal DBCs into one.

They are **not active** in this repository: Claude Code looks in `.claude/skills/` at the
root, not in a nested copy. To switch them on, symlink them there. Left off by default
because it changes what every session in this repo can do.

Only `capture.py` and `detect_bus.py` are tied to their hardware, which is the whole
reason this works — verified rather than assumed, by
[`tools/1-selftest.sh`](tools/1-selftest.sh) running the chain end to end on a virtual
bus and asserting the answer it already knew.

## What is deliberately not here

The bench half of the project has a fuller `can-up.sh` covering loopback, normal and
one-shot modes, and a `sweep.sh` that puts a known signal on a bench bus. Neither is used
here, and that is the point rather than an oversight.

- **The bring-up is a copy, not a shared script, because the settings are opposite in the
  two places.** On the desk the adapter must ACK the lone reader; on the bike it must not
  ACK anything. One script with a flag is one flag away from being wrong on the bus shared
  with the ECU, ABS and IMU, so the copy here knows only listen-only and exits rather than
  fall back.

  *That was the reasoning, and the bike did not fit it.* The R9's diagnostic connector is a
  pair with only the ECU on it ([R9_CANbus.md](R9_CANbus.md)), and unless something
  acknowledges, it gives two ids of 23. So there is a second bring-up after all,
  [`tools/can-up-ack.sh`](tools/can-up-ack.sh) — still a separate script, still not a
  flag on this one, started only by `2-session.sh --ack` after the bit rate has been
  proved listen-only, and watched by a root watchdog for as long as it runs.
- **The bench sweep is the wrong fixture for testing analysis.** It carries one ramp four
  ways, which produces a nine-way tie no rank-based scorer can break — found the hard way.
  `1-selftest.sh` builds its own single-field frame with non-tracking decoys instead.
