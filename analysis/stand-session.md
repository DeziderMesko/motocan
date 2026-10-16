# The stand session — the checklist

> **Run on 2026-10-16**, in two captures:
> [session-2026-10-16_191234](captures/session-2026-10-16_191234/README.md) and
> [session-2026-10-16_192421](captures/session-2026-10-16_192421/README.md). The wheel
> speeds were found and the 2-bit field named. Not done: step 3 (the push), the dash
> speeds of step 6, steps 7 and 8, and extras I–N; H was done with the engine off. The
> page is kept as it was used.

One session with the rear wheel off the ground. It is after three things, and has a list
of extras for whatever time and patience are left:

- **Speed and Speed 2**, the two channels of AiM's list not yet looked for. The working
  guess is that they are the two wheels, and that `0x27C` byte 2 is one of them.
- **What the 2-bit field is** — `0x27C` byte 4 top two bits, `0x76E` byte 1. It needs only
  the PWR number the dash shows in each mode.
- **The Track display.** With the display set to Track the dash offers four more modes,
  Track 1 to Track 4, and none of the three captures had it on. What each of them sets,
  and whether the bus carries anything it does not carry otherwise.

Setting the laptop and the adapter up is parts A and B of
[at-the-bike.md](at-the-bike.md), unchanged. What that page calls not negotiable has moved
since it was written: the session runs with `--ack`, because without it this connector
gives two ids of 23. The adapter still transmits nothing, and the watchdog still takes it
off the bus if it ever does.

**One change from parts A and B: the CANable's terminator is ON this time** (decided
2026-10-16). Part B says off, and why: an adapter that only listens joins a bus that is
already terminated at both ends. Pair C turned out to be the ECU and the connector and
nothing else, with one 120 Ω terminator on the bike's side, so an adapter that
acknowledges is the bus's other node and its other end — where a second terminator would
normally sit. Whether this bus is meant to have one there is not known; the three captures
ran without it and with no errors. So the check in part B reads the other way round: with
the adapter wired on and the key off, **about 60 Ω is right, and about 120 Ω means the
terminator is still off.** Note `terminator on` with `m` at the start, so the capture says
which way it was run.

**Still not negotiable: nothing is captured while riding.** Pushing the bike by hand with
the engine off is not riding.

## Bring

Rear stand, and the front stand if you have one: with both wheels in the air each can be
turned on its own, which is the cleanest way to tell the two apart. A phone that films,
something to stand it on beside the rear wheel, and a mark on the tyre it can see — the
yellow balancing dot on the sidewall, or a strip of bright tape on the rim. Laptop, charged, with sleep on lid-close and on idle turned off. CANable and
its cable, the pigtail. Something to put the laptop on that is not the bike once the wheel
is turning — a stool beside it, within reach of the keys and clear of the wheel and chain.

## Before the engine runs

- Outdoors, or the garage door open. This is ten minutes of idling.
- The stand on level, hard ground, and the bike checked for rocking on it before the start.
- Nothing that can reach the rear wheel or the chain: cable, strap, sleeve, the USB lead.
  Route the lead forward, away from the tail.
- You sit on the bike or stand at the bars. Nobody stands behind it or beside the wheel.

## The session

```bash
cd ~/captures && ~/projects/motocan/analysis/tools/2-session.sh --ack
```

Key on when it asks. It proves the bit rate for 3 s and starts acknowledging by itself.
Then, in this order:

| | Where | Do | Why |
|---|---|---|---|
| 1 | — | **Census: 30 s, hands off, engine off, bike on its wheels** | it learns what moves by itself |
| 2 | `6` ride mode | each of the five: select it, settle, press its key. Back in the main menu after **each one**, press `m` and type the PWR the dash shows, as `pwr 2` | names the 2-bit field. The note has to follow its mode, so do them in pairs: mode key, `.`, `m`, `6` |
| 2b | `0` track | On the normal display, **`i` first** and sit still for its 15 s — it resets what the next mark compares against, so `d` will show the switch and nothing from before it. Then display to **Track**, then `d`. Then `i` again, and **sit still for 15 s**. Then each of Track 1–4: select it, settle, press its number; and for **every** setting the dash shows for that mode press `v` and type it as a name and a number — `pwr 2`, `tcs 1`, whatever is on the screen. Display back to normal, then `n`, then `i` again | what the four modes set — nine modes against a field of two bits says more about that field than five did — and whether the bus carries more with the Track display on. `d` prints what changed at the switch; `i` prints the ids that are new and the bytes that have started moving by themselves, there and then |
| 3 | main menu | **Push.** Neutral, engine off, laptop carried or on the seat. `m` `push forward start`, walk it five metres at a steady pace, stop, `m` `push forward end`. Wait 3 s. The same backwards: `push back start`, `push back end` | both wheels turning at one speed: the only time the front turns at all |
| 4 | — | Bike onto the stand — both stands, if you have the front one. Laptop off the bike. **Engine still off**, key on | |
| 4a | `9`, keys `w` and `e` | neutral. Rear wheel by hand: key `w`, a few slow turns, then a brisk spin, let it run down, `w`. Then the **front** the same way with key `e` — front stand only | each wheel alone, with nothing else moving: whatever moves at `w` is the rear, at `e` the front. If neither shows anything, the speed was probably below what the ABS reports — the engine runs below give it more |
| 4b | — | Start the filming: phone on its stand, side-on to the rear wheel, the mark in view, slow motion if the phone has it. Leave it running through steps 5–7. Then start the engine | the wheel's own revolutions, independent of the dash — see the note under this table |
| 5 | main menu | `b`, and **sit still for 10 s** | re-learns the noise with the engine running, or every later mark lists the same engine bytes |
| 6 | `4` wheel speed | `s`. First gear, clutch out, **no throttle** — let it idle in gear. Hold 5 s, read the dash speed, `v`, type it. Then two more steady speeds with a little throttle, each held 5 s before its `v`. Clutch in, rear brake until the wheel stops, `s` | the rear alone: whatever moves is the rear, whatever stays at zero is the front |
| 7 | `4` wheel speed | the same in **second** and in **third**, idle in gear and one more speed each. **Optional if the video is good** | one engine speed, three wheel speeds — the gearing checks the scale without the dash, and the video already does |
| 8 | `1` gear | N, and the gear key each time you change in steps 6–7, if you remember; it is a bonus | more gear anchors with the engine running |
| 9 | — | Neutral, wheel stopped, engine off. `q` | |

**The video.** Counting the mark's passes over the five seconds of a steady speed gives the
rear wheel's revolutions per minute, with no dash and no gearing in between, and that is
the scale of whatever field moves with it. It needs the mark visible every turn and a
camera that does not move. An ordinary 30 fps video is fine at idle in first; above about
30 km/h a wheel turns more than four times a second and the mark blurs or seems to stand
still, which is what slow motion is for. The phone's clock and the laptop's differ, and
that does not matter: each steady speed lasts five seconds, and the video and the bus are
lined up by when the wheel starts and stops. Stop the filming after step 7, before the
engine is stopped, and keep the file — the same as the lean video, it stays out of git and
its hash goes into `SHA256SUMS`.

Keep it slow: nothing here needs more than about 60 km/h on the dash, and a steady number
held for five seconds is worth more than a high one. **Settle first, then press** — the
analysis reads the quiet stretch before a mark, not after it.

At the end it must print `frames transmitted: 0`, a frame count, a mark count, and no
`WARNING`.

## Extras, from the service manual

None of these is needed for the two speeds. They come from
[R9_CAN_unknowns_vs_manual.md](R9_CAN_unknowns_vs_manual.md), which reads the unnamed
bytes against what the manual says the ECU has, and each one is the test for a candidate
there. **Do any of them, in any number; skip the rest.** They are placed where they fit
into the session above. Task `9` in the menu is new and has a key for each control: press
the key, wait 2 s, work the control and hold it 2 s, let go, wait 2 s, press the key again.

**On the stand, before the engine is started** (between steps 4 and 5):

| | Where | Do | What it is a test for |
|---|---|---|---|
| A | `9`, key `w` | *moved into the session as step 4a, with the front wheel beside it* | whether `0x27C` byte 2 is a wheel pulse: single counts when slow, a steady value when spinning |
| B | `9`, keys `u` `d` | press the shift lever up, firmly, **without** changing gear; then down the same way | the shift sensor — the 12-bit value in `0x751` should fall one way and rise the other |
| C | `9`, key `c` | pull the clutch and hold it | the clutch switch, never bracketed |
| D | `9`, key `k` | first gear; sidestand down, hold, up | the sidestand switch, and the start-permission bits beside neutral |
| E | `9`, keys `f` `r` | front brake; then rear brake, separately | the two brake switches |
| F | `9`, keys `+` `-` | cruise RES+; then SET− | the two buttons the ECU reads through the BCM |
| G | `9`, key `p` | first gear, clutch **out**, sidestand up: press the start button for a second. The starter is blocked by design and must not turn | whether the start bit in `0x27C` is the button or the starter. If the starter does turn, let go — the wheel is in the air, which is why this is done on the stand and nowhere else |

**With the engine warm, after the speed runs** (after step 7, in neutral):

| | Where | Do | What it is a test for |
|---|---|---|---|
| H | `9`, key `h` | high beam on, hold 5 s, off | a load on the idle: the idle-control candidates should step and settle |
| I | `2` rpm, key `s` | hold 2 500–3 000 rpm steady for 10 s; then take it to a little over 4 000 and shut the grip at once; `s` when it is back at idle | the O2-feedback bit staying on through a steady hold, and fuel cut on the way down |
| J | `3` throttle | three fast snaps (`f` before each); then three slow openings to about the same place, each inside its own `s` … `s` | what reacts to a fast opening only |
| K | main menu, `m` | let it idle until the radiator fan starts and stops once; note `fan on` and `fan off` as they happen | the fan relay, and a fully warm idle against the manual's 1 200–1 400 rpm. Long: ten minutes or more, so only if there is time and air |

**At the very end** (instead of step 9):

| | Where | Do | What it is a test for |
|---|---|---|---|
| L | main menu, `m` | note `stop switch`, stop the engine with the **stop switch**, and look at the headlight. Note `headlight off` or `headlight on` | the bits that come on with the engine: headlight against the rest |
| M | main menu, `m` | within a minute: note `hot restart`, start it again, let it idle 60 s | the three bits that switched 26–30 s after a cold start should switch sooner |
| N | main menu, `m` | engine off. Note `key off`, turn the key **off**, wait 10 s, note `key on`, turn it on, wait 10 s. Then `q` | the fuel pump's prime and the MIL's 2 s at key-on, which no capture has seen because the script starts after the key. **Last on purpose**: if the bus going quiet or coming back trips the watchdog, the capture stops there and nothing after it is lost |

Typing a note for N with the key off works: the script does not need the bus to take a
mark. If `!! THE CAPTURE HAS STOPPED` appears during N, that is the watchdog and the
session is over — `.` then `q`, and say so.

## What may happen, and what to do

| It does | It means | Do |
|---|---|---|
| the dash speed stays at 0 with the rear turning | the speedometer reads the front wheel | `m` `dash speed zero`, and carry on without `v`. The gearing still gives the speed |
| a warning lamp comes on, or the engine stutters or will not rev in gear | a turning rear and a still front is what traction control and ABS are built to notice. Not known how the R9 takes it | `m` what you saw, back to neutral. Try once more with TCS at its lowest setting, or off if the dash offers it. If the lamp stays on after a restart, stop there and say so — it usually clears after a short ride, which is general experience and not something tried on this bike |
| the bike moves on the stand | — | clutch in, rear brake, engine off. Nothing in this session is worth it |
| `!! THE CAPTURE HAS STOPPED` | the watchdog pulled the adapter off the bus | `.` then `q`; the reason is in `ack-watchdog.log` |
| a step was botched | — | `x` takes the last mark back; do it again |

## Afterwards

The session folder in `~/captures` is the result and git does not hold it yet. Put the
video into its `other/` folder, as with the lean video. Leave it
where it is and say it is there. What gets read out of it:

- which bytes are zero through step 1, move together in step 3, and move alone in steps
  6–7 — that is rear against front;
- which move at `w` and which at `e` in step 4a, if anything did;
- the scale of each, from the wheel's revolutions in the video, from the dash anchors, and
  from rpm × gearing if step 7 was done;
- whether `0x27C` byte 2 is one of them;
- the PWR notes against the 2-bit field, five pairs;
- from step 2b: what `notes.txt` says after each `still check` — new ids, bytes newly
  moving — which bytes stepped at `display_track`, and `ride_mode` 6 to 9 with their
  settings beside the first five.

## Not in this session

Rider aids, lean, temperatures, anything that asks the bike a question, anything on another
connector. From the other page's list, one thing is left out: cranking for longer than the
engine needs, which there is no good way to do. They are either done or decided against for now; see the "Next" list at the end
of [the third capture](captures/session-2026-10-11_200550/README.md#the-third-capture-2026-10-11-the-engine-running).
