# At the bike — the checklist

The first logging session on the R9, as steps to follow rather than documents to read.
Everything here is drawn from [pre-bike-mapping.md](../analysis/background/pre-bike-mapping.md),
[analysis-tips.md](../analysis/background/analysis-tips.md) and [`tools/2-session.sh`](../analysis/tools/2-session.sh); where
a step is a suggestion rather than something written down or measured, it says so.

**Two rules that are not negotiable:** the adapter is **listen-only**, and nothing is
captured **while riding**. The script enforces the first and refuses to continue without
it.

*After the first session, 2026-10-11:* the first of those rules gave way, and the reason
is under [With `--ack`](#with---ack-the-adapter-acknowledges-frames) below. Listen-only on
this connector yields two ids of 23, so every session since has run with `--ack`, where
the adapter acknowledges frames and still transmits none. The second rule stands. The
checklist for the next session is
[stand-session.md](stand-session-2026-10-16.md) (run 2026-10-16, with the CANable's terminator on);
this page is kept as the first one was run.

## A. The laptop, at home, on a network

Do all of this before leaving. Nothing in part C needs a network; all of this does.

| | Command | Passes when |
|---|---|---|
| 1 | `cd ~/projects/motocan && git pull --ff-only` | it pulls. No such directory means the laptop was never bootstrapped — copy [`tools/bootstrap-wsl.sh`](../analysis/tools/bootstrap-wsl.sh) over and run it |
| 2 | `cd analysis && ./tools/0-setup.sh` | `imports ok` |
| 3 | `./tools/1-selftest.sh` | `selftest passed` |
| 4 | dry run, below | it ends with `session in ...` and four files |
| 5 | adapter, below | `can <LISTEN-ONLY>` |

**4. The dry run** rehearses the keys on a virtual bus. Two terminals:

```bash
cangen vcan0 -g 20
```

```bash
mkdir -p ~/captures && cd ~/captures && ~/projects/motocan/analysis/tools/2-session.sh --iface vcan0 --bench
```

`vcan0` is made by the selftest and is gone after a WSL restart. Walk into a task, press
a few keys, `x` one of them, `.` back out, `q`. Delete the session folder afterwards.

**5. The adapter on this machine.** `usbipd bind` is per machine, so the desktop having
done it says nothing about the laptop. CANable plugged in, WSL running, from an
**administrator PowerShell**:

```
winget install --exact dorssel.usbipd-win      # once, if `usbipd` is not found
usbipd list                                    # note the CANable's BUSID
usbipd bind --busid <BUSID>                    # once per machine
usbipd attach --wsl --busid <BUSID> --auto-attach
```

Leave that window open. Then in WSL:

```bash
sudo ~/projects/motocan/analysis/tools/can-up-listen-only.sh
```

Write the BUSID down here once known: `2-2` (the capture notebook, 2026-10-11; it follows
the USB port, so a different socket gives a different one)

Also: charge the laptop, and stop Windows sleeping on lid-close or idle for the day — a
sleep drops the adapter mid-capture.

## B. The pigtail and the adapter, at a desk

Skip what is already done. Pin identification is in
[connector.md](../analysis/connector.md) under the connector table, and since 2026-10-10
the pigtail itself is marked in pen with what the meter found
([photo](../analysis/sources/r9/pigtail-verified-pins.jpg)). The short form:

- The **middle column** is the CAN pair. **CAN High (2) is the middle cavity of the row
  next to the latch**, CAN Low (5) the middle of the other row. Ground is 3, battery +12 V
  is 4.
- Wire CAN High → `CANH`, CAN Low → `CANL`, ground → the adapter's `GND`. The **+12 V
  wire goes nowhere**: insulate its end. The adapter is powered from USB.
- **The CANable's 120 Ω terminator must be OFF.** On the bench it is on, because there it
  is one end of the bus; the bike's bus is terminated already and a listening adapter
  has no business changing it. With the adapter
  connected to nothing, a meter across its `CANH`/`CANL` reads about 120 Ω when it is on
  and open circuit when it is off.

  *2026-10-16: on, for every session with `--ack`.* With the key off and nothing plugged
  in, the bike's pair reads about 120 Ω: one terminator, where a CAN bus has one at each
  end and reads 60 Ω. It works without the second — the captures of 2026-10-11 ran with
  the terminator off and saw no bus error — but an adapter that acknowledges is the bus's
  other node and its other end, where the second terminator belongs, so on is the better
  setting. Both captures of 2026-10-16 ran with it on, also without a bus error. With the
  adapter wired on and the key off, about 60 Ω is then right. See
  [stand-session.md](stand-session-2026-10-16.md).

## C. At the bike

Bring: laptop, CANable with its USB cable, the pigtail, a multimeter. A rear stand if
there is one — the session is worth doing without it, and step 8 says what changes.

1. **Key OFF.** Seat off. The connector is the red 6-pin, expected under the seat — that
   is AiM's word for the R9's platform, not yet seen on an R9. Plug the pigtail in.
2. **Meter, key still off** (the first time, and after any rewiring). Across the CAN pair:
   **about 120 Ω** — measured on this R9 on 2026-10-10 with the pigtail alone, where 60 Ω
   had been expected. With the adapter wired on it must read the same; **about 60 Ω now
   means the adapter's terminator is still on**. Open circuit means those are not the CAN
   cavities. The +12 V wire reads 12 V against battery negative — which
   is how to tell it is the one to keep insulated. *(2026-10-16: with `--ack` the
   terminator is on, and about 60 Ω is the right reading — see part B.)*
3. CANable into the laptop. Administrator PowerShell, and leave it open:
   ```
   usbipd attach --wsl --busid <BUSID> --auto-attach
   ```
4. In WSL:
   ```bash
   cd ~/captures && ~/projects/motocan/analysis/tools/2-session.sh
   ```
   It asks for the sudo password and must print `listen-only on` and no `!!`. **If it
   reports anything but LISTEN-ONLY, stop and unplug.**
5. `turn the ignition ON now` — key on, wait for the dash, press any key.
6. `bus is alive` within 3 s. Start the engine now, so the census sees it idling.
7. **Census, 30 s, hands off.** It prints the ids on the bus, and it learns which bytes
   move with nobody touching the bike — those are noise, and are left out of what it
   reports later. So do keep still for it.
8. **The tasks.** A menu, not a sequence: pick by number, skip what cannot be done today,
   come back to any of them. Each prints its own instructions. Three habits:
   - **Settle first, then press.** The analysis reads the quiet stretch *before* a mark.
   - **Sweeps go up quickly and down slowly**, with `s` before and `s` after.
   - **Botched it? Press `x` and do it again.** `.` leaves a task, `m` writes a note.

   After every mark it prints `moved since the last mark:` and a list like
   `27C b3(2) b4(2)` — id, byte, how many values it took. That is there so you can see the
   bus noticed what you did; `nothing new moved` after a setting change is worth a second
   try. If the engine is started part-way through, press **`b`** in the main menu and sit
   still for 10 s, or every mark from then on lists the same engine bytes.

   Task 5 is just typing the dash temperatures now and then — go back to it a few times
   as the engine warms. Task 7 is the rider aids: change one setting at a time.

   Task 0, added 2026-10-14, is the Track display and the four modes that come with it.
   It has the one key that does not mark something you did: `i` sits still for 15 s and
   says which ids are new and which bytes have started moving by themselves. That is the
   question to ask of a state — a display left on Track — where the list after a mark
   answers it for an action.

   **Without a rear stand**, 2, 3, 5, 6, 7 and 8 are untouched — 8 is the bike leaned by
   hand. Two change:
   - **1, gear: do what the gearbox allows.** N and 1st for certain; rocking the bike
     back and forth while shifting usually finds more, which is general experience and
     not something tried on this bike. A partial run still counts — the conflict is
     which bits of the byte carry the gear, and every gear tagged is a raw value for it.
   - **4, wheel speed: skip it.** The task is the rear wheel turning alone, which is
     what tells rear from front, and nothing done at a standstill replaces that. The
     frames are in the capture regardless; the answer waits for a session with a stand.
9. **`q`** to finish. It must print a frame count and a mark count, **no `WARNING`**, and
   `session in ...`. Key off, unplug, cap back on the connector.

## With `--ack`: the adapter acknowledges frames

Not the default, and not for a first session on a bus nobody has listened to. It exists
because of what the first capture found ([the first capture's write-up](../analysis/captures/session-2026-10-11_115626/README.md), "The
first capture"): one node resending one frame because nothing acknowledged it. To hear
what that node says to something that answers, the adapter has to answer.

```bash
cd ~/captures && ~/projects/motocan/analysis/tools/2-session.sh --ack
```

Everything in C still applies — key off to plug in, the terminator off, the +12 V wire
insulated. *(2026-10-16: the terminator on, and 60 Ω at the meter, for the reason in
part B.)* What changes at step 4:

1. It comes up **listen-only first**, asks for the ignition, and listens for 3 s. Fewer
   than 20 frames and it stops there: the bit rate is unproven, and an adapter that ACKs
   at the wrong rate breaks every frame on the bus.
2. It says what it is about to do and goes ahead. Until 2026-10-11 it waited here for the
   word `ack`, typed; `--ack` on the command line is the consent now, and step 1 is the
   gate.
3. It switches mode and starts a watchdog as root
   ([`tools/can-up-ack.sh`](../analysis/tools/can-up-ack.sh)). The watchdog takes `can0` down — the
   adapter off the bus — if the session ends for any reason, if the controller leaves
   `ERROR-ACTIVE`, if its receive error counter climbs, or if a single frame is
   transmitted. Nothing in the session sends frames; that last check is there so that
   stays true.

If the prompt starts printing `!! THE CAPTURE HAS STOPPED`, the watchdog has pulled the
adapter off: `.` then `q` to finish, and the reason is in `ack-watchdog.log` in the
session folder. At the end it must print `frames transmitted ... 0` and no `WARNING`.

**Run once at the bike, 2026-10-11 13:09**, and it went as designed: 11 503 clean frames
in the 3 s listen-only probe, 273 s acknowledging, nothing transmitted, the watchdog silent
until the session ended and then taking `can0` down. The firmware does report its state
and error counters to Linux — the interface came up `state ERROR-ACTIVE (berr-counter tx 0
rx 0)` in both modes — so the watchdog has all four of its conditions to go on. They have
still only been exercised against a stand-in for `ip`; none has had to fire on the adapter.

## If it does not go to plan

| It says | It means | Do |
|---|---|---|
| `can0 does not exist` | the adapter is not attached to WSL | step 3; the message prints the commands |
| `asked for LISTEN-ONLY but ...` | the mode did not take | **stop**, do not capture |
| `NOTHING on can0 in 3 s` | key not on, or the pair is not the pair | key off, redo the 120 Ω check, try CANH/CANL swapped |
| the session dies part-way | — | `capture.log` and `ref.csv` are already on disk and complete. Run it again for the rest; convert the first by hand with `tools/3-candump-to-webcan.py capture.log -o trace.csv` |

## Afterwards

The session folder is the result and **git does not hold it** — copy it somewhere else
before anything. Then the command the script printed last: `survey.py` over `trace.csv`,
which answers the first question — is the R9 the predicted profile — with no further
input.
