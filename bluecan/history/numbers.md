# The numbers

> Moved here from `bluecan/numbers.md` on 2026-10-14, unchanged but for its links.
> `firmware/tools/check-numbers.sh` reads its pattern list from here. The figures a
> builder needs are also in [../usage.md](../usage.md#limits).

**Every measured figure in bluecan, with one home each.** If a number about this device
is worth quoting, its current value is here and the story behind it is in the patch or
the run this table points at.

This file exists because of a specific, repeated failure. The same figures — the
590 msg/s ceiling, 19.6 msg/s over the radio, the display's idle pass, the serial
console's ten-second limit — were written out in four documents each, so every time
reality changed, four places needed the edit and only two or three got it. A whole-tree
review on 2026-08-22 found six of those: documents contradicting each other, and
measurements quoted years-of-context later as though nothing had moved. Prose goes stale
exactly where code review does not look.

Two rules keep it from happening again:

1. **Link here rather than restate.** A number that appears in one file and a link in the
   others has one place to edit.
2. **When a patch changes a number, run the checker** — it prints every place in both
   repositories that quotes the old value:

   ```bash
   bluecan/firmware/tools/check-numbers.sh
   ```

   The checker reads its pattern list out of [this file](#the-pattern-list), so a new
   figure becomes checkable by being written down here.

History belongs in [firmware.md](firmware.md) (per patch) and [bench-link.md](bench-link.md)
(per run). Current state belongs in [README.md](readme-2026-10-13.md). This file is the values.

## The bus

| Figure | Value | Where it comes from |
|---|---|---|
| R9, diagnostic connector, **measured** | **506 frames/s** across **23** arbitration ids, engine off or running | three captures on 2026-10-11 — [r9-signals.md](../../analysis/r9-signals.md#what-the-r9-sends) |
| What telemetry needs, **measured** | **4 ids, 141 frames/s** on the bus; **61** over the radio at the 50 ms per id the app asks for | [how many frames this actually is](../../analysis/background/pre-bike-mapping.md#how-many-frames-this-actually-is) |
| Without an ACK | **2 ids**, one frame resent about 3 800 times a second | [the first capture](../../analysis/captures/session-2026-10-11_115626/README.md#the-first-capture-2026-10-11) — the device has to acknowledge, see [README.md](readme-2026-10-13.md#what-the-r9-turned-out-to-send) |
| Acknowledging, on the bench | switches after **32** intact frames over **200 ms**; **199–202 msg/s** received with TEC and REC at 0 on both nodes; trips at REC **32**, looked for every **10 ms** | [patch 18](firmware.md#patch-18--acknowledge-after-listening--applied-and-measured-on-the-bench) |
| R1-family bus rate, once assumed for the R9 | **~1050 msg/s** across **~16** arbitration ids | documented for the 2015+ R1, never measured here. *Not the R9's figure*: every "R9's ~1050" in the patch history was this estimate, and the device was sized against it |
| What telemetry was estimated to need | **9 ids, ~170 msg/s** at the 50 ms per id the app asks for | the pre-capture arithmetic, kept in [how many frames this actually is](../../analysis/background/pre-bike-mapping.md#how-many-frames-this-actually-is) |
| Saturated 500 kbps bus | **~4200 frames/s** arithmetic ceiling; **4078/s** measured | [bench-link.md](bench-link.md) |
| Bench bus, R9-shaped | ~850 msg/s across 16 ids | [the soak](bench-link.md#the-soak) |

## The BLE link

| Figure | Value | Where it comes from |
|---|---|---|
| Clean ceiling | **~590 msg/s** (re-run on the fixed firmware: clean to ~583, knee at 600–620) | [patch 12](firmware.md#patch-12--the-drain-tasks-priority--applied), re-run in [item 1](../../journal/next-without-the-bike.md#1-the-allow-list--done-and-it-works) |
| Refusals past it | 0.1% at 589, **0.6% at 718**, 7.8% at 840, 22% at 914 msg/s | [patch 12](firmware.md#patch-12--the-drain-tasks-priority--applied) |
| Ceiling before the priority fix | ~200 msg/s clean; 2.5% refused at 321, 6.5% at 476 | [patch 12](firmware.md#patch-12--the-drain-tasks-priority--applied) |
| Direct-to-host vs the wrapper | 0.35% vs 0.8% at ~590 msg/s, 12% vs 35% at ~846 — one interval, 15.00 ms | [patch 13](firmware.md#patch-13--the-frame-path-goes-straight-at-the-host--applied) |
| What the drain path can take | **3399 frames/s** off the queue at a saturated bus, all offered to the radio | [patch 12](firmware.md#patch-12--the-drain-tasks-priority--applied) |
| Why it refuses | `BLE_HS_ENOMEM` — the host is out of mbufs, every time | [patch 13](firmware.md#patch-13--the-frame-path-goes-straight-at-the-host--applied) |
| Connection interval | **assigned by the phone, not granted**: 15.00, 12.50 and 11.25 ms have all come back from the same 7.5–12.5 ms request | [patch 11](firmware.md#patch-11--ask-the-phone-for-a-faster-connection-interval--applied) |
| First phone session | **8018 frames, 0 lost, 0 errors**, one subscribed peer | [the phone session](firmware.md#the-phone-session--run-and-it-works) |

**Never compare two BLE runs at different negotiated intervals.** It is the ceiling on
everything downstream and the phone picks it afresh on every reconnect.

## With the allow-list on

| Figure | Value | Where it comes from |
|---|---|---|
| Filtered throughput | **635.8 msg/s on the bus → 19.6 msg/s over the radio**, 0 dropped, **0 refused** | [patch 14](firmware.md#patch-14--honour-the-allow-list-and-the-notify-interval--applied-and-measured) |
| Interval accuracy | 19.4–19.6 msg/s against a requested 20 Hz — within **3%** | [patch 14](firmware.md#patch-14--honour-the-allow-list-and-the-notify-interval--applied-and-measured) |
| Allow-list capacity | **32 slots**; a 33rd request is counted, and the panel reads `flt 32!` | [patch 14](firmware.md#patch-14--honour-the-allow-list-and-the-notify-interval--applied-and-measured) |

## The frame queue

| Figure | Value | Where it comes from |
|---|---|---|
| Depth | **2000 frames** = ~1.9 s of the R9's ~1050 msg/s, ~0.5 s of a saturated bus, 26 KB of DRAM (upstream's 8 was ~8 ms) | [patch 5](firmware.md#patch-5--frame-queue-depth--applied) |
| Deepest backlog absorbed | **1261 frames**, nothing dropped — before patch 12; single digits after it | [patch 5](firmware.md#patch-5--frame-queue-depth--applied), [patch 12](firmware.md#patch-12--the-drain-tasks-priority--applied) |
| Peak in a 600 s Bluecan soak | **21 of 2000** at 919 msg/s | [patch 15](firmware.md#patch-15--logging-without-interrupts-off--applied-and-measured) |

## The panel

| Figure | Value | Where it comes from |
|---|---|---|
| Idle pass, nothing changed | **~420 µs** — measured 2026-08-22. *Retired: 280 µs, measured on a smaller page* | [cost, measured on the board](firmware.md#cost-measured-on-the-board) |
| One character redrawn | **~650 µs** | [patch 9](firmware.md#patch-9--the-display--applied) |
| A pass while the counters tick | 2–6 ms | [patch 9](firmware.md#patch-9--the-display--applied) |
| First paint after boot or a page change | **~245 ms**, worst seen ~251 ms | [cost, measured on the board](firmware.md#cost-measured-on-the-board) |
| Display task stack free | **1824 B** of 4 KB, after every page had been visited — measured 2026-10-13. *Retired: 1912 B, before the LIVE page; 2.2 KB before that* | [patch 19](firmware.md#patch-19--the-firmware-decodes-four-frames-and-shows-them--applied-and-measured-on-the-bench) |
| LIVE page, a repaint while values move | **2–4 ms**, 2–5 characters; 135–180 µs with nothing changed | [patch 19](firmware.md#patch-19--the-firmware-decodes-four-frames-and-shows-them--applied-and-measured-on-the-bench) |
| Census capacity | **32 ids**; the IDS page shows 24. *Retired: 20, which the R9's 23 overflowed* | [patch 19](firmware.md#patch-19--the-firmware-decodes-four-frames-and-shows-them--applied-and-measured-on-the-bench) |
| Status pixel, one update | **~30 µs** | [patch 16](firmware.md#patch-16--the-neopixel-as-the-riders-status-light--applied) |
| Cost to the frame counters | none measurable, at any of the above | [patch 9](firmware.md#patch-9--the-display--applied) |

## Endurance

| Figure | Value | Where it comes from |
|---|---|---|
| `bluecan_bench`, one hour | **3 224 404 of 3 224 404** at ~850/s, 0 lost, no reset, heap flat | [the soak](bench-link.md#the-soak) |
| Bluecan, no phone | **600 s at 919 msg/s**, 0 dropped, queue peak 21, census monotonic to 699 043 | [patch 15](firmware.md#patch-15--logging-without-interrupts-off--applied-and-measured) |
| Bluecan, with a phone | **150 s at 476 msg/s** — still the longest, and what [item 3](../../journal/next-without-the-bike.md) is about | [patch 12](firmware.md#patch-12--the-drain-tasks-priority--applied) |

## The board and its supply

| Figure | Value | Where it comes from |
|---|---|---|
| BAT rail on the bench | **4.09 V**, no measurable cost to read | [patch 17](firmware.md#patch-17--the-supply-voltage-on-the-glass--applied-flashed-and-read) |
| With the JST jack empty | the gauge models the charger's rail as a cell: 4.10 V, 100.0%, "discharging" at 22%/hr | [patch 17](firmware.md#patch-17--the-supply-voltage-on-the-glass--applied-flashed-and-read) |
| Bike supply | USB, **3 A** — no step-down needed | [hardware.md](hardware-2026-10-13.md) |
| Program storage / static RAM | 1222 KB of 2883584 (42%), 99.1 KB (30%); built as the Lite, without PSRAM and on the chip's own USB serial port. *Retired: 652 KB, 77.3 KB, before the Wi-Fi mode; 1215 KB, 98.5 KB at patch 40* | `arduino-cli compile`, release build, [patch 40](firmware.md#patch-40--the-wi-fi-mode-statistics-and-the-cards-files-on-a-web-page--applied-run-on-the-feather-without-a-phone) |
| Free internal heap, no PSRAM | **183 228 bytes** after start-up, largest block 139 252; no phone connected. Before the Wi-Fi mode, which costs about 21 KB of static RAM; not measured since | [patch 38](firmware.md#patch-38--the-status-light-on-its-own-task-on-both-boards--applied-run-on-the-feather) |

## The AtomS3 Lite, on the Feather

| Figure | Value | Where it comes from |
|---|---|---|
| Free internal heap, no PSRAM | **197 464 bytes** after start-up, largest block 155 636; flat under the full replay | [patch 32](firmware.md#patch-32--the-feather-as-an-atoms3-lite-and-a-build-for-the-lite--applied-run-on-the-feather) |
| ...with the microSD log | **147 708 bytes**, largest block 106 484 | [patch 33](firmware.md#patch-33--a-microsd-log-and-a-stand-in-for-the-card--applied-run-with-the-stand-in) |
| ...with standby and the status light, as the `feather-as-atom` profile has them | **190 872 bytes**, largest block 147 444; no phone connected. Before the Wi-Fi mode | [patch 38](firmware.md#patch-38--the-status-light-on-its-own-task-on-both-boards--applied-run-on-the-feather) |
| ...with the Wi-Fi mode built in, as `feather-as-atom` has it now | **166 184 bytes** in the normal mode, largest block 122 868, after patch 42 and the census module. *Retired: 167 456 at patch 40.* In the Wi-Fi mode **161 964**, nothing on its network, at patch 40. Debug builds, nothing connected | [patch 40](firmware.md#patch-40--the-wi-fi-mode-statistics-and-the-cards-files-on-a-web-page--applied-run-on-the-feather-without-a-phone) |
| Program storage / static RAM, `atom` and `feather-as-atom` | 1183 KB / 93.0 KB; with the microSD log 1242 KB / 133 KB. Without the Wi-Fi mode (`CONFIG_NO_WIFI`) 612 KB / 71.3 KB. *Retired: 610 KB / 71.2 KB, 667 KB / 111 KB before the Wi-Fi mode; 1175 KB / 92.5 KB, 1234 KB at patch 40* | `arduino-cli compile`, release build, [patch 40](firmware.md#patch-40--the-wi-fi-mode-statistics-and-the-cards-files-on-a-web-page--applied-run-on-the-feather-without-a-phone) |

## The serial console

| Figure | Value | Where it comes from |
|---|---|---|
| How long it can be held open | **indefinitely** — 600 s under `bluecan-soak.py`. *Retired: "about ten seconds", which was the board panicking and rebooting on the write* | [patch 15](firmware.md#patch-15--logging-without-interrupts-off--applied-and-measured) |
| What still applies | re-attach after every **flash**; a dead port and a board receiving nothing look identical | [README.md](readme-2026-10-13.md#picking-this-back-up) |

## The pattern list

What [check-numbers.sh](../firmware/tools/check-numbers.sh) greps for. One line per figure:
a key, an extended regular expression, and what it is, separated by `::` —
not `|`, which the regexes themselves use for alternation. Keys beginning `stale-` are
retired values — a hit is not automatically wrong, since a document may deliberately
name the old figure to mark it retired, but every hit is worth a look.

Add a line here when a patch produces a number worth quoting twice.

```
# key :: extended regex :: description
rate-r9-measured   :: 506 (frames|msg)/s|506 frames a second :: the R9's diagnostic connector, measured
rate-r9-needed     :: 141 (frames|msg)/s             :: the four ids telemetry needs, measured
rate-r9-bus        :: ~?1050 msg/s                   :: the R1-family rate once assumed for the R9
rate-telemetry     :: ~?170 msg/s                    :: the pre-capture estimate of what telemetry needs
rate-saturated     :: 4[02][07][08] (frames|msg)/s   :: a saturated 500 kbps bus
ble-ceiling        :: ~?59[0] msg/s                  :: the BLE clean ceiling
ble-knee           :: 600.620                        :: where refusals start
ble-drain-max      :: 3399                           :: frames/s the drain path can take
ble-first-session  :: 8018                           :: the first phone session
filter-throughput  :: 635\.8|19\.6 msg/s             :: filtered bus in, radio out
filter-accuracy    :: 19\.4.19\.6                    :: the notify-interval gate
filter-slots       :: 32 slots|flt 32                :: allow-list capacity
queue-depth        :: 2000 (frames|entries)          :: frame queue depth
queue-backlog      :: 1261                           :: deepest backlog absorbed
queue-peak-soak    :: 21 of 2000                     :: peak in the 600 s soak
disp-idle          :: ~?420 (µs|us)                  :: idle repaint
disp-char          :: ~?650 (µs|us)                  :: one character redrawn
disp-full          :: 24[05] ms|251 ms               :: a full repaint
disp-stack         :: 1824                           :: display task stack free
led-update         :: ~?30 (µs|us)                   :: one status-pixel update
heap-lite          :: 197 464|197 KB                 :: free heap, Feather as the Lite
heap-lite-standby  :: 190 872|191 KB                 :: free heap, feather-as-atom profile
heap-feather       :: 183 228|183 KB                 :: free heap, Feather built as the Lite, with the display
stale-heap-p37     :: 195 416|186 712                :: patch 37's heap figures, before the light's task
heap-lite-sd       :: 147 708|148 KB                 :: free heap, Feather as the Lite with the microSD log
heap-lite-wifi     :: 166 184|166 KB                 :: free heap, feather-as-atom with the Wi-Fi mode built in
heap-wifi-mode     :: 161 964|162 KB                 :: free heap in the Wi-Fi mode
size-wifi          :: 1183 KB|93\.0 KB|1222 KB|99\.1 KB|1242 KB :: program and static RAM with the Wi-Fi mode
stale-size-p40     :: 1175 KB|92\.5 KB|1215 KB|98\.5 KB|1234 KB|167 456 :: patch 40's sizes and heap
stale-size-p38     :: 652 KB|77\.3 KB|610 KB|71\.2 KB|667 KB|111 KB :: program and static RAM before the Wi-Fi mode
soak-bench-hour    :: 3 ?224 ?404                    :: the one-hour bench soak
soak-bluecan       :: 600 s at 919|919 msg/s         :: the 600 s Bluecan soak
soak-phone         :: 150 s at 476                   :: the longest run with a phone
supply-bench       :: 4\.09 V                        :: BAT rail on the bench
stale-disp-idle    :: 280 (µs|us)                    :: retired idle repaint
stale-disp-stack   :: 2\.2 KB|1912                   :: retired stack figures
stale-console      :: ten seconds|ten-second         :: retired serial-console limit
stale-ble-ceiling  :: ~?200 msg/s                    :: the pre-patch-12 ceiling
```
