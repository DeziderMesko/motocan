# Development environment (Windows + WSL2)

Development happens inside WSL2 (Ubuntu 24.04) on a Windows host. The repository,
the toolchain and the build all live on the Linux side; only the USB endpoint is
inherently Windows-owned.

## The WSL2 USB problem

WSL2 runs in a lightweight VM with no USB host controller, so `/dev/ttyACM*` never
appears no matter what is plugged in. There are two ways around it, and this project
uses both.

### usbipd-win — the real fix

[usbipd-win](https://github.com/dorssel/usbipd-win) forwards a USB device into the VM
over IP. Whether it is worth installing hinges on one thing that is easy to miss:
older WSL2 kernels shipped **without** `CONFIG_USB_ACM`, so the device would forward
but no serial node would ever be created, and people ended up building custom kernels.
This install's kernel (`6.18.33.2-microsoft-standard-WSL2`) has what is needed:

```
CONFIG_USB_ACM=m              cdc-acm — the ESP32-S3's native USB CDC
CONFIG_USB_SERIAL_CP210X=m    typical USB-serial bridges on cheap dev boards
CONFIG_USB_SERIAL_CH341=m
CONFIG_USB_SERIAL_FTDI_SIO=m
CONFIG_CAN=m / CAN_RAW / CAN_DEV / CAN_NETLINK
CONFIG_CAN_GS_USB=m           candleLight — makes the CANable a native can0
CONFIG_CAN_SLCAN=m            the CANable's other firmware mode
CONFIG_CAN_VCAN=m             virtual CAN, for testing with no hardware at all
usbip-core / usbip-host / vhci-hcd
```

The deciding argument for installing it was not the Feather — that can be flashed over
UF2 without any passthrough — but the **CANable v2.0 Pro**. The bench rig needs two
devices live at once, and the UF2 trick only exists for the Feather. Without
passthrough there is no way to reach the CANable at all.

WSL-side setup (done):

```bash
sudo apt install -y linux-tools-virtual hwdata
sudo update-alternatives --install /usr/local/bin/usbip usbip "$(ls /usr/lib/linux-tools/*/usbip | tail -1)" 20
sudo usermod -aG dialout $USER
```

The `usbip` client that gets selected comes from a `linux-tools` package built for a
different kernel version (`6.8.0-138-generic`) than the running WSL kernel. That
mismatch is normal and harmless — the userspace tool talks to `vhci-hcd` over sysfs,
not through a version-locked ABI.

Installed once per Windows machine, from an **administrator** PowerShell:

```
winget install --exact dorssel.usbipd-win
```

Reopen the PowerShell afterwards so it picks up the new `PATH`. `usbipd` is a Windows
program; the `usbip` inside WSL is the other end of the link, not the same tool.

Per-device, from an **administrator** PowerShell on Windows:

```
usbipd list
usbipd bind --busid <BUSID>                      # once per device, persists
usbipd attach --wsl --busid <BUSID> --auto-attach # per session
```

`--auto-attach` matters for the ESP32-S3 specifically: its USB is native to the SoC, so
resetting into the bootloader drops it off the bus and it returns with a different PID
(`239A:8123` → `303A:1001`) mid-upload. If an upload still fails, hold **D0/BOOT** and
tap **RESET** to put the board in download mode *before* attaching, so nothing
re-enumerates partway through.

### After a Windows reboot

`bind` persists, `attach` does not. So the routine is just:

```bash
bluecan/firmware/tools/usb-attach.sh
```

That script needs no administrator. It only sends you back to an admin PowerShell if a
device was never bound at all, in which case it prints the exact `usbipd bind` line to
run.

### The ESP32-S3 identity switch — bind BOTH

Confirmed in practice, not just in theory. The board presents three different USB
identities, and `usbipd bind` applies to a *device*, not to a physical port, so binding
one identity does nothing for the others:

| State | VID:PID | How you get there |
|---|---|---|
| Running a sketch | `239a:8123` | normal boot |
| ROM download mode | `303a:1001` | esptool toggling DTR/RTS, or BOOT+RESET |
| TinyUF2 bootloader | `239a:0123` | double-tap RESET |

A serial upload walks straight from the first into the second, mid-operation. If only
`239a:8123` is bound, the device vanishes from WSL the instant esptool resets it and the
upload dies with `Could not open /dev/ttyACM0`:

```
1-5  303a:1001  USB Serial Device (COM4), USB JTAG/serial debug unit  Not shared
```

The fix is to bind the download-mode identity too. It has to be done while the board is
*in* that mode, since usbipd can only bind a device it can currently see. Get there with
**hold D0/BOOT, tap RESET**, then from an admin PowerShell:

```
usbipd bind --busid 1-5
```

Both bindings then persist and serial uploads work unattended from then on.

### Solved: gs_usb stalled after 3 frames over usbip

For a while the CANable attached and `can0` came up at 500 kbps, but exactly three frames
completed per channel open and nothing completed afterwards. It looked like a usbip
delivery problem. It was not: the adapter's shipped firmware stops servicing its bulk IN
endpoint after three deliveries that had to wait for data, and only a channel-stop
un-wedges it. Reflashing to **ElmüSoft Candlelight 2.5** fixed it, and the acceptance test
(`cangen -n 200 -g 10`, then `-n 500 -g 2`) now passes with every frame delivered on a
single channel open.

Written up in full — reproduction, evidence, everything ruled out, and the reflash — in
**[gs-usb-stall.md](gs-usb-stall.md)**. Worth reading before trusting any usbip-shaped
hypothesis on this machine: usbipd-win, vhci-hcd and gs_usb were each exonerated by
measurement rather than by assumption.

One behaviour change to expect from the new firmware: it reports device loopback
properly, so in loopback mode `candump` shows every frame **twice** (TX echo plus the
looped-back RX) and the counters read 2×. That is correct, not duplication.

### TinyUF2 — the fallback that needs nothing

Kept because it costs nothing and is immune to every problem above: the Feather's
TinyUF2 bootloader exposes a `FTHRS3BOOT` mass-storage volume that Windows mounts and
WSL reaches under `/mnt`. Copying a `.uf2` there flashes it. Serial can likewise be
read without passthrough by letting Windows hold the COM port and piping it across
through interop — see [firmware/tools/serial-monitor.sh](firmware/tools/serial-monitor.sh).

## Toolchain (installed, no root required)

| Component | Version | Location |
|---|---|---|
| `arduino-cli` | 1.5.2 | `~/.local/bin/arduino-cli` |
| ESP32 Arduino core | 3.3.11 | `~/.arduino15/packages/esp32` (~7.3 GB) |
| Adafruit GFX | 1.12.6 | `~/Arduino/libraries` |
| Adafruit ST7735/ST7789 | 1.11.0 | `~/Arduino/libraries` |

Board index URL added to `arduino-cli` config:
`https://espressif.github.io/arduino-esp32/package_esp32_index.json`

Add `~/.local/bin` to `PATH` if it isn't already:

```bash
echo 'export PATH="$HOME/.local/bin:$PATH"' >> ~/.bashrc && . ~/.bashrc
```

FQBN: `esp32:esp32:adafruit_feather_esp32s3_reversetft`

## Build options

```
PartitionScheme=tinyuf2_noota,PSRAM=enabled,LoopCore=1,EventsCore=1,CDCOnBoot=cdc,FlashSize=4M
```

`tinyuf2_noota` is load-bearing, not a default. It lays out flash as app at `0x10000`
(2816 KB) with the UF2 bootloader kept in its own `factory` partition at `0x2d0000`.
The more commonly recommended `huge_app` gains 200 KB of app space and **destroys the
UF2 bootloader**, which would take the entire flashing workflow above with it. 2.8 MB
is far more than this firmware needs — the bring-up sketch uses 14%.

If the bootloader does get wiped, it is recoverable: Adafruit's
[factory reset / bootloader repair](https://learn.adafruit.com/adafruit-esp32-s3-tft-feather/factory-reset)
procedure reinstalls it over the ESP32-S3 ROM bootloader, which cannot be erased.

## Flashing

```bash
cd bluecan/firmware
./build.sh bluecan_bringup            # compile + produce .uf2
./build.sh bluecan_bringup --upload   # ...and upload it over serial
./build.sh bluecan_bringup --flash    # ...or copy it to the UF2 bootloader drive
```

`--upload` is the normal path now that both USB identities are bound: it needs no button
presses. Run `./tools/usb-attach.sh` first if `/dev/ttyACM0` is missing.

In practice the first `--upload` after a fresh attach still fails once, because binding
is not attaching: esptool resets the board into download mode, the device leaves as
`239a:8123` and returns as `303a:1001`, and the new identity arrives *unattached*. Then
it happens again in reverse when the flashed sketch boots. `usbipd --auto-attach` covers
this but runs as a foreground watcher, which is awkward to own from a script, so

```bash
./tools/reflash.sh bluecan_bench
```

re-attaches around each transition instead and leaves nothing running afterwards. It is
the command to use for routine flashing; `build.sh --upload` is the layer underneath.

`--flash` is the fallback that needs no passthrough at all. For it, put the board into
bootloader mode first: **double-tap RESET**. A drive labelled `FTHRS3BOOT` appears in
Windows and becomes visible to WSL under `/mnt/<letter>`. The script finds it by looking
for `INFO_UF2.TXT` rather than by drive letter, since the letter varies. Copying the
`.uf2` flashes it and the board reboots into the new firmware by itself.

If the drive doesn't show up in `/mnt`, WSL hasn't picked up the new volume. Simplest
fix is to copy the `.uf2` from Windows Explorer instead — the file is at
`\\wsl.localhost\Ubuntu\home\<user>\projects\motocan\bluecan\firmware\build\<sketch>\<sketch>.uf2`.

## Serial monitor

### The trap: the board prints into the void unless DTR is asserted

Worth reading before debugging a sketch that "does not print". The ESP32-S3 Arduino
core's `USBCDC` gates its output on the host's DTR line. A reader that opens the port
with DTR low sees **nothing at all** — no boot banner, no heartbeat — from a sketch that
is running perfectly. There is no other symptom, so it looks exactly like a crash or a
failed flash, and it will be diagnosed as one.

Asserting DTR is safe on *this* board specifically, and the reason is the same fact that
makes host-side reset impossible here: USB is native to the SoC, so there is no
USB-serial bridge whose DTR/RTS lines are wired to EN. On almost any other ESP32 board
asserting DTR would reboot it. Here it cannot.

[firmware/tools/bench.py](firmware/tools/bench.py) does the right thing and explains why
in its docstring; anything else reading `/dev/ttyACM0` directly needs to do the same.

### Driving the bench sketch

```bash
./tools/bench.py 10                          # stream 10 s
./tools/bench.py 20 -c z --at 4:b --at 10:M  # zero, burst at 4 s, listen-only at 10 s
```

`bluecan_bench` takes single-character commands on the same link that carries its log,
so a bench run is one scriptable command rather than a sequence of button presses on a
board nobody can reach from inside a shell. Lower case turns a thing on, upper case off.
See [bench-link.md](bench-link.md).

### The Windows-interop fallback

```bash
./tools/serial-monitor.sh
```

Auto-detects the board's COM port by USB vendor ID (`239A`). Pass a port explicitly if
needed: `./tools/serial-monitor.sh COM7 115200`.

The script deliberately does not assert DTR/RTS. On the ESP32-S3's native USB CDC those
lines are the reset-into-bootloader trigger, and a monitor that resets the board every
time it attaches is worse than no monitor.

Note the board presents two different USB identities and only one of them is a serial
port:

| Mode | VID:PID | Appears as |
|---|---|---|
| Running a sketch (CDC on boot) | `239A:8123` | COM port |
| UF2 bootloader (double-tap RESET) | `239A:0123` | `FTHRS3BOOT` mass storage |

## CANable v2.0 Pro

The bench counterpart (see [hardware.md](hardware.md)). Once attached through usbipd in
candleLight mode it is a normal Linux CAN interface. `can-utils` is installed, and
[firmware/tools/can-up.sh](firmware/tools/can-up.sh) is the way to bring the interface up
rather than a bare `ip link`: SocketCAN control modes are **sticky**, so an `ip link set
can0 up` that names no mode silently keeps whatever was set last — a loopback flag left
over from an earlier run already invalidated one round of testing here.

```bash
sudo ./tools/can-up.sh              # can0, 500 kbps, normal mode
sudo ./tools/can-up.sh --loopback   # self-test, no bus partner needed
sudo ./tools/can-up.sh --one-shot   # no retransmits: the bench mode against Bluecan
candump -l can0
```

The script also raises `txqueuelen` from SocketCAN's default of 10 to 1000. Ten slots is
fine for a hand-typed `cansend` and far too few for anything that fills the queue faster
than the bus drains it: `cangen -g 0` returns `write: No buffer space available` after
about twenty frames, which reads uncomfortably like a return of the gs_usb stall below
and is nothing of the sort.

The adapter has a second USB identity worth knowing about: `0483:df11`, the STM32 ROM
bootloader. It is reached deliberately with `dfu-util -d 1d50:606f -e` for firmware
updates, and accidentally whenever the 5 V rail dips (see [hardware.md](hardware.md)).
Both identities are bound in usbipd and both are known to `usb-attach.sh`, so recovering
or reflashing needs only an `attach`, never an administrator PowerShell.

`vcan` is available too, so `candump`/`cansend` workflows and any log-parsing tooling
can be developed against a virtual interface before the adapter is even plugged in:

```bash
sudo modprobe vcan && sudo ip link add dev vcan0 type vcan && sudo ip link set vcan0 up
```

## Current state

Working end to end:

- toolchain, board definition, compile
- usbipd passthrough — both USB identities bound, `--auto-attach` running,
  `/dev/ttyACM0` present in WSL
- serial upload from WSL via `./build.sh <sketch> --upload`
- serial capture from WSL (pyserial on `/dev/ttyACM0`, or
  [firmware/tools/serial-monitor.sh](firmware/tools/serial-monitor.sh))
- the bring-up sketch flashed and running: reports `core 1`, so the Arduino task is
  pinned where the CAN work needs it
- the TFT, confirmed visually — title, per-check OK/FAIL lines and the button row all
  render correctly at rotation 3
- the TWAI controller: with A0 and A1 jumpered, the self-test reaches `CAN READY` and
  holds it. Toolchain, flash path, controller, 500 kbps timing and self-reception are
  all proven; any failure past this point is wiring
- the KAmod transceiver: wired to the Feather, and the self-test reaches `CAN READY`
  *through* it, so the whole MCU → transceiver → bus → transceiver → MCU loop carries
  real 500 kbps CAN
- the CANable as a SocketCAN interface: `can0` at 500 kbps, `cangen`/`candump` clean
  after the firmware reflash described above

Two things learned the hard way, both now handled:

- A serial upload leaves the running-sketch USB identity and enters the ROM
  download-mode one mid-operation, so **both** must be bound (see above). With that
  done, uploads run unattended.
- RTS does **not** reset this board from the host. There is no USB-serial bridge to
  pull EN — USB is native to the SoC. So a host-side reset-and-capture doesn't work,
  and the sketch has to make itself observable instead. Hence the once-per-second
  serial heartbeat, and D1 to re-run the checks in place.

- the two boards joined over `CAN H`/`CAN L` and exchanging frames in both directions,
  with mutual ACK and no drops on a saturated bus — see [bench-link.md](bench-link.md)
- BLE, on the Bluecan firmware the board carries now: core 0 runs the NimBLE host and the
  frame-drain task beside the display, and a phone has connected, subscribed and recorded
  a session against it — see [firmware.md](firmware.md#the-phone-session--run-and-it-works).
  This page's setup work stops at the serial link; WSL has no Bluetooth stack at all, so
  everything on the BLE side is measured from the phone or off the board's own counters

Not yet done:

- nothing has been captured from an actual R9; the bench partner only sends what it is
  told to
