# Bluecan's clock, how far it drifts asleep — the bench check from 2026-10-21

What patch 47 left for a hand with a phone: how far the board's clock is off after hours
of standby, where it runs on the chip's RC oscillator rather than its crystal. The
answer decides how often the clock needs setting for a log's time to mean anything, and
whether step 2 of the plan, the time from RaceChrono, is worth doing.

The Feather is built as the Lite (`feather-as-atom`, release), with patch 47 on it since
2026-10-21 00:20. It sleeps after 60 s without a frame. It is powered from the
desktop's USB, so **the desktop has to stay on, and not suspend, from the setting to the
reading**: a USB port that loses its power is a power-on, and the clock starts again at
1970. The CANable stays quiet; a frame on the bus wakes the board, which does no harm but
is worth noting.

## 1. Set the clock

- [ ] Wake the board with a short press of D2, then hold D2 for 5 s: steady white, let
  go, purple.
- [ ] Join `bluecan-XXXX` from the phone. The page that opens has a **Clock** section:
  *Set from this phone just now.* and, the first time, *At that setting the board did
  not know the time.* Note the time: ______
- [ ] *Back to the normal mode*. A minute later the board is asleep.

## 2. Wait

Eight hours or more, the longer the better: the reading's own error is a few tens of ms
(the phone's clock and the request), against which 8 hours is 36 000 s.

## 3. Read

- [ ] Wake, hold D2, join, as in 1. The first page that opens says: *At that setting it
  was X ahead of / behind the phone, Y after the setting before: Z ppm.* Write it down
  as it stands:

  ______

  Opening the page again within ten minutes does not set the clock again, and the line
  stays.

## What the number means

At 50 ppm a day costs about 4 s, enough to tell one ride from another for weeks. At
500 ppm, about 43 s a day, a log's time is still right to the hour for a month, and the
setting at each download keeps it there. Several hundred ppm or more over a night would
be a reason to look at setting it more often.
