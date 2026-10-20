# Bluecan Wi-Fi mode, the bench checks of 2026-10-20

What patches 39–41 left for a hand at the bench: everything that needs the button, a
phone, or the Feather out of the USB state it was left in overnight. In the order to do
them. The Feather is built as the Lite (`feather-as-atom`), so D2 is the button; the
CANable plays the bike, in one-shot mode (`sudo bluecan/firmware/tools/can-up.sh
--one-shot`), with `cangen can0 -g 2 -I 100 -L 8 -D r` for traffic.

The Wi-Fi password is in `bluecan/firmware/bluecan/src/web/secret.hpp` on the desktop,
not in git.

## 0. Flash patch 41

The Feather was left awake but with its USB port failed (Windows: `Device Descriptor
Request Failed`). Hold **D0**, press and release **RESET**, let go of D0: download mode.
Then:

```bash
bluecan/firmware/tools/reflash-bluecan.sh feather-as-atom --debug
```

- [x] Uploaded, by the owner, after a reconnect of the Feather.

## 1. Wakes keep USB (patch 41)

Run `bluecan/firmware/tools/console-log.sh` in a terminal of its own and `tail -f
bluecan/firmware/build/console.log` in another. Stop traffic; after 60 s the LED goes
out and the console says `Standby: no frame for 60 s, sleeping`.

- [x] Woken by traffic, five times: the console came back by itself each time. But
  Windows reported *USB device not recognized* at every sleep: the D+ pull-up stayed on.
  Fixed in patch 41 (pulls off first); then four in four, nothing listed while asleep.
- [ ] Woken by a short press of D2, five times: `Standby: woken by the button`, the LED
  blue, asleep again 60 s later.
- [ ] If any wake fails as before, note which and how many: that decides between this
  fix and the next idea.

## 2. The hold

With the board awake (traffic or a press of D2 first):

- [ ] A short press of D2 does nothing visible.
- [ ] Held: at five seconds the LED turns **steady white**. The console says `Button:
  held 5 s, let go to switch`.
- [ ] Let go: it restarts; the LED **breathes purple**. Console: `Mode: Wi-Fi`, then the
  network's name and `http://192.168.4.1/`.
- [ ] Held again for five seconds from the Wi-Fi mode: white, then back to blue.
- [ ] From asleep: a short press, then hold the button down for five seconds, all in
  one go or as two presses; either way it should end in purple.

## 3. The phone

In the Wi-Fi mode:

- [ ] The network `bluecan-C5D8` is listed, asks for a password, and the password works.
- [ ] With the phone on it, the LED is **steady purple**.
- [ ] `http://192.168.4.1/` opens; `live` in green at the top, and the numbers move
  every second. With traffic on: frames a second near what `cangen` sends, the id
  table filling.
- [ ] Card: `No card.`, or, in a build with `--sd` and a card wired to A2–A5, the list.
- [ ] The page's *Back to the normal mode*: a confirmation, then the note that it is
  restarting; the LED blue within a few seconds.
- [ ] RaceChrono, if it was connected before the switch, connects again by itself.
- [ ] Phone left on the network, page closed: back in the normal mode about five minutes
  after the last request (measured on 2026-10-20 without a phone: 300 s).

## 4. A download, if a card is at hand

Needs a microSD card on A2–A5 and a build with `--sd`:

```bash
bluecan/firmware/tools/reflash-bluecan.sh feather-as-atom --debug --sd
```

- [ ] In the normal mode, traffic for a minute: the console says `SD log: writing
  /bluecan-NNNN.bin`.
- [ ] Switch to Wi-Fi; the page lists the file with a size.
- [ ] Download it on the phone or a laptop; the size matches.
- [ ] `bluecan/firmware/tools/sdlog-to-candump.py bluecan-NNNN.bin | head` shows the
  traffic sent.
- [ ] How long a large file takes, if there is one: MB per second.

## 5. The Feather with its screen

```bash
bluecan/firmware/tools/reflash-bluecan.sh feather --debug
```

- [ ] A short press of D2 still freezes the screen.
- [ ] D2 held five seconds: white, then the Wi-Fi mode; the header says `Wi-Fi` in
  magenta.
