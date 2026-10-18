# How the findings changed

The descriptive pages — the front page, `analysis/r9-signals.md`, `analysis/open-questions.md`
and the rest — say only what is known now. What they said before, and when it changed, is
here. The pages as they stood with every correction written into them are at commit
`ab4fd6f` (`git show ab4fd6f:analysis/r9-signals.md`).

## The fields

- **Gear as the ECU has it, `0x27C` bits 29–31.** Until 2026-10-17 only bit 29 was known:
  set in first gear with the engine running (both times, 2026-10-16), not in second gear with
  the engine off, a candidate for the low bit of a 3-bit gear. AiM's R1 file has `ECU_GEAR`
  there; the test was second gear with the engine running, bit 30 instead of 29. Done
  2026-10-17: byte 3 read `0x46` in second and `0x66` in third at TCS 3. Open question 9,
  closed.
- **TCS, `0x27C` byte 3.** Until 2026-10-16 the equation was `b3 >> 1`, right with the
  engine off and 16 too high in gear with it running, when bit 5 (the ECU's gear) is set.
  Every capture before 2026-10-16 had the engine off or in neutral. Corrected to
  `(b3 >> 1) & 0x0F`; AiM's MT-09 file had the 4-bit width before the captures showed why.
- **`0x27C` b6 bit 7.** Read as "set while the engine runs" from the third capture (set
  within half a second of both starts, cleared at the one stop seen). Corrected 2026-10-16:
  three stops did not clear it, the 2026-10-11 one included, which the capture had shown all
  along. 2026-10-17: it clears in the last frames before key-off.
- **PWR, `0x27C` b4 bits 6–7 and `0x76E` b1.** From the first capture: a 2-bit setting that
  goes with the ride mode, PWR at a guess, since two bits cannot number five modes. Named
  2026-10-16 from four dash photos in Track 1–4; 2026-10-17 the dash's mode menu agreed for
  all five modes. Open question 2, closed 2026-10-16.
- **Rear wheel speed, `0x756`.** Found 2026-10-16 (open question 1) as
  `b1 << 8 | (b0 & 0xF0) | b2 >> 4`, 0.01 km/h at a guess, scaled by a video of the wheel
  (700 counts per revolution a second, a 1.946 m wheel). Corrected 2026-10-17 from a run
  past 41 km/h: the top four bits are `b3` hi, not `b1` hi. The scale moved to the pulses,
  696.5–697.6, a 1.936 m wheel, and one dash photo.
- **Front wheel speed, `0x765`.** Found 2026-10-16 as twelve bits,
  `(b2 >> 4) << 8 | (b6 >> 4) << 4 | (b3 & 0x0F)`. The top nibble was guessed as `b0` hi,
  the first of three candidates (`b0` hi, `b2` lo, `b3` hi), and Bluecan's LIVE page used
  that guess from 2026-10-16. Confirmed 2026-10-17 in a run-up past 41 km/h while the field
  copied the rear's. Open question 10, closed.
- **Rear wheel pulses, `0x27C` byte 2.** First seen as single-frame pulses while the bike
  was lifted on 2026-10-11. Named 2026-10-16 at 46.4 pulses a revolution of `0x756` (46.2
  against the video). 2026-10-17: 46 holes counted on the ring, one pulse each; the earlier
  figures were 0.9 % and 0.4 % high. Open question 11, closed: two photos of a 40-slot
  ring, one from a web shop, were not this bike's, and the 2.7 % between the video and the
  manual's gearing came from first gear, now question 12.
- **The brake, `0x383` b4 bit 3.** `0x383` sat in the list of frames where nothing ever
  changed until 2026-10-16; its rate was written as 2 Hz until 2026-10-17 and is 10 Hz.
- **`0x756` speed field** was in the list of ids that never change with the engine off
  until 2026-10-16.
- **`0x76A`, battery voltage — withdrawn.** "12.85 V" was byte 2's low nibble, which turned
  out to be the top nibble of the water temperature's raw value: a 22 °C engine.
- **`0x751` b6 bits 5–6.** First read as neutral / in gear. 2026-10-16: not neutral alone,
  start permissions at a guess. 2026-10-17: bit 6 = neutral or clutch pulled, bit 5 =
  neutral or sidestand up fits every value in six captures.
- **`0x751` `b0hi:b2`, the shift-sensor candidate.** The service-manual page read the first
  capture as the value moving while gears were changed; what was recorded is that it moved
  while the throttle was blipped. 2026-10-16: the lever pressed with the engine off moved
  nothing.
- **Engine speed's scale.** Fixed at 100/256 not by a new measurement but by naming the
  hundreds in `0x750`, which agree with `0x27C` byte 0 in every frame.
- **The gear anchors** were given on the front page as 13 of 13 from 2026-10-17; the first
  capture's `ref.csv` has 12 gear anchors, and no later capture marked a gear. Corrected to
  12 on 2026-10-17.

## The open questions that closed

- **1, the two wheel speeds** — found 2026-10-16. Left open after it: the unit and the
  front's scale. The unit came from the pulses and a dash photo on 2026-10-17; the front's
  scale is still question 1.
- **2, the 2-bit mode field** — PWR − 1, 2026-10-16.
- **7, the Track display and its four modes** — 2026-10-16: they set PWR and TCS as any mode
  does; SCS and LIF changed and showed nothing; the display itself is `0x76E` byte 2; no new
  ids.
- **9, `0x27C` bits 29–31 as a 3-bit gear** — yes, 2026-10-17.
- **10, how far up the front wheel's field goes** — `b0` hi, 2026-10-17. The rider called it
  the one wheel-speed question a stand can settle (2026-10-16).
- **11, how many pulses a revolution** — 46, 2026-10-17. On the way the rear sprocket was
  not counted: the rider is the first owner, so it is the standard 43.
- **12, first gear** — second and third gear's ratios from the owner's manual were the test,
  and they agreed to 0.1 % on 2026-10-17, leaving first gear alone.

## RaceChrono

- 2026-10-13: the first eight channels entered and seen right against a replay.
- 2026-10-16: TCS and PWR equations corrected (the old TCS was 16 high in gear with the
  engine running; the old "mode field" equation gave PWR less one).
- 2026-10-17: everything but the Track display entered. Against a replay the wheel speeds
  read 3.6 times too fast, because a speed channel takes m/s; corrected to divide by 360.0.
  The first export showed TCS and PWR typed with `bytesTo…` instead of `bitsToUintLe`, and
  the speeds over `360` without `.0`; corrected and exported again.
- Two forms of the grip equation were refused by the app on 2026-10-13 with a complaint about
  brackets; why is not known.

## The connector and the terminator

- Before a pigtail was in hand the pinout came from AiM's MT-09 guide [S7], KDS's table and
  AiM's cable drawing (`analysis/connector.md` at `ab4fd6f` has the reasoning as written).
  A rival allocation, 1 = GND, 4 = K-line, 6 = +12 V, was a search engine's summary of a
  paywalled forum thread that no source says; KDS's table came back scrambled the same way
  through a summariser. The standard itself was not read.
- The pair was expected to read 60 Ω. Measured 2026-10-10, key off, pigtail alone: about
  120 Ω, one terminator. CAN High 3.2 V and CAN Low 1.7 V, a wider spread than predicted.
  The pigtail was marked in pen the same day.
- The CANable's terminator was off for the three captures of 2026-10-11, on the reasoning
  that a listening adapter joins a bus already terminated at both ends. Decided 2026-10-16:
  on, because an acknowledging adapter is the bus's other end. Both captures of that day
  ran with it on, without a bus error; the 2026-10-11 ones ran without and saw none either.
  Bluecan's hardware page said off until the same day.
- The first checklist (2026-10-11) had the adapter listen-only as a rule; listen-only gave
  two ids of 23, and every session since has run with `--ack`.

## Bluecan

- Its pages carried a status dated 2026-10-14: working on the bench against a replay, with
  RaceChrono recording from it; not yet on the R9. Still true on 2026-10-17.
- Its at-the-bike checklist said, until 2026-10-17, that eight channels were entered and the
  TCS and PWR equations had to be replaced with the ones corrected on 2026-10-16.
- Its parts list named the board as #5691, the Reverse TFT Feather with a PCB antenna, until
  2026-10-18. The board in use is #6303, the same Feather with a w.FL antenna connector and
  no antenna in the box; the pages now say so and list the antenna.
- Its user guide and at-the-bike checklist said a recording session asks for three ids,
  `flt 3`, at 41–61 frames a second, until 2026-10-19. The R9 profile has had channels on
  six ids since 2026-10-17, which is `flt 6` and about 91 frames a second; a bench replay
  with that filter forwarded 88 on 2026-10-19.
- Its hardware page said on 2026-10-18 that the Unit Mini CAN's documentation does not say
  whether it has a terminator. Its schematic has one, fixed; corrected 2026-10-19.

## The service-manual candidates

- `analysis/R9_CAN_unknowns_vs_manual.md` was written without the capture files, from the
  behaviour recorded on the mapping page (then `canbus-mapping.md`) and the service manual.
  Its candidates were held to the three captures of 2026-10-11 on 2026-10-12, with the
  results in `open-questions.md`; the page itself was kept as written until 2026-10-17,
  when each candidate was brought to its current state. As written it read the first
  capture's `0x751` `b0hi:b2` excursions as the shift sensor moving while gears were
  changed, engine off; they were throttle blips with the engine running.
- The checklists of the first session (2026-10-11) and the stand session (2026-10-16) lived
  in `analysis/` until 2026-10-17 and are now `first-session-2026-10-11.md` and
  `stand-session-2026-10-16.md` here.
