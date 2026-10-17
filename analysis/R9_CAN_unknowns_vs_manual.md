# R9 CAN: the unnamed fields, read against the service manual

This page checks the section *Everything that changes and has no name* in [r9-signals.md](r9-signals.md#everything-that-changes-and-has-no-name) (then part of `canbus-mapping.md`)
(third capture, 2026-10-11, engine idling on the stand) against the Yamaha R9 service manual.
The manual has no CAN frame definitions. What it does have:

- the list of what the ECU reads and drives,
- the YDT monitor items with their ranges,
- the fail-safe columns that name the ECU's internal control states,
- the event codes.

That inventory is the vocabulary for naming fields. The behaviour of each field, as the
mapping page records it, is then matched against it.

**Nothing here was checked against the capture files.** They were not available for this
pass. Every row below is a candidate, built from the manual plus the behaviour already
written down. Each one comes with the test that would settle it. The confidence column
means how well the behaviour fits the manual, not proof.

Page references are to `Service Manual R9.pdf` (SM) and the Czech owner's manual (OM), as in
[R9_CANbus.md](R9_CANbus.md).

> **Since checked against the captures, 2026-10-12.** The candidates below were then held
> to the three capture files, as far as those go, and the results are in
> [open-questions.md](open-questions.md#the-unnamed-fields-read-against-the-service-manual).
> In short: `0x750` `b4hi:b1` is engine speed in hundreds of rpm (zero with the engine off,
> so the second branch of the test in 2.1), and no intake-pressure field was found; `0x764`
> b2 is not the grip's rate of change; `0x755` b2 bit 4 is temperature-gated, 50 to 61 °C;
> `0x756` b2 is a state and not a timer. One correction to 2.1: the first capture's
> excursions of `0x751` `b0hi:b2` were recorded while the throttle was blipped with the
> engine running, not while gears were changed. The rest is untested and this page is
> otherwise as it was written.

---

## 1. What the manual says the ECU has

**Inputs wired to the ECU.** Sources: SM PDF 501,
pinout SM PDF 508–509.

| Group | Inputs | Pins |
|---|---|---|
| Intake | Intake air pressure sensors 1 and 2, intake air temperature, coolant temperature | MAP1 68, MAP2 69 |
| Engine | crankshaft position, O2 sensor, gear position (73) | |
| Throttle | 2× TPS, 2× APS | |
| Pedal | **shift sensor** (quickshifter load cell) | 71 |
| Switches | clutch switch (80/95), sidestand via the relay-unit diode (95, P1601), rear brake light switch (67), a line shared with the BCM and rear brake switch (98) | |

**Outputs the ECU drives:**

- throttle servo, 3 injectors, 3 ignition coils
- **fuel pump relay** (29)
- **starter relay** / starting-circuit cut-off relay 2 (96)
- **radiator fan relay** (19)
- **headlight control unit** (28)
- **GCU** (generator control unit, 45)
- grip warmer (option), purge cut valve (California only)

**Not on the ECU.** The **oil pressure switch** and the **fuel sender** go to the meter
(SM PDF 459: "oil pressure warning … NG → Replace the
meter assembly", "fuel meter fails to come on → check the fuel sender"). So expect no oil
pressure bit and no fuel level on the YDT pair.

**Received over CAN from other units:**

- **Rear wheel speed** from the ABS ECU. P0500: "Normal signal is not received from the rear
  wheel sensor or ABS ECU to ECU" (SM PDF 546).
- **Front and rear wheel pulses**: YDT items 07 and 16 are in the FI (ECU) menu, cumulative
  0–999 (SM PDF 558).
- **Bank angle** from the IMU: YDT item 17, **in 5° steps**, 0–60°. The ECU needs it for the
  overturn cut-off: P1600 and 30_EVENT
  (SM PDF 549,
  565).

**Internal states the manual names.** They appear in the fail-safe columns
(SM PDF 541–552) and the event codes
(SM PDF 562–563):

- **O2 feedback** and O2 feedback learning. Events 240/241: "during O2 feedback, the
  adjustment is maintained at the upper/lower limit".
- **ISC (idle speed control) feedback** and ISC learning. Events 242/243: "during idling,
  the adjustment is maintained at the upper/lower limit".
- **Fuel cut-off**: "Fuel cut is prohibited by accelerator opening" (P2122), "Fuel is not cut
  off due to the throttle opening" (P0122) and "… due to the intake air pressure difference"
  (P1004).
- **α-N mode**, and an **atmospheric pressure** value. MAP1 and MAP2 should read the same
  with the key on and the engine stopped (P1004).
- Quick shift, cruise control and YVSL.
- Events 244 (poor starting) and 245 (engine stop detected).

**The YDT monitor items and their ranges.** Source:
SM PDF 557–561.

| Code | Item | Range / reading |
|---|---|---|
| 01 / 13 | TPS 1 / 2 | 0–125; closed 16±1 / 9–23, open 96–107 / 93–109 |
| 03 / 04 | Intake air pressure 1 / 2 | 0–126 kPa; engine stopped = atmospheric (≈101 kPa ≈ 3.64 V at sea level, 90 kPa ≈ 3.30 V at 1000 m, PDF 584) |
| 05 / 06 | Intake air / coolant temp | −30–120 °C |
| 07 / 16 | Rear / front wheel pulses | 0–999, cumulative |
| 09 | Battery ("fuel system") voltage | 0–18.7 V (18.7 / 255 = 0.0733 V per count, if the YDT value is an 8-bit raw) |
| 14 / 15 | APS 1 / 2 | 0–125; closed 14–18, open 82–92; **turned past closed (forward): 7–12** |
| 17 | Bank angle | 5° steps, 0–60° |
| 20 / 21 | Sidestand / neutral + clutch | 21 is ON in neutral, and ON in gear with the clutch pulled and the sidestand up. That is the start-permission logic (SM PDF 442) |
| 67 / 87 / 104 | ISC learning state (00/01/02), O2 learning, O2 cycle data | |
| 70 | Programmed unit version | 0–254 |
| 80–83 | Cruise RES+, SET−, cancel circuit (clutch, brake lever, pedal), front and rear brake switches | |
| 95 | Shift sensor | 0.2–4.8 V, **≈2.5 V at rest**, lower on upshift load, higher on downshift load (PDF 696) |

**Warm idle spec** (SM PDF 88): 1200–1400 r/min at
90–100 °C, with **"O2 feedback control: Active"**.

---

## 2. Field by field

Confidence levels:

- **A**: the behaviour and the manual fit closely, and one test settles it.
- **B**: a plausible fit with more than one candidate.
- **C**: a guess with no manual anchor.

### 2.1 The noisy "IMU-shaped" values: engine-management inputs, not the IMU

**`0x751` `b0hi:b2` (12 bits, noisy around `0x804`): the shift sensor. A.**

`0x804` = 2052, the middle of a 12-bit range. On a 5 V reference that is 2.505 V, and the
manual gives the shift sensor at rest as "approx. 2.5 V" (code 95). A load cell on a
100 Hz frame is what a quickshifter needs.

In the first capture its top byte moved `0x7E`–`0x91` while gears were being changed, engine
off. Upward means "downshift weighting" in the manual.

*Test:* key on, engine off. Press the shift lever lightly up, then down, without
shifting.
- Expected: the value falls under upshift load and rises under downshift load.
- Also check whether the first capture's excursions line up with the gear anchors.

**`0x750` (100 Hz, noisy with the engine off, almost every bit moving when running): intake air pressure 1 and 2. B+.**

There are two MAP sensors, and fuelling reads them fast. With the engine off they sit at
atmospheric pressure and show only ADC noise. That matches "noisy at rest".

`b4hi:b1` following engine speed at r = 0.95 does not argue against MAP in this session:

- through the warm-up the idle fell and the valve closed, so manifold pressure fell with
  the rpm;
- in the blips both rose together.

*Test:*
- With the engine off, a MAP field reads high and steady. That is about 3.5 V at a few
  hundred metres (≈2 900 as 12 bits of 5 V, ≈725 as 10 bits, or a kPa number around
  97–99).
- It drops sharply at the moment the engine fires.
- The manual's P1004 check says MAP1 and MAP2 agree with the key on. So look for two fields
  in `0x750` that read the same with the engine off and separate once it runs.
- If `b4hi:b1` is instead zero or constant with the engine off, it is engine-speed-derived
  (a second rpm, a period or a target idle). Then MAP is elsewhere in the frame.

**What this does to the lean question.** The 100 Hz noise in `0x750` and `0x751` was the
reason to suspect an IMU. It has better explanations now. And the ECU's own copy of lean
is quantised to **5°** (YDT code 17). The R1 forum's "5-degree increments" is the same
thing. So even a request-based read of lean through this connector would not be useful
telemetry.

### 2.2 The start-up cluster around 26–30 s: O2 feedback. A for the group, B for each bit

O2 feedback is a state the manual names, and the warm-idle spec says it is "Active". The
sensor is heated (P0030), so closed-loop control starts some seconds after a cold start.
It is normally suspended in transients and during fuel cut.

| Field | Behaviour | Best candidate |
|---|---|---|
| `0x751` b6 bit 4 (`0x60` → `0x70`) | set 30 s after start, cleared at the stop, no reaction to blips | O2 sensor active / feedback enabled (a latch) |
| `0x755` b4 bit 6 | on from 26 s after start, drops out for about a second after each blip | O2 feedback running (suspended after a blip) |
| `0x769` b1 bit 4 | set with the engine off and for the first 29 s, a half-second pulse about 1 s after each blip, set again at the stop | open loop / O2 feedback not running (a near-complement of the row above; 2 Hz frame) |

*Test:*
- **A hot restart.** Every delay should shrink, because the sensor and exhaust are already
  hot.
- **A steady 2 500–3 000 rpm hold for 10 s.** The "running" bit stays on.
- **A snap-close from 4 000 rpm or more.** The feedback drops out for as long as the
  deceleration lasts.

### 2.3 Fuel cut and transient fuelling after a blip. B

The ECU cuts fuel with the accelerator closed at elevated rpm (P2122 and P0122 fail-safe
wording). Several fields change exactly in that window:

- `0x764` b1 goes `44` → `43` for a fraction of a second after each blip;
- `0x769` b1 bit 4 pulses;
- `0x76D` b4 / b6 bit 1 change at the blips.

*Test:* hold the grip closed on a long deceleration from 4 000+ rpm.
- A fuel-cut flag lasts as long as the rpm stays above the threshold.
- Any injection-time field goes to zero over the same span.

**`0x757` b1, b2, b5 (one non-zero frame at two of three blips): acceleration enrichment or a fast-opening detect. B−.**

*Test:* compare fast snaps with slow openings to the same position. Only the snaps should
trigger it.

**`0x764` b2 (jumps while the grip moves): the grip's rate of change. B−.**

*Test:* overlay it on the derivative of the 15-bit grip field.

**`0x764` b0, b3–b7, `0x759` b5 (nearly all 256 values), `0x756` b3/b5/b7, `0x75F` b2lo/b5lo: injection time and ignition advance, possibly per cylinder. C+.**

The ECU drives three injectors and three coils, so look for **three fields that move
almost in lockstep**. Injection time rises in the blips and falls to zero during the fuel
cut above. Ignition advance changes on a blip and on closing.

### 2.4 Idle control (ISC)

**`0x76C` `b1:b0hi:b2hi` (16 bits around `0x9200`, up after the cold start, then down and back): ISC feedback adjustment. B.**

The manual has exactly two "adjustments with upper/lower limits": O2 (events 240/241) and
ISC (242/243). This field moves in the first ten seconds, before any of the O2 bits in 2.2
come on, so ISC is the better fit.

*Test:* load the idle and watch whether the field steps away from its rest value and
settles.
- Switch on the high beam.
- Wait for the radiator fan.
- Fit the optional grip warmer, if present.

**`0x512` b3 (0 engine off, ~55 falling to 24 as the engine warms): the ISC amount or warm-up enrichment. B−.**

It falls in proportion to the idle valve opening: valve 23 → 11 in the `÷16` unit, b3
55 → 24, a ratio of about 2.2–2.4 at both ends.

*Test:*
- If it follows the valve when the idle is loaded (fan, high beam), it is the ISC amount.
- If it only follows coolant temperature, smoothly, it is warm-up enrichment.

**`0x755` b2 bit 4 (off, then flickering with a growing share of on from about 2 min after the start, steady from about 3 min): a warm-up stage. B−.**

Candidates: ISC feedback or learning becoming enabled, or "idle stable".

*Test:* read the coolant raw value (`0x512` b0) at 20:18:06 and 20:18:59.
- If the flicker always begins at the same temperature, it is temperature-gated.
- If it tracks rpm reaching the idle target instead, it is an idle-stable condition.

**The idle spec as a second check on the rpm scale.** Warm idle is 1200–1400 r/min at
90–100 °C (SM PDF 88). The third capture ended at
76 °C and 1440 rpm, which is consistent. One fully warm idle (until the fan cycles) gives a
second rpm point.

### 2.5 Switches and start logic

**`0x751` b6 bits 5 and 6 (`0x60` in neutral, `0x00` in gear): the neutral switch plus start permission. B+.**

Two bits change together, not one. The manual's start logic
(SM PDF 442) and YDT code 21: start is allowed in
neutral, or in gear with the clutch pulled **and** the sidestand up.

*Test:* in gear, sidestand up, pull the clutch.
- One bit should set ("start allowed").
- The other should stay clear (neutral).
- Repeat with the sidestand down. Neither bit should set.

**`0x27C` b3 bit 0 (≈0.5 s as the engine starts): the starter relay command or the start switch. A−.**

The ECU drives the starter relay itself (pin 96).

*Test:*
- **Crank longer.** The bit should last as long as the cranking.
- **Press start in gear with the clutch out.** The cut-off relay blocks the starter. If the
  bit still sets, it is the switch input; if not, it is the relay or cranking state.

**Never stepped, and the ECU reads them:** clutch switch, sidestand switch, rear brake (pin
67), the front and rear brake via the BCM (code 83), cruise RES+/SET− (codes 80/81).

`0x76A` b7 bits 2–3 and `0x76D` b0 bits 6–7 "flip now and then". Check them against these
first. A switch read without debouncing would flicker like that.

### 2.6 Things that turn on with the engine running

These fields are set at the start and cleared at the stop:

- `0x27C` b6 bit 7
- `0x76E` b4 bit 1 and b6 bit 0
- `0x751` b3 bits 1 and 3
- `0x75F` b1 bit 4, briefly

Candidates from the ECU's own outputs and judgements:

- **Fuel pump relay** (pin 29). It runs while the engine runs and also primes for a few
  seconds at key-on. *Test:* start the capture **before** turning the key on. A bit that is
  on for a few seconds at key-on, off, then on at the start, is the pump.
- **Headlight on** (pin 28 to the headlight control unit). The owner's manual says the
  headlights come on only after the engine starts (OM PDF 40,
  5-2). *Test:* stop the engine with the stop switch and see whether the headlight and a
  bit go off together.
- **Engine-running judgement** and **charging/GCU enable** (pin 45). These are what is left
  once the two above are placed.

**`0x751` b7 bit 2 (set once, a second after the stop):** maybe the "engine stop detected"
judgement (event 245). **C.**

**`0x756` b2 (counts 0 → `0B` through the start, back at the stop): a start-sequence state or an after-start counter. C.**

*Test:* timestamp each step.
- Equal intervals mean a timer.
- Steps tied to cranking, the first firing and the flare mean a state machine.

**`0x751` b4 high nibble (4 at idle, 7 and 9 in the blips, 5 at the start): a coarse engine-speed band. B.**

4 at 1 440–1 635 rpm, 9 at about 3 400 and 5 during the start flare all fit roughly
`round(rpm / 380)`, or a floor with a bin of about 350. That looks like a map-axis index.

*Test:* plot it against `0x27C` bytes 0–1 × 0.3915.

### 2.7 Wheel

**`0x27C` byte 2 (`01` for single frames, only while the bike was being handled): rear (or front) wheel-speed pulses. B+.**

The ECU has both wheels' pulses from the ABS ECU (P0500; YDT items 07 and 16, cumulative
counts). A single-frame `01` is what a pulse-per-frame delta, or a first speed estimate,
looks like when a wheel turns a few centimetres.

*Test:* rear stand, turn the rear wheel by hand.
- Slow turning gives single `01`s.
- Steady spinning gives a value proportional to speed.
- Nothing at all means it is the front wheel.

### 2.8 Frames where nothing ever changed: `0x383`, `0x760`–`0x763`, `0x76B`

Candidates from the manual:

- programmed unit version (code 70, 0–254) and the ECU part number;
- ISC learning state (code 67: 00/01/02) and O2 learning status;
- the DTC / MIL status, all zero with no faults. AiM's channel list has `FlagError` and
  `Error`.

*Free tests:*
- Look for ASCII.
- **Capture through a key-on.** The MIL lights for 2.0 s at key-on
  (SM PDF 500), so a MIL bit, if one is broadcast
  here, shows itself without creating a fault.
- Provoking a real DTC (for example, unplugging the intake air temperature sensor with the
  key off) would also show the error field. But it leaves an entry in the malfunction
  history that, per the manual, only the YDT deletes
  (SM PDF 514).

### 2.9 Throttle notes from the manual

- **The APS reads the grip's forward travel.** YDT codes 14/15 show 7–12 past closed against
  14–18 closed (SM PDF 701). The 15-bit grip field
  in `0x758` does not show it, so that field is a processed value that clamps at rest.
- **The YDT's 0–125 throttle scale does not fit the CAN fields.** The valve spans ~85 YDT
  counts closed-to-open against ~123 in the CAN unit, so the manual does not name that unit.
- **Two items the manual gives no handle on:** `0x758` b1 bits 4–5 (three steps on the way
  open) and `0x755` b7 high nibble (moves only with the engine off).

---

## 3. One session that tests most of this

Run each step bracketed with marks.

1. **Start the capture before the key goes on.** Turn the key on and wait 10 s. This tests
   the fuel pump prime, the MIL for 2 s, MAP = atmospheric (MAP1 = MAP2) and whether the
   constant frames change.
2. **Key on, engine off.** Bracket each of these separately:
   - shift lever pressed up, then down, without shifting;
   - clutch pulled and released;
   - sidestand down and up, in gear;
   - front brake, rear brake;
   - cruise RES+ and SET−.
3. **In gear, clutch out, press start** (blocked), then a normal start in neutral. Crank for
   longer than usual if the engine allows.
4. **Idle until the radiator fan cycles.** This gives the fan relay, the warm idle against
   1200–1400 r/min, and the O2 bits settled.
5. **Load the idle:** high beam on and off.
6. **Hold 2 500–3 000 rpm for 10 s, then snap shut from 4 000+.** This gives decel fuel cut,
   the O2 dropout and the injection-time candidates going to zero.
7. **Three fast snaps and three slow openings** to the same grip position.
8. **Stop with the stop switch.** Note whether the headlight goes off.
9. **Rear stand.** Turn the rear wheel by hand, then idle in first gear.
10. **Hot restart within a minute.** The O2 delays should shrink.

*2026-10-17: steps 1 (key-on), 4 (the fan), 7 (fast against slow), 8 (stop switch) and
10 (hot restart) will not be done; the owner sees no value in them. The fields each would
have tested are listed in [open-questions.md](open-questions.md#tests-decided-against-2026-10-17).*

---

## Sources in the service manual

| PDF | Man. | What |
|---|---|---|
| 88 | 2-5 | Idling condition: 1200–1400 r/min, O2 feedback active, 90–100 °C |
| 154 | 3-8 | Idle check, ISC learning value (YDT code 67) |
| 442 | 8-9 | Starting circuit cut-off logic |
| 459 | 8-26 | Oil pressure switch and fuel sender go to the meter |
| 500–501 | 9-2, 9-3 | MIL 2 s at key-on; parts connected to the ECU / ABS ECU / BCM |
| 508–509 | 9-10, 9-11 | ECU pinout (fuel pump relay 29, headlight 28, fan 19, GCU 45, starter 96, MAP 68/69, shift 71) |
| 541–552 | 9-43…9-54 | DTC table with fail-safe states (O2/ISC feedback, learning, fuel cut, α-N) |
| 546 | 9-48 | P0500: rear wheel speed comes to the ECU from the ABS ECU |
| 549–550 | 9-51, 9-52 | P1004 (MAP1/MAP2, atmospheric), P1600 (IMU), P1601 (sidestand), P1806 (shift sensor) |
| 557–561 | 9-59…9-63 | YDT sensor and actuator tables |
| 562–563 | 9-64, 9-65 | Event codes 240–245 (O2 / ISC adjustment at limit, engine stop) |
| 565 | 9-67 | Overturn event, bank angle in 5° steps |
| 584 | 9-86 | MAP voltage against altitude |
| 696 | 9-198 | Shift sensor 2.5 V at rest, direction of change |
| 701 | 9-203 | APS: closed, open, past closed |
| OM 40 | 5-2 | Headlights come on after the engine starts |
