# Testing on the bench

Everything Bluecan does can be tried without the bike: a USB-CAN adapter plays the bike,
generating traffic or replaying the R9's captured traffic, and the phone connects as it
would on the road. This page is how. The commands are run from the repository root on
Linux, or under WSL2 with the devices attached as in [build.md](build.md#under-wsl2).

## The rig

| | |
|---|---|
| USB-CAN adapter | a CANable 2.0 Pro, or any adapter that appears as a SocketCAN interface (`can0`). The CANable Pro is isolated, which also makes it the safer tool for looking at the bike's bus |
| Bus | a twisted pair from the adapter's `CAN H`/`CAN L` to the KAmod's. **Both ends terminated**: the KAmod's `120R` jumper on, and the adapter's terminator on |
| Software | `can-utils` (`candump`, `cansend`, `cangen`, `canplayer`) and `xz` |

```bash
sudo apt install can-utils xz-utils
```

### The CANable's firmware

The MKS CANable 2.0 Pro ships with a gs_usb firmware that stops delivering frames after the
third one. Flash [ElmüSoft Candlelight 2.5](https://github.com/Elmue/CANable-2.5-firmware-Slcan-and-Candlelight)
(`STM32G431 - Candlelight2.5 - Multiboard.dfu`) over DFU; it then appears as a native
`can0` with USB id `1d50:606f`. The upstream candleLight firmware does not support the
STM32G431 these boards use. The diagnosis is in
[history/gs-usb-stall.md](history/gs-usb-stall.md).

Two things to expect from it afterwards:

- A brief dip in its 5 V supply, even a wiggled USB plug, can drop it into the STM32 ROM
  bootloader. It then appears as `0483:df11` and no `can0` appears. Unplug it and plug it
  back in.
- In loopback mode `candump` shows every frame twice, the transmit echo and the received
  copy. That is correct.

## Bringing the interface up

```bash
sudo bluecan/firmware/tools/can-up.sh
```

That is `can0` at 500 kbps in normal mode. Use the script rather than a bare `ip link`:
SocketCAN keeps the previous mode flags unless they are named, so a loopback or listen-only
flag left over from an earlier run silently stays on. The script also raises the transmit
queue from 10 frames to 1000, without which `cangen` at full speed fails with
`No buffer space available`.

Other modes: `--loopback` (a self-test with no partner), `--listen-only`, `--one-shot`
(no retransmission; see below), `--down`.

With no adapter at all, a virtual interface is enough to try the log tooling:

```bash
sudo modprobe vcan && sudo ip link add dev vcan0 type vcan && sudo ip link set vcan0 up
```

## Traffic

**Replay the R9's own traffic.** The captures in [analysis/captures/](../analysis/captures/)
are `candump` logs. Replaying one drives the LIVE page through real values, and with
RaceChrono connected, the channels:

```bash
xz -dc analysis/captures/session-2026-10-11_200550/capture.log.xz | canplayer
```

The third capture, `session-2026-10-11_200550`, has the engine running and all 23 ids.
The first, `session-2026-10-11_115626`, has gear changes but little else. Under WSL2,
`canplayer` pauses for about 2 seconds every half minute or so, because it paces itself
on a clock that WSL2 steps back; the LIVE page goes to dashes during each pause. A replay
there is good for values, not for timing.

**A signal with a known value**, to check a RaceChrono channel definition before trusting
it on the bike:

```bash
bluecan/firmware/tools/sweep.sh --hz 5
```

It sends id `0x200` with a triangle wave 0–1000 in four encodings at once: bytes 0–1
big-endian, bytes 2–3 little-endian, byte 4 as 0–250, byte 5 inverted, and bytes 6–7 a
constant `A5 5A`. A channel that reads 0–1000 against it has the right offset, length,
byte order and equation.

**Load**, for throughput: `cangen can0 -g 5 -I 123 -L 8 -D i` sends about 200 frames a
second of one id; a smaller gap sends more, and `-g 0` saturates the bus at about 4000.

### Acknowledging on the bench

Bluecan acknowledges the adapter a fraction of a second after the first frames, once it
has proved the bit rate. Until then nothing acknowledges the adapter's frames, so the
adapter retransmits its first frame repeatedly; that is normal and stops by itself.

A Bluecan built without `CONFIG_CANBUS_ACK`, or one that has tripped back to listen-only,
never acknowledges. Against it, bring the interface up with `--one-shot`, or `cangen`
stalls on its first frame and nothing new reaches the bus. A stalled `cangen` needs
`kill -9`; `timeout` does not stop it.

The same start shows on the BUS page as a jump in `err` and in `ble lost`, once, and `rx`
ends a few hundred frames above what was sent. Until Bluecan acknowledges, the adapter
repeats its first frame back to back. The first attempts end in an error flag, which
`err` counts. After about sixteen the adapter is error-passive, its error flag is
recessive, and from then on Bluecan receives every repeat as a valid frame: a copy of the
same frame at the bus's full rate, for the 200 ms Bluecan takes to prove the bit rate.
With `flt ALL` they all go to Bluetooth, which refuses most of them.

Measured on 2026-10-17 against a freshly restarted Bluecan, the phone at 12.5 ms and
`flt ALL`, replaying the six ids RaceChrono has channels for from
`session-2026-10-17_123714`: the adapter sent 56 623 frames, Bluecan's `rx` read 57 415,
so 792 copies, about 200 ms of a full bus. It left `lost 656` and `err 301`, and neither
counter moved after the first second. A replay of the whole capture the same way left
`err 884` and `lost 297`. It is the start, not a fault. The bike's nodes will repeat
their first frame the same way, so `err`, and `lost` if a phone is already asking for
everything, can jump there too ([at-the-bike.md](at-the-bike.md)).

To see a trip: with Bluecan acknowledging, bring the adapter down and up again at
250 kbps and send. The BUS page goes to `TRIP-REC` in red and the LED starts its red
flash. A board that starts against a 250 kbps adapter never begins acknowledging.

## Tools

In [firmware/tools/](firmware/tools/). Each script's header says more.

| Tool | Does |
|---|---|
| `can-up.sh` | brings `can0` up in a named mode, as above. Needs `sudo` |
| `sweep.sh` | the known-value signal above. `--sidecar ref.csv` also writes the true value per frame |
| `bench.py` | serial console reader that asserts DTR. `bench.py 10` reads for 10 s. Also drives the `bluecan_bench` sketch's single-letter commands at set times |
| `ble-load.py` | ramps the bus rate against a connected, subscribed phone and reports where Bluetooth starts refusing frames. Needs a debug build and `sudo -v` first |
| `led-state.py` | prints what the LED is showing, in words, and can generate traffic to change it (`--rate 20`). Needs a debug build |
| `bluecan-soak.py` | holds the console open under load for as long as asked and reports whether the board restarted. Needs a debug build |
| `r9-decode-check.py` | checks the firmware's R9 decoder against [analysis/r9.dbc](../analysis/r9.dbc) over every captured frame; see [build.md](build.md#checking-the-decoder) |
| `reflash-bluecan.sh` | builds and uploads the Bluecan firmware under WSL2. `--debug` for a debug build |
| `usb-attach.sh` | attaches the Feather and the CANable into WSL2 through usbipd. `--list` shows what Windows sees |
| `serial-monitor.sh` | reads the console through Windows when USB is not attached to WSL2 |
| `check-numbers.sh` | lists every place in both repositories that quotes a measured figure from [history/numbers.md](history/numbers.md). For keeping documents consistent after a change |
| `build.sh`, `reflash.sh` | build and flash the sketches in this folder |
| `soak.sh`, `soak-report.py` | an hour-long two-node soak with the `bluecan_bench` sketch |
| `can-loopback-test.sh` | the adapter's acceptance test from the stall diagnosis |
| `clock-seed.py` | sets the `bluecan_clock` sketch's clock from the computer |
| `uf2conv.py` | converts a build to `.uf2`; see [build.md](build.md#by-uf2-with-no-tools-at-all) |

## Bench sketches

Three small sketches for the same board, in [firmware/](firmware/). They are not the
Bluecan firmware; flash it back afterwards.

| Sketch | For |
|---|---|
| [bluecan_bringup](firmware/bluecan_bringup/) | a self-test of the toolchain, the display, the buttons and the CAN controller. With a wire from `A1` to `A0` and no transceiver it tests the chip; with the KAmod wired and its terminator on, it tests the transceiver too. Ends at `CAN READY` |
| [bluecan_bench](firmware/bluecan_bench/) | a continuous receiver for two-node work: counts, ids, error counters, and a listen-only switch, driven over the serial console. It acknowledges |
| [bluecan_clock](firmware/bluecan_clock/) | a clock on the screen, set from the computer, for filming the dash with a time base shared with a `candump` log |

```bash
cd bluecan/firmware && ./build.sh bluecan_bringup --upload
```

Under WSL2, `./tools/reflash.sh bluecan_bench` does the same with the re-attaching.
