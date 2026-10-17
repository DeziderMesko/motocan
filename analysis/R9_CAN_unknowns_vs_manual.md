# R9 CAN: the unnamed fields, read against the service manual

This page holds the fields in
[r9-signals.md](r9-signals.md#everything-that-changes-and-has-no-name) that change and have
no name against the Yamaha R9 service manual. The manual has no CAN frame definitions. What
it does have:

- the list of what the ECU reads and drives,
- the YDT monitor items with their ranges,
- the fail-safe columns that name the ECU's internal control states,
- the event codes.

That inventory is the vocabulary for naming fields. Each field below has a candidate from
it, a grade for how well the behaviour fits the manual, and the test that would settle it;
where a capture has answered the test, the field says what it showed. The same as one
table: [open-questions.md](open-questions.md#the-unnamed-fields-read-against-the-service-manual).

Page references are to `Service Manual R9.pdf` (SM) and the Czech owner's manual (OM), as in
[R9_CANbus.md](R9_CANbus.md).

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

It spreads in the blips with the engine running (1 961–2 164 against 2 036–2 068 at rest).
With the shift lever pressed firmly up and down four times, engine off, it read its three
resting values, 2 038, 2 054 and 2 070, throughout: against the candidate, unless the
sensor is read only with the engine running.

*Test:* the same lever presses with the engine idling. The value should fall under upshift
load and rise under downshift load.

**`0x750` (100 Hz, noisy with the engine off, almost every bit moving when running): intake air pressure 1 and 2. B+. Not found.**

There are two MAP sensors, and fuelling reads them fast. With the engine off they sit at
atmospheric pressure, and the manual's P1004 check says MAP1 and MAP2 agree with the key
on. A MAP field reads high and steady with the engine off — about 3.5 V at a few hundred
metres, ≈2 900 as 12 bits of 5 V, ≈725 as 10 bits, or a kPa number around 97–99 — and
drops sharply as the engine fires.

`b4hi:b1` is not it: it is zero with the engine off and is engine speed in whole hundreds
of rpm ([r9-signals.md](r9-signals.md)). `b7` hi goes 9 with the engine off, 3 while
cranking, 5 at a cold idle and 6 warm, but in a blip it goes the wrong way for a pressure:
down as the valve opens, and up to 8 on the closed throttle at 3 400 rpm. Bytes 0, 2, 5
and 6 are not resolved.

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

The timing agrees: 30 s, 26 s and 29 s after the start in the third capture, and
`0x751` b6 bit 4 at 31 s in the first.

*Test:*
- **A steady 2 500–3 000 rpm hold for 10 s.** The "running" bit stays on.
- **A snap-close from 4 000 rpm or more.** The feedback drops out for as long as the
  deceleration lasts.
- A hot restart, where every delay should shrink, will not be done.

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

*Test:* compare fast snaps with slow openings to the same position; only the snaps should
trigger it. It will not be done.

**`0x764` b2 (jumps while the grip moves): not the grip's rate of change.** In a slow
closing the grip fell from 124 to 72 over four seconds and the byte sat at `A1` throughout.
It changes with where the grip is, not how fast it moves, and only below about two thirds
of the travel.

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
settles: the high beam with the engine running, or the optional grip warmer. The high beam
with the engine off moved nothing; waiting for the radiator fan will not be done.

**`0x512` b3 (0 engine off, ~55 falling to 24 as the engine warms): the ISC amount or warm-up enrichment. B−.**

It falls in proportion to the idle valve opening: valve 23 → 11 in the `÷16` unit, b3
55 → 24, a ratio of about 2.2–2.4 at both ends.

*Test:*
- If it follows the valve when the idle is loaded (high beam with the engine running), it
  is the ISC amount.
- If it only follows coolant temperature, smoothly, it is warm-up enrichment.

In the captures so far the valve and the coolant moved together, so the two cannot be told
apart.

**`0x755` b2 bit 4: a warm-up stage, temperature-gated. B−.** Never on below 50.0 °C of
coolant, first on at 50.8, on a growing share of the time through the fifties (27 %, 39 %,
62 %, 83 %), steady from 60.8 °C. Engine speed was flat at 1 450–1 500 the whole while, so
it is not an idle-reached condition. Candidates: ISC feedback or learning becoming
enabled.

Warm idle is 1200–1400 r/min at 90–100 °C (SM PDF 88). The third capture ended at 76 °C and
1440 rpm.

### 2.5 Switches and start logic

**`0x751` b6 bits 5 and 6: bit 6 = neutral or the clutch lever pulled, bit 5 = neutral or the sidestand up. B+.**

The manual's start logic (SM PDF 442) and YDT code 21: start is allowed in neutral, or in
gear with the clutch pulled **and** the sidestand up. Every value of the byte in six
captures fits the two bits above ([r9-signals.md](r9-signals.md)); the start needs bit 6
and running in gear needs bit 5, at a guess.

*Test:* engine off, in gear, clutch out: the sidestand up and down should read `0x20` and
`0x00`; stand up, the clutch pulled and let out, `0x60` and `0x20`.

**`0x27C` b3 bit 0 (≈0.5 s as the engine starts): the starter, not the start switch. A−.**

The ECU drives the starter relay itself (pin 96). With the start blocked (second gear,
sidestand down) the button was pressed and the bit did not set. *Test still open:* crank
longer; the bit should last as long as the cranking.

**The switches the ECU reads, stepped one by one:** both brakes are `0x383` b4 bit 3;
cruise RES+ and SET− are in `0x76D`; the clutch and the sidestand are the `0x751` b6 bits
above; `0x755` b6 bit 0 sets with the clutch or either brake but also stays set through an
engine run in gear.

`0x76A` b7 bits 2–3 and `0x76D` b0 bits 6–7 "flip now and then". A switch read without
debouncing would flicker like that; none of the switches above matched them.

### 2.6 Things that turn on with the engine running

These fields come on with the engine:

- `0x27C` b6 bit 7: set at the first start after key-on, not cleared by a stop, cleared at
  key-off
- `0x76E` b4 bit 1 and b6 bit 0: set at the start, cleared at the stop
- `0x751` b3 bit 1: clear exactly while the engine runs, set otherwise; the engine-running
  judgement at a guess. Bit 3 sets and clears with the starter bit
- `0x75F` b1 bit 4, briefly at the start

Candidates from the ECU's own outputs and judgements:

- **Fuel pump relay** (pin 29). It runs while the engine runs and also primes for a few
  seconds at key-on. A bit that is on for a few seconds at key-on, off, then on at the
  start, is the pump. One key-on is captured, 12 s before a start (2026-10-17); it has not
  been searched for a prime.
- **Headlight on** (pin 28 to the headlight control unit). The owner's manual says the
  headlights come on only after the engine starts (OM PDF 40, 5-2). The test, stopping
  with the stop switch, will not be done.
- **Engine-running judgement** and **charging/GCU enable** (pin 45). These are what is left
  once the two above are placed.

**`0x751` b7 bit 2 (set once, a second after the stop):** maybe the "engine stop detected"
judgement (event 245). **C.**

**`0x756` b2 lo (`06` to `0B` through the start, back at the stop): a start-sequence state. C.**

A state, not a timer: `06`, `07`, `08` within 100 ms before any engine speed is reported,
`09` at 1 190 rpm, `0A` at 1 509, `0B` at 1 541 — gaps of 50, 50, 300, 150 and 60 ms. At
the stop `0B`, `03`, `01`, `00` in 100 ms.

**`0x751` b4 high nibble (4 at idle, 7 and 9 in the blips, 5 at the start): a coarse engine-speed band. B.**

Roughly, with a lag: medians of 1 477, 2 079, 2 288, 2 701, 2 996 and 3 327 rpm for 4 to 9,
about 300 rpm a step from 1 900 up, but the bands overlap by hundreds of rpm. It is not a
clean function of the speed at that instant; a map-axis index at a guess.

### 2.7 Wheel

The ECU has both wheels' pulses from the ABS ECU (P0500; YDT items 07 and 16, cumulative
counts). On this connector: the rear's pulses in `0x27C` byte 2, one a hole of the sensor
ring, and the two wheel speeds in `0x756` and `0x765` ([r9-signals.md](r9-signals.md)).

### 2.8 Frames where nothing ever changed: `0x760`–`0x763`, `0x76B`

`0x383` changes: its b4 bit 3 is the brake, and b0 bit 2 set once as a fault came on and
stayed set through a key cycle, a candidate for a fault flag.

Candidates from the manual:

- programmed unit version (code 70, 0–254) and the ECU part number;
- ISC learning state (code 67: 00/01/02) and O2 learning status;
- the DTC / MIL status, all zero with no faults. AiM's channel list has `FlagError` and
  `Error`.

*Free tests:*
- Look for ASCII.
- **Capture through a key-on.** The MIL lights for 2.0 s at key-on (SM PDF 500), so a MIL
  bit, if one is broadcast here, shows itself without creating a fault. One key-on is in
  the capture of 2026-10-17, and none of these frames changed.
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

Steps 1 (key-on), 4 (the fan), 7 (fast against slow), 8 (stop switch) and 10 (hot
restart) will not be done. The fields each would have tested are listed in
[open-questions.md](open-questions.md#tests-decided-against).

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
