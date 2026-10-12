# Tips for the first logging session

Practical technique for turning a raw capture into a confirmed mapping, gathered from two
outside sources on 2026-08-22 and kept separate from
the project's task list because it is method rather than plan.
Where a tip contradicts something already written here, this file says so.

Provenance labels **[T1]** and **[T2]** are local to this file; the mapping table's
`[S1]`–`[S7]` are unrelated. Both sources are about cars. Nothing below had been tried
against the R9 when it was written, and the two differ sharply in weight — see
[Sources](#sources). Three sections have a note dated 2026-10-11 saying what the bike did
to them.

## The finding that changes the plan: [T2]'s analysis chain is not hardware-locked

[T2] ships an MIT-licensed Claude Code skill — survey, correlate, bitsearch, calibrate,
build_dbc, verify — advertised for the vendor's own CANsub interface. **Only `capture.py`
and `detect_bus.py` actually touch that hardware.** Everything downstream reads a plain
semicolon CSV, checked in `common.py`'s `load_trace()`:

```
TimestampEpoch;BusChannel;ID;IDE;DLC;DataLength;Dir;EDL;BRS;ESI;RTR;DataBytes
```

So a converter from a CANable capture into that header is a short script, and the whole
chain then runs on our own logs. This retires most of the adjudication tooling this
project had planned to write itself: `correlate.py` and `bitsearch.py` settle rival
candidate fields better than a hand-rolled scorer would.

**Verified on 2026-08-23, and the answer to both halves is no problem.**
[`tools/1-selftest.sh`](../tools/1-selftest.sh) plants a signal on a virtual bus whose
answer is chosen in advance and runs the whole chain against it: correlate returns 1.0
against a 0.93 runner-up, bitsearch 1.000 against 0.106, `build_dbc` fits scale 1 offset
0 at R²=1.0000, and verify passes both halves of its gate.
The converter is
[`candump-to-webcan.py`](../tools/3-candump-to-webcan.py) and is about as
small as it looks; the whole chain runs on a classic-CAN log and returns the right field
with a wide margin. Nothing in a 500 kbps classic log confused a script that expects CAN
FD — the payload is packed as an arbitrary-precision int either way, so eight bytes is
just the small case.

One thing did stop everything, and it is not in the article: their `requirements.txt` says
python-can and python-can-cansub are "not needed for the offline-log workflow", and
`scripts/common.py` then imports `python_can_cansub` at module level, so every offline
script fails on import without it. Both install from PyPI with no hardware attached.

## Excite the signal twice, and for different reasons

The single most useful thing in either source, and it corrects an earlier plan here.
Quoting [T2]'s methodology:

> a few **discrete holds** let counters spuriously rank-match the monotone step pattern
> (correlate goes degenerate — many candidates tie at ~1.0, which is how a wrong field can
> be chased)

- **Sweep to identify.** A smooth, continuous ramp across the signal's range. On a ramp the
  true field scores ~1.0 while rolling counters and unrelated traffic fall to ~0. The
  transitions also recover the field's *full width* — discrete holds reveal only the MSB and
  under-read the field as a narrow high-bit slice.
- **Holds to calibrate.** Settle at known values and tag each one. Low-noise held data is
  what scale and offset should be fitted to.

For the R9: rolling the throttle smoothly up and down is the identifying excitation for
TPS, APS and RPM. Gear positions are holds — and since there is nothing to sweep on a
gearbox, the counter-collision risk above applies to the gear row in full and has to be
watched for by hand.

**Both halves were reproduced on 2026-08-23** on a synthetic bus, which is the only place
the true answer is known in advance —
[`analysis-selftest.sh`](../tools/1-selftest.sh). Two results are
worth carrying to the bike. The degeneracy is real and it does not need a counter to
appear: a frame carrying the same value in several encodings produced a **nine-way tie at
0.9998**, because no rank-based scorer separates a value from its own affine image — so
`0x205` vs `0x264` for wheel speed will not be settled by score alone if one turns out to
be derived from the other. And a **symmetric** sweep makes the lag unidentifiable, since
its two halves shift in opposite directions; the reported lag was −0.4 s for a reference
with none, and using it cost R² 0.9976 → 0.9494. Roll the throttle up quickly and back
down slowly, or the other way round — but not evenly.

Two more rules from the same section: when one run is ambiguous, capture **several sweeps
with different excitation** and require the same field to win in all of them; and
2–3 collinear hold points fit any line with R²≈1, so a calibration must always be
validated against the *moving* data, not just the points it was fitted to.

## A marker is not one kind of thing

[T2]'s sidecar annotation format is `epoch;kind;label;value`, with three kinds, and it is
better than the single event marker planned here:

| kind | meaning | used for |
|---|---|---|
| `event` | a discrete moment | "shifted now" |
| `value` | a continuous reference sample | tracking a sweep |
| `anchor` | a **known physical value** at a time | "this is 4000 rpm" — feeds calibration |

`anchor` is the one worth stealing outright: it is how "hold at a known value" gets
expressed at all. Their `sample_hold()` interpolates the step series forward, so human
reaction lag is inside the model rather than pretended away.

## Verification is a gate, not a formality

> A candidate can correlate with the reference and even decode correctly at a few
> calibration points while being the **wrong encoding** — a scrambled but deterministic
> function of the true field. The classic tell: it matches when the signal is **parked**
> but **diverges in motion**. — [T2]

Their `verify.py` scores parked and moving segments separately and returns PASS or
UNCONFIRMED, with the explicit instruction not to rationalise a low score as reference
noise. Adopt the discipline even if the script is not used: a mapping that only agrees at
the points it was calibrated on has not been confirmed.

## Are you decoding the signal, or the dashboard's opinion of it?

Both sources land on this independently, and it is the failure most likely to bite the R9.

[T1]'s first ever decode was the gear selector — and it turned out to be **what the
instrument cluster displays**, not what the gearbox reports. Two different signals on the
same bus. That maps straight onto the `0x236` gear row in
[pre-bike-mapping.md](pre-bike-mapping.md#mapping-table): agreement with the dash is not proof
that the frame *is* the gearbox, because the dash is exactly what a display-facing frame
would agree with. A bike with a quickshifter has a real reason to carry both.

[T1] also caught the reverse case from the decoded shape: the electric park brake frame
had a `moving` state, which a simple switch would not — so the frame was the actuator's
position, not the switch. **The set of states a field takes is evidence about which node
is talking.**

## The signal may not be on this bus at all

[T1] spent the first third of a session hunting a park brake switch that was on LIN, not
high-speed CAN. What resolved it was having the **workshop manual** to hand, which named
which module lives on which bus.

For the R9 this is the standing explanation for the 🔴 rows — Fuel, EBM, BSR — that no
amount of analysis will extract from a capture that does not contain them. It is also an
argument for getting the R9's service documentation, which we do not have.

*2026-10-11: now we do, and it was exactly this.* [R9_CANbus.md](../R9_CANbus.md) summarises
the service manual: three CAN pairs, the IMU and ABS on one, the meter on another, and the
diagnostic connector on a third with only the ECU. Lean, SCS, LIF and QS were hunted
through three captures on the wrong pair before the manual said so.

## Prove the bus is alive before anything else

[T1] wakes the bus (hazard lights), captures for 5 s, counts frames — 3445 — and only then
starts. [T2] does the same thing in `detect_bus.py`, and additionally reads state,
frame-rate and RX/TX error counters, warning on bus-off, error-passive, idle or erroring:
a fast way to catch a wrong bit-rate, a wrong mode, or a wiring/termination fault before
burning a capture on it.

## The operator is the weakest component

[T1]'s park-brake capture failed. Not the analysis — the recording. In his own words,
*"I was starting to get confused there, honestly. The beeping was going on."* He then found
the same signal by hand in SavvyCAN in about two minutes.

The lesson for the prompting script: keep the per-step load on the rider low, make the
steps unambiguous, and provide a way to **mark a botched cycle and repeat it** rather than
letting it quietly poison the log. [T2]'s answer to the same problem is statistical —
lag search plus rank-based, outlier-robust scoring, on the stated assumption that human
reference input contains reaction lag, wrong inputs and gaps.

## Verification is a gate — and the parked half is the half that catches it

Worth reading twice alongside the section above, because the first fixture built here
missed it. A sweep-only run made `verify.py` report `parked Spearman = n/a (n=0)`: the
signal never sits still, so the gate scored the moving segments and silently skipped the
steady ones — and the steady ones are where the failure it exists to catch shows up, since
a wrong-but-deterministic encoding is exactly the thing that agrees while parked and
diverges in motion. A PASS with an empty parked column is the weaker claim wearing the
stronger claim's word. **Check the sample counts, not just the verdict.**

## Build the throwaway confirmation UI

[T1]'s most transferable habit: after a candidate decode, put a small live gauge on screen
and move the physical thing. He confirms a steering-angle decode in seconds by turning the
wheel and watching the needle.

For us this is better than round-tripping through RaceChrono channel entry, and it is the
natural place to show **rival candidates side by side** while blipping the throttle — the
conflict adjudicator, live. It also does not need the phone or Bluecan involved.

## Listen-only, and never while moving

[T2]'s default profile is SILENT / `listen_only`, and their reasoning names our exact case:

> For any existing active multi-node bus (car, truck, **bike**, machine). Other ECUs
> provide the ACK; the CANsub transmits nothing. **Use this whenever unsure.**

Their skill never transmits data frames at all — replay and injection are deliberately out
of scope, and bit-rate probing is passive. They also state plainly: never reverse-engineer
on a moving vehicle. That matches the stance already taken here, from an independent
direction.

Note the asymmetry with this project's own bench: on the desk the CANable must ACK the
lone reader, on the bike it must not. Same adapter, opposite setting, and the bike is the
case where getting it wrong touches something that matters. That is why the bring-up used
here, [`tools/can-up-listen-only.sh`](../tools/can-up-listen-only.sh), knows only the one
mode and refuses to continue if the interface does not report it.

*2026-10-11: [T2]'s premise — "other ECUs provide the ACK" — is false on the R9's
diagnostic connector.* Nothing else is on that pair, so a listen-only adapter left the ECU
resending one frame 3 800 times a second and showed two ids of 23. The sessions since run
with `2-session.sh --ack`: one ACK bit per frame, no data frames ever, a watchdog that
takes the adapter off the bus if a single frame is transmitted. "Never while moving" stands.

## Filming the dash is a real reference channel, not a fallback

[T2] implements it — `vision_reference.py`, OCR over video frames, producing a timestamped
reference series. It is their second of three reference methods, ahead of human input.

The R9's dash shows gear, RPM, speed and temperature digitally, which is the good case for
OCR. Their caveats: a stable camera and darker conditions help, and the recording has to
share a clock with the capture.

## What not to take from these

- **[T1] deliberately withholds DBC files** from the model, preferring to let it "learn"
  the car. He has no DBC for his vehicle; we have AiM's protocol for the R9's own platform
  in [sources/aim-yamaha/](../sources/aim-yamaha/README.md). Our first session is confirming
  or breaking a specific prediction, not starting from nothing.
- **[T2]'s cleanest reference is a decoded OBD2 signal from the same log** (correlation
  ≈0.99 against ≈0.55 for a hand-driven ramp). OBD2 PIDs are request/response, which means
  transmitting — in tension with listen-only on a live bike bus. How they reconcile that is
  not stated in the article, so treat this path as an open question, not an option.
- **[T2]'s headline numbers** — "90%+ of signals", "95%+ time reduction", "5–10 min per
  signal" — are vendor marketing for a paid interface and are not independently checked.
  The methodology document reads like engineering rather than marketing, and argues against
  self-deception in a way marketing does not, which is the reason to take the method
  seriously while leaving the numbers alone.
- **[T1]'s headline** oversells too: of his two on-camera decodes, the AI-assisted park
  brake **failed** and manual SavvyCAN was faster. The workflow is worth copying; the
  framing is not evidence.

## Incidental, but it settles an open question

[T1] confirms in practice what the plugin's documentation implies: a candleLight adapter
does not work with SavvyCAN out of the box on Windows, and the fix is dropping in the
Qt SerialBus DLL, after which it appears under **QT SerialBus devices** and uses
**gs_usb**. Two independent confirmations that this path works, which matters because
the laptop that will be at the bike runs Windows.

## Sources

- **[T1]** ["HOW TO DECODE CANBUS WITH AI"](https://www.youtube.com/watch?v=GlQO8fLgHi4)
  — Jordans Garage, 2026-05-25, 28:03. Hobbyist decoding a Range Rover L494 for an LS swap,
  using a DSD TECH SH-C30A (candleLight), Claude Code over Tailscale, and SavvyCAN.
  **Read as an auto-generated transcript only** — the screen was never seen, so any number
  he did not say aloud is not in evidence, and the ones he did are Range Rover values.
  Useful for workflow habits and failure modes, not for technique depth.
- **[T2]** CSS Electronics, ["AI CAN Bus Reverse Engineering (Claude Code)"](https://www.csselectronics.com/pages/can-bus-reverse-engineering-ai-llm-claude)
  — article plus a [31:24 video](https://www.youtube.com/watch?v=jQDqWuL5-nQ) by Martin
  Falch, 2026-07-06, and the open-source skill at
  [CSS-Electronics/can-bus-reverse-engineering-skills](https://github.com/CSS-Electronics/can-bus-reverse-engineering-skills)
  (MIT). The quotations above are from the repository's own
  `references/re-methodology.md` and its script sources, not from the marketing page.
  Retrieved 2026-08-22. Vendor content for a paid CAN interface — the method is strong,
  the performance claims are unverified.
