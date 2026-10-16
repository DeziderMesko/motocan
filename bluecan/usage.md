# Using Bluecan

What the board does from power-on, what its screen, buttons and LED say, how RaceChrono
connects to it, and what to expect when something is wrong. The figures are from the
bench, with the firmware as of 2026-10-14; it has not yet been used on the R9 itself.

## From power-on

1. **After about 1.5 seconds** (6.5 in a debug build) the screen comes up on the LIVE
   page with grey dashes and the header `bluecan advertising`, and the LED breathes white
   while Bluetooth and the CAN controller start. The board is now visible to RaceChrono.
2. **The CAN controller starts listening.** It receives but sends nothing, not even an
   acknowledgement. The LED breathes blue: no phone yet.
3. **Frames arrive and it starts acknowledging.** Once it has received 32 frames intact,
   over at least 200 ms, with no error among them, it switches to normal mode and
   acknowledges every frame it receives. At the wrong bit rate no frame arrives intact, so
   this is the proof that the bus is the one it expects. On the bench it takes a fraction
   of a second from the first frame; on the R9 it should happen as soon as the key is
   turned on. The BUS page then reads `ACK`.
4. **The bike's values appear** on the LIVE page as their frames arrive.

It never transmits a frame of its own. What it puts on the wire is the acknowledge bit
and, when it receives a broken frame, an error flag.

### When it stops acknowledging

It goes back to listening only, **until it is restarted**, if any of these happens. It
checks every 10 ms:

| BUS page shows | Cause |
|---|---|
| `TRIP-REC` | its receive error counter reached 32 |
| `TRIP-STATE` | the controller reported error-warning or bus-off |
| `TRIP-TEC` | its transmit error counter moved at all, which should be impossible for a node that never transmits |

On the R9 this means telemetry stops: without an acknowledgement the bike's engine
control unit repeats one frame, and only 2 of its 23 ids get through. The LED flashes red
over its colour. To try again, restart the board (RESET or unplug it). If it trips again,
leave it off the bike and look at the bus with a separate adapter.

## The screen

**D1** steps through four pages: LIVE, BUS, IDS, LAST. The board always starts on LIVE.
The page name is in the footer.

### The header, on every page

| Header | Meaning |
|---|---|
| `bluecan advertising` (yellow) | no phone connected |
| `bluecan connected` (red) | a phone is connected **but has not subscribed**, so it receives nothing. See [Troubleshooting](#troubleshooting) |
| `bluecan SUB 15.0ms` (green) | a phone is subscribed and receiving. The number is the connection interval the phone chose |

### LIVE — the bike's values

```
bluecan advertising
Gear 3     RPM  8450
Grip  42%  Valve  38%
Water 76C  Air   17C
TCS 7      Mode 3
Valve commanded  14.5 %
```

| Field | Shows | From |
|---|---|---|
| `Gear` | `N`, `1`–`6`, `-` while between gears | `0x27C`, 100 Hz |
| `RPM` | engine speed | `0x27C` |
| `TCS` | the traction control setting, as on the dash | `0x27C` |
| `Mode` | the PWR setting less one, as the bike sends it: PWR 1 shows `0`, PWR 4 shows `3` | `0x27C` |
| `Grip` | throttle grip, %. Reads 0 for the first twentieth of its travel | `0x758`, 20 Hz |
| `Valve` | throttle valve as measured, %. Its zero is provisional: a warm idle reads about 8% | `0x758` |
| `Water`, `Air` | coolant and intake air temperature, °C. The dash shows the air temperature 1–2 °C lower | `0x512`, 1 Hz |
| `Valve commanded` | throttle valve as the engine control unit commands it, % | `0x759`, 20 Hz |

**A value that stops arriving turns to grey dashes**: after half a second for `0x27C`,
`0x758` and `0x759`, after three seconds for `0x512`. With the key off, the whole page
goes to dashes.

The page is decoded on the board, separately from what is sent to the phone, so it reads
the same whether or not a phone is connected and whatever the phone has asked for. How
sure each value is: the [root README](../README.md#what-is-decoded-so-far).

### BUS — the device's state

An example; the figures are illustrative.

```
CAN 500k ACK         RUN        4.09V
rx       123456   506/s
fwd       98765    41/s flt 3
ble       98760    41/s lost 0
queue    0 peak    3/2000 peer 1
drop 0  err 0
ext 0 rtr 0 tec 0 rec 0
free 180k  min 175k  disp stck 1824
draw 0.4ms max 254.0ms        up  12:34
```

| Line | Meaning |
|---|---|
| `CAN 500k` | the bit rate. Then the mode — `LISTEN-ONLY`, `ACK`, or a trip in red (see [above](#when-it-stops-acknowledging)) — and the controller's state: `RUN` (green), `ERRORS` (amber: an error counter has reached the controller's warning limit), `BUS-OFF` (red), `DOWN` (not started). At the right, the battery rail voltage ([hardware.md](hardware.md#power)): green, amber below 3.7 V, red below 3.5 V |
| `rx` | frames received since start-up, and per second |
| `fwd` | frames passed on towards the phone, and per second. `flt` is the app's filter: `ALL`, or the number of ids it asked for. `flt 32!` in red means it asked for more than 32 |
| `ble` | frames sent over Bluetooth, and per second. `lost` counts frames the Bluetooth stack refused because it was full; red when not zero |
| `queue` | frames waiting between the CAN receiver and Bluetooth, the highest it has been, and its capacity. `peer` is the number of connected phones; red above 1 |
| `drop` | frames lost because the queue was full. `err` counts CAN error interrupts. Both green at zero, red otherwise |
| `ext`, `rtr` | 29-bit and remote frames received and discarded. `tec`, `rec`: the controller's transmit and receive error counters |
| `free`, `min` | free heap now and its lowest. `disp stck`: free stack of the display task, in bytes |
| `draw` | how long the last repaint took, the longest so far, and the uptime in minutes and seconds |

All counters run from start-up and never reset; a restart is the only way to zero them,
and `up` going back to zero is how a restart shows.

`rx` counts everything received; `fwd` only what the app's filter lets through. While a
phone is recording, `fwd` is much lower than `rx`, which is correct.

### IDS — which ids are on the bus

Three columns of id and frames per second, up to 24 ids on the page. The top line, `ids
seen 23`, counts distinct ids forwarded since start-up; `(+more)` means more than fit. It
records up to 32.

**Use it as a wiring check with no phone recording.** 23 ids means the board is
acknowledging and getting the R9's full diagnostic traffic; 2 means it is not. The count
never goes down, and it counts forwarded frames, so once a phone has started recording,
ids it did not ask for stay in the count with a rate of 0, and a board that met a
recording phone before the bus would count only the phone's ids. The BUS page's `ACK` is
the reliable reading either way.

### LAST — the last frames

The six most recent forwarded frames, newest first: id, length, payload in hex.

## The buttons

| Button | Does |
|---|---|
| **D0** | cycles: screen and LED on → LED only, screen off → both off → back. Everything else keeps running. Use LED only on the bike |
| **D1** | next page |
| **D2** | freezes the screen, to read a frame off the LAST page; the footer says `(held)`. Counting continues behind it. Press D2 again, or D1, to unfreeze |

**RESET** restarts the board. Pressed twice quickly, it enters the UF2 bootloader
([build.md](build.md#by-uf2-with-no-tools-at-all)); press RESET once more to leave it.

## The LED

One RGB LED, readable at a glance. The colour is the state of the chain from the bus to
the phone; the blinking is frames moving. It never shows bike data.

| LED | Meaning |
|---|---|
| white, breathing | starting up; the CAN controller is not running yet |
| blue, breathing | no phone subscribed. Frames may well be arriving |
| yellow, blips | a phone is subscribed and asking for everything — RaceChrono while you set up channels |
| green, blips | a phone is subscribed with a filter — RaceChrono recording |
| yellow or green, a double flash every 2 s | subscribed, and **no frames for more than 2 seconds**: the bus has gone quiet, the key is off, or the connector has come out |
| amber, blinking 1 Hz | the CAN controller's error counters have reached its warning limit — BUS shows `ERRORS` |
| red, solid | the CAN controller is bus-off |
| a short red flash once a second, over any of the above | **something was lost** since start-up: a frame dropped from the queue, more filter requests than fit, a second phone connected, frames refused by Bluetooth while recording, or the board stopped acknowledging. It stays until a restart. The BUS page shows which |

The blips go faster with more frames: about 2 a second at the 20 frames a second a
recording session sends per id, up to 5 a second from 40 frames a second up.

Frames refused by Bluetooth while RaceChrono asks for everything are expected and do not
light the red flash; see [Limits](#limits).

## Connecting RaceChrono

You need **RaceChrono Pro**. The free version does not add DIY devices.

- The board advertises as **`RaceChrono XX:XX:XX`**, the last three bytes of its Bluetooth
  address. There is no pairing and no PIN.
- **Add it from inside RaceChrono**, as a DIY Bluetooth LE device, not from the phone's
  Bluetooth settings.
- When the app asks what the device provides, **tick CAN-Bus only.** Bluecan has no GPS
  and no "Monitor" function. With Monitor ticked the app connects and never subscribes:
  the header stays `bluecan connected` and nothing arrives.
- Enter the channels in the vehicle profile, under **CAN-Bus channels**. The R9's are in
  [racechrono-channels.md](racechrono-channels.md), PID in decimal, equations as the app
  accepts them.
- RaceChrono needs a position source of its own, the phone's GPS or an external receiver.
  Without a GPS fix its live screen shows an empty Time tile and no rates under the CAN
  channels; the values themselves still show. That this is the GPS fix is likely and not
  yet confirmed.

**What the app asks for depends on what it is doing.** While you configure channels or
test the connection, it asks for every id at 50 ms (`flt ALL`). When it records, it first
denies everything and then allows each id it has a channel for, with its interval
(`flt 3` for the R9's channels as entered). Bluecan applies the filter in the CAN receive
interrupt, so an id nobody asked for costs nothing further. Before any app has asked,
everything is forwarded.

One phone at a time. A second connection lights the red flash.

## Limits

| | |
|---|---|
| CAN | 500 kbps, standard 11-bit ids. The bit rate is fixed in the firmware |
| The R9's diagnostic bus | 23 ids, 506 frames a second |
| Filter | up to 32 ids. Each is sent at most once per interval the app asks for; measured within 3% of 20 Hz at a 50 ms request |
| Bluetooth throughput | without a filter, clean to about 590 frames a second; past that the Bluetooth stack starts refusing frames (0.6% at 718, 22% at 914). The R9's 506 fit. Recording the R9's channels is about 41–61 frames a second |
| Connection interval | chosen by the phone. The firmware asks for 7.5–12.5 ms; an Android phone has given 11.25, 12.5 and 15 ms on different connections. Throughput depends on it |
| Queue | 2000 frames between the CAN receiver and Bluetooth, about 4 seconds of the R9's whole bus |
| Display | costs nothing measurable: frames are not lost while it repaints |
| Endurance | 600 s at 919 frames a second with no phone, none dropped; 150 s at 476 frames a second with a phone, the longest run with one. An hour of bench traffic on the bench sketch, none lost |
| Phone platform | Android tested. iPhone needs the connection interval setting changed ([build.md](build.md#build-time-settings)) and has not been tried |

## Troubleshooting

| What you see | Likely cause | What to do |
|---|---|---|
| Nothing on the serial console | the release build prints almost nothing, and nothing prints without DTR | use a debug build and [bench.py](firmware/tools/bench.py); see [build.md](build.md#the-serial-console) |
| The board powers but cannot be flashed or found on USB | a charge-only cable | use a data cable |
| BUS stays `LISTEN-ONLY`, `rx 0`, `err 0`, with the key on | not connected to the CAN pair, or `CAN H` and `CAN L` swapped | key off, check the pins and the resistance ([hardware.md](hardware.md#termination)), swap H and L |
| BUS stays `LISTEN-ONLY`, `rx` in the thousands per second, IDS shows 2 | frames are arriving but the bit-rate proof does not complete; `err` will be climbing | key off and note `err`. Not seen so far; look at the bus with a separate adapter |
| `TRIP-REC`, `TRIP-STATE` or `TRIP-TEC` in red | the board took itself off the bus | note which, and `rec`. Restart once. If it trips again, stop |
| A warning on the bike's dash that was not there before | possibly the board | key off, unplug the board, key on and see whether it clears. Do not plug it back in that day |
| Header `connected`, not `SUB`; the app shows nothing | the app has not subscribed. Usually Monitor was ticked when the device was added | remove the device in RaceChrono and add it again with CAN-Bus only; or disconnect and reconnect |
| `ble lost` climbing while the app is configuring channels | the app asked for everything and the bus is faster than Bluetooth carries | expected above about 590 frames a second; does not happen on the R9's 506. Not a fault |
| `ble lost` climbing while recording | the link cannot keep up with what was asked for | check the interval in the header; fewer channels or a longer interval in the app |
| LIVE shows dashes for a moment and recovers | the frames stopped for more than half a second | on the bike that is worth noting: when, and what `rx` did |
| `flt 32!` in red | the app asked for more than 32 ids | fewer CAN channels |
| `peer 2` in red | two phones connected | disconnect one |
| `up` has gone back to zero | the board restarted | note when; a debug build with the console attached may show why |
