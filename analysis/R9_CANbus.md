# Yamaha R9 (2025): CAN bus between the meter, ECU, IMU, ABS, BCM and YDT connector

This is a summary of the service manual `Service Manual R9.pdf` (LIT-11616-38-66) and the Czech owner's manual `YZF890_(R9)_CZ_2025.pdf`.

Page references use two numbers:
- **PDF** is the page number shown by the PDF viewer.
- **man.** is the page number printed in the manual.

**Neither PDF is in the repository**: both are Yamaha's. The page references are plain text for that reason; they were links into a local copy until 2026-10-12.

Both manuals are for the **2025** model year. The bike this project measures is a 2026 R9, and its owner's reading is that the two are identical in everything below.

> The manual has no dedicated "CAN" chapter. Everything below is pieced together from three sources:
> - the chapter 9 troubleshooting procedures for the Uxxxx DTCs and the ABS codes,
> - the ECU pin table,
> - the radio-equipment declaration in the owner's manual.
>
> Facts stated **directly in the manual** are kept separate from my **deductions** (⚠).

---

## 1. Yamaha wire colour codes

| Code | Colour | | Code | Colour |
|---|---|---|---|---|
| L | blue | | W | white |
| Lg | light green | | Y | yellow |
| B | black | | Sb | sky blue |

`L/W` means a blue wire with a white tracer.

---

## 2. Three CAN pairs on the bike

The ECU has pins for **three separate CAN pairs**. The ECU pin table ("ECU COUPLER LAYOUT") is on PDF 508–509 (man. 9-10, 9-11).

| # | Pair (H / L) | ECU pin H / L | Units on the pair | Source in the manual |
|---|---|---|---|---|
| **A** | **L/W / L/B** (blue/white – blue/black) | 77 / 61 | **ECU, meter assembly, BCM** | U0155 PDF 713–716, U0100 BCM PDF 717–721, U0155 BCM PDF 726–729 |
| **B** | **Lg/W / Lg/L** (light green/white – light green/blue) | 78 / 62 | **ECU, ABS ECU (hydraulic unit), IMU** | U0125 PDF 710–712, U0121 BCM PDF 722–725, ABS 57 PDF 753–754 |
| **C** | **W/Y / W/L** (white/yellow – white/blue) | 79 / 63 | **ECU ↔ YDT diagnostic connector (6P)**, ⚠ probably also the meter | ABS diagnosis PDF 532–534 |

H = CAN High, L = CAN Low. The manual labels the pairs "(CANH)" and "(CANL)" in the text on PDF 532–534.

The pairs run through **joint couplers**, which are splice connectors with a cap ("joint coupler cap"). For continuity checks you remove the cap and measure through the coupler.

### Diagram (simplified)

```
          pair B (Lg/W – Lg/L)                       pair A (L/W – L/B)
  ┌─────────┐                                                    ┌──────────────┐
  │   IMU   ├──┐                                              ┌──┤ Meter (CCU)  ├── Bluetooth ── phone
  └─────────┘  │    ┌──────────────┐        ┌───────────┐     │  └──────────────┘    (MyRide, Y-TRAC Rev)
               ├────┤ joint coupler├────────┤           ├─────┤
  ┌─────────┐  │    └──────────────┘        │    ECU    │     │  ┌──────────────┐
  │ ABS ECU ├──┘                            │ (gateway) │     └──┤     BCM      │── handlebar switches,
  └─────────┘                               │           │        └──────────────┘   lights, turn signals, cruise
                                            │           ├── pair C (W/Y – W/L) ── YDT connector 6P
                                            └───────────┘          ⚠ + meter?
```

---

## 3. Meter assembly coupler

**Coupler drawing:** PDF 715 (man. 9-217), coupler "1" in the U0155 procedure. The same page also shows ECU coupler "6" and joint couplers "4" and "5".

Pins as drawn (two rows, ✕ = empty cavity):

```
top row:     G/W   ✕    ✕   Sb/W   ✕    ✕   R/B  Br/W
bottom row:  B/L  L/W  L/B   G/L  W/L  W/Y   ✕   B/W
                   ▲    ▲
                 CAN H CAN L  (pair A – arrows in the manual)
```

| Wire | Meaning | Confidence |
|---|---|---|
| **L/W** | CAN H, pair A (meter – ECU – BCM) | ✅ stated in the manual (U0155, arrows on PDF 715) |
| **L/B** | CAN L, pair A | ✅ stated in the manual |
| **W/Y** | probably CAN H, pair C (diagnostic) | ⚠ deduction: same colour as ECU pin 79 and the YDT pair |
| **W/L** | probably CAN L, pair C | ⚠ deduction: same colour as ECU pin 63 and the YDT pair |

Route of pair A to the meter:

**meter → sub-wire harness → wire harness coupler → joint coupler → ECU**

The sub-wire harness is the one for the meter assembly, front turn signal/position lights, headlight control unit and right auxiliary light. The continuity table is on PDF 714 (man. 9-216).

⚠ The drawing doesn't say whether the coupler is shown from the wire side or the terminal side. Confirm the pins by insulation colour and with a multimeter before tapping in.

---

## 4. ECU coupler – CAN pins

- **Coupler drawing:** PDF 715 (coupler "6") and PDF 711.
- **Pin descriptions:** PDF 509 (man. 9-11).

| Pin | Colour | Description in manual | Pair |
|---|---|---|---|
| 61 | L/B | CAN communication circuit | A – L |
| 77 | L/W | CAN communication circuit | A – H |
| 62 | Lg/L | CAN communication circuit | B – L |
| 78 | Lg/W | CAN communication circuit | B – H |
| 63 | W/L | CAN communication circuit | C – L |
| 79 | W/Y | CAN communication circuit | C – H |

In the ECU coupler drawing these wires sit at the left edge of the top two rows: `L/B Lg/L W/L` on top, with `L/W Lg/W W/Y` below.

---

## 5. IMU and ABS ECU (pair B)

- **IMU:** the U0125 procedure "Abnormal CAN communication between the ECU and the IMU" is on PDF 710–712 (man. 9-212 to 9-214).
  - ECU → joint coupler → IMU: **Lg/W** (H), **Lg/L** (L)
  - IMU power from the ignition fuse: **brown/blue → red/white**
  - IMU ground: **black/white**
- **P1600** "Lean angle sensor: … Normal signal is not received from IMU" is an **ECU** DTC. This proves the IMU data reaches the ECU.
  - Procedure: PDF 678–681 (man. 9-180).
  - Code table: PDF 549.
- **ABS ECU:** on the same Lg/W – Lg/L pair, routed ABS ECU → joint coupler → ECU. See PDF 723 and PDF 753–754.
- ABS code 91 "CAN communication (between IMU and hydraulic unit)" is on PDF 765–766.

---

## 6. BCM (Body Control Module)

- **What it is:** a body control unit. The abbreviation is listed on PDF 51 (man. 1-2). It reads the handlebar switches, drives the lights and turn signals, and passes this information on over CAN.
- **Connected parts** (PDF 501, man. 9-3, "PARTS CONNECTED TO THE BCM"):
  - ignition fuse and backup fuse
  - left and right handlebar switches: joystick and Home button for the meter, turn signals, lights, horn, and the **cruise control buttons** (P0564, PDF 650–653)
  - rear brake light switch
  - tail/brake light, front turn signal/position lights, auxiliary lights (PDF 451, PDF 455)
- **CAN:** pair A (L/W – L/B), the same pair as the meter and ECU. See PDF 717 and PDF 727.
- **BCM DTCs** (PDF 552–553):
  - U0100: BCM ↔ ECU
  - U0121: BCM ↔ ABS
  - U0155: BCM ↔ meter. When this link fails, the meter can't be operated with the joystick and Home button, the spanner warning lights up and "Err" is shown.
- **Location:** on the left side, under the upper side cowling, mounted on the inner panel.
  - PDF 195 (man. 4-14): part 13, coupler 12, bolts **7 N·m**.
  - Cable routing: PDF 107 (man. 2-24).

---

## 7. YDT diagnostic connector (pair C)

- The procedure on PDF 533–534 (man. 9-35, 9-36) states: "Between ECU coupler and YDT coupler: **white/yellow (CANH)**, **white/blue (CANL)**".
- The connector has 6 pins ("YDT sub harness (6P)", 90890-03266). The manual **does not** give its pinout. As far as is known, it is an ISO 19689 connector (JST MWT, 6-pin).
- The YDT connector therefore carries **pair C**, the ECU's diagnostic bus. It is not the main inter-unit traffic on pairs A and B.

---

## 8. IMU data in the phone app – which path does it take?

**Facts:**

- The phone connects over Bluetooth to the **CCU (Communication Control Unit)**, using the MyRide and Y-TRAC Rev apps. See owner's manual p. 4-1 to 4-3 (OM PDF 31–33).
- The Bluetooth radio declaration (2400–2483.5 MHz) is issued for the **"motorcycle meter panel YA-1352"** (OM PDF 4). **So the CCU is part of the meter.**
- The IMU is on pair B and the meter is on pair A. **The ECU is the only unit on both.**
- Two troubleshooting procedures confirm that the ECU acts as a gateway. Both check the wiring of *both* pairs to the ECU when two units on different pairs can't talk:
  - ABS code 89 "CAN communication (between meter assembly and hydraulic unit)" (PDF 759–762) checks meter → ECU (L/W, L/B) **and** ABS → ECU (Lg/W, Lg/L).
  - U0121 BCM ↔ ABS (PDF 723) checks the ABS → ECU wiring.

**Conclusion (⚠ deduction):** IMU → (pair B) → **ECU** → (pair A) → meter/CCU → Bluetooth → phone.

**Caveats:**

1. The gateway may not forward raw data. The ECU may only send a processed lean angle at a lower rate, instead of full-resolution 3-axis acceleration.
2. Some messages may only appear while the app is connected, or in TRACK mode.
3. The app may calculate some values from the phone's own GPS and sensors.
4. The meter also has W/Y – W/L wires (pair C), so some data may travel there.

**Suggested test:**

1. Log pair A (L/W – L/B) with the bike on a stand.
2. Lean it from side to side and jolt it forward and back. Look for CAN IDs whose bytes change with lean.
3. Repeat with the app connected, with it disconnected, and in TRACK mode.
4. For comparison, capture pair B (Lg/W – Lg/L) directly, where the IMU traffic is at full resolution.

---

## 9. What the captures on pair C add (2026-10-11)

Three captures were taken on the YDT connector on 2026-10-11. They are in [r9-signals.md](r9-signals.md); what they say about the wiring above:

- **Pair C carries broadcast traffic, not only diagnostics.** 23 ids at 506 frames a second with the key on: `0x27C`, `0x383`, `0x512` and a block from `0x750` to `0x76E`. Engine speed, gear, TCS, the temperatures, the grip and the throttle valve are in it.
- **Nothing on pair C acknowledges the ECU's frames.** With the adapter listen-only the ECU resent one frame about 3 800 times a second and only two ids got through; with the adapter acknowledging, all 23 arrived at their own rates. So whatever else is wired to W/Y – W/L, it was not taking part with the key on. That weighs **against** the deduction in sections 2 and 3 that the meter is active on pair C: an active node would have acknowledged.
- **One terminator is visible from the connector.** The pair reads about 120 Ω with the key off, where two terminators would read 60 Ω.
- **No IMU data and no rider-aid settings other than TCS reach pair C.** *(2026-10-16: and PWR, which the 2-bit mode field turned out to be.)* Lean was looked for against a 30 Hz reference and is not there; SCS, LIF and QS were stepped 21 times and nothing moved. That fits section 8: those travel on pairs A and B, and the ECU does not copy them to the diagnostic pair.

The suggested test in section 8 has not been run. The rider has not decided to tap pair A, and the project's aim for now is what pair C already sends.

---

## 10. Quick page index

| Topic | PDF | man. |
|---|---|---|
| Abbreviations (BCM, IMU, …) | 51 | 1-2 |
| Cable routing near BCM / O2 coupler | 107 | 2-24 |
| BCM location and mounting | 195 | 4-14 |
| Parts connected to ECU, ABS ECU and BCM | 501 | 9-3 |
| ECU circuit diagram | 504–507 | 9-6 to 9-9 |
| ECU pin table | 508–509 | 9-10, 9-11 |
| ABS / YDT – CANH / CANL wires | 532–534 | 9-34 to 9-36 |
| DTC tables (P1600, BCM) | 549, 552–553 | 9-51, 9-54, 9-55 |
| P1600 (IMU → ECU) | 678–681 | 9-180 to 9-183 |
| U0125 ECU ↔ IMU (IMU coupler) | 710–712 | 9-212 to 9-214 |
| U0155 ECU ↔ meter (**meter coupler**) | 713–716 | 9-215 to 9-218 |
| U0100 BCM ↔ ECU | 717–721 | 9-219 to 9-223 |
| U0121 BCM ↔ ABS | 722–725 | 9-224 to 9-227 |
| U0155 BCM ↔ meter | 726–729 | 9-228 to 9-231 |
| ABS 57 – vehicle CAN | 753–754 | 9-255, 9-256 |
| ABS 89 – meter ↔ ABS | 759–762 | 9-261 to 9-264 |
| ABS 90 – ECU ↔ ABS | 763–764 | 9-265, 9-266 |
| ABS 91 – IMU ↔ ABS | 765–766 | 9-267, 9-268 |
| Wiring diagram (large sheets) | 767–773 | – |
| CCU / Bluetooth (owner's manual) | OM 4, OM 31–33 | 4-1 to 4-3 |
