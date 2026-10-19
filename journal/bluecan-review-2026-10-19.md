# Bluecan review, 2026-10-19: what could cost a ride's data

The whole of `bluecan/firmware/bluecan/` as of `f6ff603`, read for one question: what
could leave the rider back from a ride with no data, because something stopped, stuck
or did not survive an odd situation. The new code (`src/sdlog/`, `src/standby/`, the
AtomS3 Lite profile) was read line by line; patches 23–31, since the review of
2026-10-16, were read as their diff and are display layout, the wheel speeds and the
screen's doze. Nothing here has been changed yet: each finding says what it would take,
and the decisions are the owner's.

Ordered by how much data each could cost.

## 1. A trip to listen-only lasts until the next start — high

`controller::supervise()` stops acknowledging when REC reaches 32, the controller reports
error-warning or bus-off, or TEC moves, and the state is latched "until reboot"
(`controller.cpp`, the `acknowledging` case). On the R9's diagnostic connector a listening
board gets almost nothing: listen-only gave two ids of 23 on 2026-10-11, because the
engine control unit repeats its first unacknowledged frame. So one burst of bus errors
during a ride costs the rest of that key cycle's telemetry and log.

- What could cause a burst on the road has not been tried: a pigtail contact that opens
  under vibration, ignition noise. Every capture on the stand, CANable acknowledging,
  saw no bus error, engine starts included.
- Nothing shows it on the Lite (see 4). The Feather shows `TRIP-REC` on the BUS page and
  a red flash.
- With standby the latch ends at key-off. It does not end while the key is on: in
  listen-only the repeated frame keeps the frame counter moving, so the board never
  goes quiet enough to sleep.
- The latch was chosen on purpose: rejoining a bus that has just looked wrong was judged
  not worth it. The alternative is to drop back to listening and prove the bus again —
  the same 32 clean frames over 200 ms that the first start needs — with a limit or a
  growing pause so that a bus that is really bad does not have the board flapping on
  and off it.

## 2. The microSD log does not recover from a failed write — high, once a card is used

`sdlog::run()` counts a failed `file.write()` and moves on to the next sector. After a
card error the SD library's state is not reset, so every later write is likely to fail
too: a card that drops out for a moment in its socket costs the rest of the ride. The
error count is printed only by a debug build. Recovery would be: on a failed write,
close, `SD.end()`, mount again and open a new file, with a pause between attempts.

The same code also has nowhere to go when the card is full (a 16 GB card is about 370
hours at the R9's rate) or when the ten-thousandth file name is reached, where it opens
the last one again and truncates it.

## 3. No watchdog on the frame path — medium

The task watchdog watches only core 0's idle task. It resets a board where something on
core 0 spins; it does not see a task that blocks for good, nor anything on core 1. If
the Bluetooth drain task, the card's task or `loop()` (the bus supervisor and standby)
stopped, the board would stay up and do nothing until the power went. Nothing found here
is known to block for ever — the logger's lock, the BLE send and both USB consoles all
time out — so this is a safety net, not a fix: add the three tasks to the task watchdog
and reset it from each loop. A reset costs about half a second on the bus, a few
seconds for the phone, and starts a new file.

On the Lite, standby already covers one case: if reception stopped while the bus was
busy, the board would sleep after five seconds and wake at once, which is a restart.

## 4. The Lite has no status light — medium

The status light's logic lives in the display task, which a board without a display
does not run, so the Lite's RGB LED (G35) stays dark. Before riding off there is nothing
on the board to say whether it is acknowledging, whether a phone is subscribed, whether
the card is being written. The phone shows the first two; nothing shows the card.
Separating the light from the display would also give findings 1 and 2 somewhere to be
seen.

## 5. After a wake the filter is "everything" until the app sends one — medium, to test

The allow-list survives a disconnect but not a restart, and a wake from standby is a
restart. Whether RaceChrono sends its filter again when it reconnects while recording is
not known (`device.cpp`, `onDisconnect()`). If it does not, a session that spans a key
cycle carries the whole bus, 506 frames a second, where it asked for about 91 — slower to
the phone and with the losses measured on 2026-10-19 at `flt ALL`, not a stop. Test: start
recording, let the board sleep and wake, and read the filter off the console.

## 6. Going to sleep in the middle of a card write — medium-low, to test with a card

`sdlog::stop()` waits at most a second for the card's task, and a card may take longer:
the SD library waits up to 500 ms per operation. After the second the chip sleeps
anyway. The file stays as it was at its last flush, five seconds old at worst, but the
card is left selected (the Atomic TF-Card Base ties chip select low) with its SPI pins
floating while the chip sleeps, so what state it is in at the next mount is not known.
A longer wait, and the three SPI pins held idle in the RTC domain during sleep (G6–G8
can be), would close it.

## 7. The TX pad's hold is released only on a wake — low, and free to fix

`standby::on_boot()` releases the RTC hold on CAN TX only when the wake cause is ext0.
If the chip ever started for another reason with the hold still on, the controller
would believe it was acknowledging while its pad stayed recessive: no acknowledgement
on the bus, the R9 down to its repeated frame, and nothing on the board saying so.
Releasing the hold on every start costs nothing.

## 8. Frames lost in the controller's receive FIFO are not counted — low

The data-overrun interrupt is off, so a frame lost because the interrupt ran late is
lost without a trace. The interrupt is not in IRAM, so a flash write delays it — and
NimBLE is built to store bonding keys in flash (`CONFIG_BT_NIMBLE_NVS_PERSIST`), if a
phone ever bonds. Whether the S3 counts overruns reliably would need checking before
turning it on.

## 9. Smaller things

- The card task's stack: 2 KB of 6 KB used, measured only with the stand-in card. The
  real FatFs path is deeper. A stack overflow resets the board (canary check), so this
  is a number to read with a card in.
- `sdlog::_mode` is a plain variable read by the CAN interrupt. Safe as long as
  `SDLOG.start()` runs before the controller starts, as it does; an atomic would keep
  it safe if that order changed.
- The log files carry no header: no format version, no record size. A later change of
  format could not be told from the file.

## Checked and sound

- With no computer on USB — the bike — `Serial.write()` and `Serial.flush()` return at
  once, on the Lite's own USB port (HWCDC) and the Feather's TinyUSB alike (core 3.3.11).
- The Bluetooth send path never blocks: a refused notification is counted and dropped.
- The logger's lock has a timeout and the logger does nothing in an interrupt.
- The frame queue and the card's ring both drop and count rather than block when full.
- The brownout detector is on; a stack overflow and a spinning task on core 0 both
  reset the board.
