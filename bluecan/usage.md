# Using Bluecan

What the board does from power-on, what its screen, buttons and LED say, how RaceChrono
connects to it, and what to expect when something is wrong. The figures are from the
bench; it has not been used on the R9 itself.

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
5. **Ten seconds after the screen comes up** it clears and shows `Zzzzz....` in the
   middle for five seconds, then goes dark. The LED stays on, as in D0's LED-only
   position. The first press of **D0** lights the screen again; from then on D0 works as
   described under [The buttons](#the-buttons). A press of D0 before the screen goes dark
   keeps it lit.

It never transmits a frame of its own. What it puts on the wire is the acknowledge bit
and, when it receives a broken frame, an error flag.

### When it stops acknowledging

It goes back to listening only if any of these happens, and starts again as it does at
start-up: after a second it proves the bus — 32 intact frames over at least 200 ms with
no error among them — and acknowledges again. It checks every 10 ms:

| BUS page shows | Cause |
|---|---|
| `TRIP-REC` | its receive error counter reached 32 |
| `TRIP-STATE` | the controller reported error-warning or bus-off |
| `TRIP-TEC` | its transmit error counter moved at all, which should be impossible for a node that never transmits |

On the R9 telemetry stops while it listens: without an acknowledgement the bike's engine
control unit repeats one frame, and only 2 of its 23 ids get through. On a clean bus that
lasts a little over a second. The BUS page shows the trip in red until the board is
acknowledging again; the LED's red flash stays until the board is restarted, as a record
that it happened. If the trip word keeps coming back, the bus is not clean: leave the
board off the bike and look at the bus with a separate adapter.

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
RPM   8450 Gear    3
Grip   42% TPS   38%
Front   97 Rear   97
PWR     2T TCS     7
Water  76C  Air  17C  TPS cmd  14.5%
```

| Field | Shows | From |
|---|---|---|
| `RPM` | engine speed | `0x27C` |
| `Gear` | `N`, `1`–`6`, `-` while between gears | `0x27C`, 100 Hz |
| `Grip` | throttle grip, %. Reads 0 for the first twentieth of its travel | `0x758`, 20 Hz |
| `TPS` | throttle valve as measured, %, the throttle position sensor in Yamaha's naming. Its zero is provisional: a warm idle reads about 8% | `0x758` |
| `Front`, `Rear` | wheel speeds, whole km/h, the front on the left. The scale is held to the rear wheel's own pulses on a stand; the dash reads about 10 % higher, as speedometers do. Both have been seen to 59 km/h. On a stand with the rear turning alone, `Front` shows the rear's speed after a few seconds ([open question 8](../analysis/open-questions.md)) | `0x765`, 10 Hz; `0x756`, 20 Hz |
| `PWR` | the PWR setting, 1–4, as on the dash. `T` right after the number while the dash is on its Track display. Which mode is selected is not on the bus, only the settings it makes | `0x27C`; `T` from `0x76E`, 2 Hz |
| `TCS` | the traction control setting, as on the dash | `0x27C` |
| `Water`, `Air` | coolant and intake air temperature, °C, on the small line. The dash shows the air temperature 1–2 °C lower | `0x512`, 1 Hz |
| `TPS cmd` | throttle valve as the engine control unit commands it, %, on the small line | `0x759`, 20 Hz |

**A value that stops arriving turns to grey dashes**: after half a second for `0x27C`,
`0x756`, `0x758`, `0x759` and `0x765`, after three seconds for `0x512`. `T` disappears
three seconds after `0x76E` stops. With the key off, the whole page
goes to dashes.

The page is decoded on the board, separately from what is sent to the phone, so it reads
the same whether or not a phone is connected and whatever the phone has asked for. How
sure each value is: the [root README](../README.md#what-is-decoded-so-far).

### BUS — the device's state

An example; the figures are illustrative.

```
CAN 500k ACK         RUN        4.09V
rx       123456   506/s
fwd       98765    88/s flt 6
ble       98760    88/s lost 0
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
| `ext`, `rtr` | 29-bit and remote frames received and discarded. `tec`, `rec`: the controller's transmit and receive error counters. While it only listens, `tec` reads 191: the counter is parked there to keep the controller from looping on its own error flags |
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
| **D0** | cycles: screen and LED on → LED only, screen off → both off → back. Everything else keeps running. Use LED only on the bike. After boot the screen goes dark by itself ([From power-on](#from-power-on), step 5), and the first press lights it |
| **D1** | next page |
| **D2** | freezes the screen, to read a frame off the LAST page; the footer says `(held)`. Counting continues behind it. Press D2 again, or D1, to unfreeze. **Held for 5 seconds** and let go, it switches to the [Wi-Fi mode](#the-wi-fi-mode) and back |

**RESET** restarts the board. Pressed twice quickly, it enters the UF2 bootloader
([build.md](build.md#by-uf2-with-no-tools-at-all)); press RESET once more to leave it.

## Standby

The AtomS3 Lite, and the Feather built as one (`feather-as-atom`), sleep while the bus is
silent, since the Lite is powered from the bike's battery with the key off. The `feather`
profile, with its screen, does not.

- **A minute after the last frame** the board puts its LED out and sleeps: with the key
  off, or with the connector pulled.
- **The first frame wakes it**, as the key is turned on. It starts again from the top
  and acknowledges about three quarters of a second after the frame: on the bench,
  508–509 ms after the program started, behind about 250 ms of bootloader. The R9
  repeats its first frame until something acknowledges it, so nothing that matters is
  missed.
- **A press of the button wakes it too**: the Lite's one button, or **D2** on the Feather
  built as one. It then stays awake for a minute with the bus silent, as it does after
  any frame, and sleeps again.

The sleep is light sleep, not deep sleep: the Lite's button cannot wake the chip from
deep sleep. What either costs the bike's battery is estimated in
[hardware.md](hardware.md#current-while-asleep).

## The microSD log

With a microSD card on the board's card pins, the AtomS3 Lite's TF-Card Base
([hardware.md](hardware.md#the-m5stack-atoms3-lite)) or, on the Feather, a card on SCK
A2, MOSI A3, MISO A4 and CS A5, the board logs every frame it receives to the
card, whatever RaceChrono has asked for: a new file, `bluecan-NNNN.bin`, at every start.
It looks for the card once, as it starts, and without one goes on with no log; a card
put in later is used from the next start, which on the bike is the next wake. The files
are downloaded from the [Wi-Fi mode](#the-wi-fi-mode). The card must be FAT32. It has not
yet been run with a card.

## The Wi-Fi mode

In place of Bluetooth, the board can put up a Wi-Fi network of its own with a web page on
it: the bus's statistics, and the microSD card's log files to download. Never both at
once: the chip has one radio, and the normal mode is built to get every frame through it
to the phone.

**To switch**, hold the button for five seconds: the Lite's one button, D2 on the Feather.
At five seconds the LED turns **steady white**; let go, and the board restarts in the
Wi-Fi mode, its LED **breathing purple**, steady purple once something has joined the
network. A board asleep is woken with a short press first. Holding the button again the
same way switches back.

**To connect**, join the network `bluecan-XXXX` (the last four hex digits of the board's
MAC) with the password from `bluecan/firmware/bluecan/src/web/secret.hpp`, which the
build script wrote and printed the first time it built ([build.md](build.md#build-time-settings)).
The phone then opens the page by itself, as it does for a hotel's network that wants a
sign-in: Android offers *Sign in to network*, and the page opens in that window. It works
there as in a browser. Otherwise open <http://192.168.4.1/>; any other address typed
there leads to the page too. The network has no internet, and a phone may ask whether
to stay on it.

**The page** updates every second:

| Section | Shows |
|---|---|
| Bus | whether the board acknowledges, frames a second and in total, error interrupts, TEC and REC, trips, 29-bit and remote frames, frames dropped |
| Bike | engine speed, gear, both wheel speeds, water and air temperature, from the frames the LIVE page decodes; `-` when a frame has not arrived for two seconds |
| Ids | every id seen since the start: how many a second, how many in all, how long since the last, its last payload |
| Card | the log files, `bluecan-NNNN.bin`, newest first, each with a link to download it. Only when the board found a card as it started the mode |
| Board | free memory, and how many devices are on the network |

While the page is out of sight, behind another app or in a background tab, it stops
asking, so that a page left open does not keep the board in this mode; it starts again
when it comes back. If the board has gone back to the normal mode meanwhile, or does not
answer for three seconds, a red bar across the top says so: the page is dead.

Nothing on the page writes anything, and files cannot be deleted from it. The card is
not written in this mode: frames on the bus meanwhile are not logged. A downloaded file
is 24 bytes a frame; [firmware/tools/sdlog-to-candump.py](firmware/tools/sdlog-to-candump.py)
turns it into a candump log.

**Back to the normal mode** by a restart, whichever comes first:

- the page's button *Back to the normal mode*;
- the board's button held for five seconds again;
- five minutes without a request to the page: the page closed or out of sight, or the
  phone's browser shut while the phone stays on the network. The phone's own checks of
  whether the network reaches the internet do not count;
- the rear wheel above 10 km/h for a second: the bike is being ridden. RaceChrono, if it
  is waiting, connects again by itself.

Any other restart, a reset or the power, also comes back in the normal mode. In the
Wi-Fi mode the board does not sleep, so that a download is not cut off with the key
off; the five minutes are what stop it running on.

## The LED

One RGB LED, readable at a glance. The colour is the state of the chain from the bus to
the phone; the blinking is frames moving. It never shows bike data. On the Feather it is
the NeoPixel beside the screen; on the AtomS3 Lite, the LED under the button, with the
same colours (built, not yet seen on a Lite). A board that sleeps while the bus is silent
puts it out first.

| LED | Meaning |
|---|---|
| white, breathing | starting up; the CAN controller is not running yet |
| blue, breathing | no phone subscribed. Frames may well be arriving |
| yellow, blips | a phone is subscribed and asking for everything — RaceChrono while you set up channels |
| green, blips | a phone is subscribed with a filter — RaceChrono recording |
| yellow or green, a double flash every 2 s | subscribed, and **no frames for more than 2 seconds**: the bus has gone quiet, the key is off, or the connector has come out |
| amber, blinking 1 Hz | the CAN controller's error counters have reached its warning limit — BUS shows `ERRORS` |
| red, solid | the CAN controller is bus-off |
| white, steady | the button has been held for five seconds: let go to switch modes |
| purple, breathing | the [Wi-Fi mode](#the-wi-fi-mode), nobody on its network |
| purple, steady | the Wi-Fi mode, a device on its network |
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
(`flt 6` for the R9's channels as entered in [racechrono-channels.md](racechrono-channels.md)). Bluecan applies the filter in the CAN receive
interrupt, so an id nobody asked for costs nothing further. Before any app has asked,
everything is forwarded.

One phone at a time. A second connection lights the red flash.

## Limits

| | |
|---|---|
| CAN | 500 kbps, standard 11-bit ids. The bit rate is fixed in the firmware |
| The R9's diagnostic bus | 23 ids, 506 frames a second |
| Filter | up to 32 ids. Each is sent at most once per interval the app asks for; measured within 3% of 20 Hz at a 50 ms request |
| Bluetooth throughput | without a filter, clean to about 590 frames a second; past that the Bluetooth stack starts refusing frames (0.6% at 718, 22% at 914). The R9's 506 fit. Recording the R9's channels is about 91 frames a second, 88 measured against a replay ([racechrono-channels.md](racechrono-channels.md#load-on-the-link)) |
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
| `TRIP-REC`, `TRIP-STATE` or `TRIP-TEC` in red | the board took itself off the bus; it proves the bus again and rejoins by itself | note which, and `rec`. If the word keeps coming back, stop |
| A warning on the bike's dash that was not there before | possibly the board | key off, unplug the board, key on and see whether it clears. Do not plug it back in that day |
| Header `connected`, not `SUB`; the app shows nothing | the app has not subscribed. Usually Monitor was ticked when the device was added | remove the device in RaceChrono and add it again with CAN-Bus only; or disconnect and reconnect |
| `ble lost` climbing while the app is configuring channels | the app asked for everything and the bus is faster than Bluetooth carries | expected above about 590 frames a second; does not happen on the R9's 506. Not a fault |
| `ble lost` climbing while recording | the link cannot keep up with what was asked for | check the interval in the header; fewer channels or a longer interval in the app |
| LIVE shows dashes for a moment and recovers | the frames stopped for more than half a second | on the bike that is worth noting: when, and what `rx` did |
| `flt 32!` in red | the app asked for more than 32 ids | fewer CAN channels |
| `peer 2` in red | two phones connected | disconnect one |
| `up` has gone back to zero | the board restarted | note when; a debug build with the console attached may show why |
