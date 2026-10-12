# The first capture, 2026-10-11

> The write-up of this session, as it was written on the day. The current state of every
> signal is in [r9-signals.md](../../r9-signals.md); where a later session corrected
> something here, that page has the correction.

[`captures/session-2026-10-11_115626/`](./): the CANable
on the diagnostic connector, listen-only, 500 kbps, 11:56:47 to 12:08:12. 2 599 203
frames, 39 marks. `capture-changes.candump` beside the full log is the same capture
with the repeats taken out — the 530 frames where an id's payload changed — and is the
one to open for the contents; the timing argument below needs the full log. The engine start was not marked; the data puts it at 12:07:23, and the
rider confirms it: key on and engine off through the first temperature anchors (12:07:00
and 12:07:07, 16 °C each), then started so the water would begin to warm.

**The predicted profile is not on this connector.** The census is two IDs, and neither is
one anybody named:

| ID | DLC | Frames | Share |
|---|---|---|---|
| `0x27C` | 7 | 2 593 940 | 99.8% |
| `0x751` | 8 | 5 263 | 0.2% |

None of `0x20A`/`0x216`/`0x236`/`0x23A`/`0x23E`, and none of the R1-style set either.

**Nearly all of it is one frame being retransmitted because nothing acknowledges it.** A
listen-only adapter does not ACK, and evidently nothing else on this pair does. The frame
period says so on its own. Counting the stuff bits of `27C#00000008000030`:

| | Bits | Period | Frames/s |
|---|---|---|---|
| acknowledged, sent back to back | 116 | 232 µs | 4 310 |
| unacknowledged, error-passive sender retrying | 130 | 260 µs | 3 846 |
| **measured**, 5–100 s, pauses over 2 ms taken out | | **258 µs** | **3 875** |

The second row is the frame up to the empty ACK slot, then a passive error flag, the error
delimiter, intermission and the eight-bit suspend an error-passive transmitter owes. The
measurement is 0.7% from it and 11% from the first row, and the clock it rides on — WSL's —
is known to be off by up to 3.4%, not 11. Every `0x27C` payload arrives at least 1 000
times in a row, and there were 172 payload changes in 685 s. The adapter's error counters
would have settled it outright and were not read before `can0` was unplugged.

**What the sender is actually doing is a 415 ms cycle.** About every 415 ms it goes quiet
for roughly 12 ms. After every second pause comes a burst of 5–8 identical `0x751` frames
— so `0x751` once per 830 ms — and then `0x27C` again, with new contents if there are any.
Payload changes land on that grid and nowhere else. So the capture holds about 2.4 samples
of `0x27C` and 1.2 of `0x751` per second, however many frames it took to carry them. Whether
this is the node's normal rate or what it falls back to when nobody answers is not known.

**It fits the 120 Ω.** One terminator visible from the connector and one node talking with
nobody to acknowledge it are the same picture: this pair is not the bus the dash, ABS and
IMU share, or it is and they are not on it with the key on. Nothing here says which unit
is on the other end.

**What the two frames carry**, bytes numbered from 0, checked against the marks in
`ref.csv`:

| Frame | Field | Reading | Evidence |
|---|---|---|---|
| `0x27C` | byte 4, low 3 bits | gear: `0` = N, `1`–`6`, `7` = between gears | 12 of 12 gear anchors, up through the box and back down, read the anchored value at the moment of the mark |
| `0x27C` | byte 3, `>> 1` | TCS setting | 3 of 3 anchors: `0x02`, `0x06`, `0x12` at TCS 1, 3, 9. Bit 0 was set in one frame, while cranking |
| `0x27C` | byte 4, top 2 bits | **not** the ride mode itself — a 2-bit setting the mode carries, unidentified (PWR is the obvious guess) | The R9 has five ride modes (sport, street, rain, custom 1, custom 2) and two bits cannot number five. The capture has exactly five states of bytes 3–4, visited in a fixed cycle: `0E C0` → `0A 80` → `06 40` → `08 80` → `08 00` → `0E C0`. Read as (TCS, this field) that is (7, 3), (5, 2), (3, 1), (4, 2), (4, 0) — five modes each bringing its own TCS level and its own value here, with `2` used twice. The four `ride_mode` anchors 1, 2, 3, 4 landed on `06 40`, `0A 80`, `0E C0`, `08 00`, and the rider says the numbers were meant as sport, street, rain, custom 1 — trying to, because the dash does not step through the modes in a plain order and may reorder them by last use. On that numbering TCS rises 3, 5, 7 from sport to rain as it should, rain at 7 is what the rider expects, and `08 80`, never anchored, is left for custom 2; the custom modes' TCS is not remembered. The order the states appear in the capture says nothing about which is which, for the same reason. `tools/2-session.sh` now has a key per mode (`s`, `t`, `r`, `1`, `2`, written to `ref.csv` as 1–5), so the next capture names the mode instead of counting to it. No field that numbers the mode 1–5 was found in either frame |
| `0x27C` | bytes 0–1, big-endian | engine speed, **scale a candidate** | zero until 12:07:23, then `0x0311` once and `0x0E52`–`0x1120` after it, changing every sample, and the start is where the rider says it was. At [S6]'s ×0.391 that is 307 rpm cranking and 1 430–1 720 rpm at a cold idle. That it is engine speed is settled; the scale is not — no rpm was written down to check it against |
| `0x27C` | byte 6, bit 7 | engine running, **candidate** | `0x30` before 12:07:23, `0xB0` from the second frame after |
| `0x751` | byte 6 | `0x60` in neutral, `0x00` in gear | flips within one 830 ms sample of every change of `0x27C` into or out of gear 0. `0x70` from 12:07:54, engine running, unexplained |
| `0x751` | high nibbles of bytes 0 and 2 | one value centred on `0x80`, unidentified | sits at `0x7F`/`0x80`/`0x81` at rest, reached `0x7E`–`0x91` while the throttle was being blipped |

**What was marked and did not show up**: the throttle and lean events changed nothing in
`0x27C`; the LIF and SCS settings (three anchors each) changed nothing in either frame; and
nothing followed water temperature from 16 to 24 °C or air temperature. Those are either
not on this pair or not sent in this state.

**[S7] had a second protocol in it all along, and it fits this better than the first.**
The MT-09 guide gives two Race Studio configurations, not one. `YAMAHA` / `FZ09 MT09` is
the broadcast protocol this page has been predicting from, and the guide marks it "RS3
Only". The other is `OBD_II` / `CAN`, marked "**Only EURO5 models**", with its own channel
list: OBDII RPM, SPEED, ECT, TPS, IAT, MAP, MAF, FUEL LEVEL, PPS. That is generic OBD-II
over CAN — the logger asks, the ECU answers, and nothing is sent unasked. This page read
the guide for its connector and its first channel list and took the FZ09MT09 protocol to
cover the Euro5 bikes; the guide does not say which of the two works on them, and offers
the OBD one for them by name.

So the reading that fits the capture, and it is a reading, not something the guide says
of the R9: the red Euro5 connector is first of all an OBD port. Temperatures, throttle and
speed would then come only on request, which is why a listener saw none of them, while the
dash gets its own copy over a bus this connector does not reach. Sellers of ELM327 kits
with a red 6-pin cable advertise live rpm, coolant temperature and throttle from these
bikes, which is the same mechanism. `0x27C` and `0x751` are still not explained by it —
neither is an OBD response id.

A search on 2026-10-11 found nobody describing this on a Yamaha: no mention of `0x27C` or
`0x751`, nothing on the R9's connector or bus layout, nothing on whether the Y-TRAC CCU has
a bus of its own. The retransmission itself is ordinary — PEAK's support forum answers it
as a stock question: one of two nodes in listen-only, and the other "sends it again and
again" ([Listen mode](https://forum.peak-system.com/viewtopic.php?p=7321),
[arriving continuously the same message](https://forum.peak-system.com/viewtopic.php?p=7316)).
fz09.org's "Data logging using OBDII" thread was read by hand and is a Woolich reader
being plugged in, nothing more.

**What it leaves open.** Two steps, each a larger intrusion than the last. First, something
has to acknowledge the node's frames: `tools/2-session.sh --ack` does that and nothing
else, and shows whether more ids appear once the resending stops. If they do not, the
second step is OBD requests — the adapter sending frames of its own, which `--ack` is
built to refuse and its watchdog to stop. That one is not decided. The remaining reading,
that the predicted frames are on a pair reached somewhere else on the bike, is not ruled
out by any of this.
