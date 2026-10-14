# Solved: the gs_usb 3-frame stall is an adapter firmware bug, not usbip

> Moved here from `bluecan/gs-usb-stall.md` on 2026-10-14, unchanged but for its links.

A standalone write-up, readable without the rest of this repository. Originally an open
problem report ("gs_usb stalls after 3 frames over usbip/WSL2"); the root cause was
localized to the adapter's firmware, the firmware was replaced, and the acceptance test
now passes. The full diagnosis is below.

## Resolution (2026-08-21)

Flashed **ElmüSoft Candlelight 2.5** (`STM32G431 - Candlelight2.5 - Multiboard.dfu`
from [Elmue/CANable-2.5-firmware-Slcan-and-Candlelight](https://github.com/Elmue/CANable-2.5-firmware-Slcan-and-Candlelight)),
replacing the shipped `candlelight ba6b1dd (2021-07-16)` build. Flashing was done
entirely over usbip: `dfu-util -d 1d50:606f -e` to enter the ROM bootloader, `usbipd
bind/attach` of the `0483:df11` device, `dfu-util -w -c 1 -i 0 -a 0 -D <file>.dfu`,
then a physical replug to leave DFU mode (first boot takes tens of seconds while
Windows installs drivers for the new composite device; a re-`bind` is needed because
the device identity changed).

Acceptance test result: `cangen -n 200 -g 10` and `cangen -n 500 -g 2` both complete
with every frame delivered on a single channel open, zero drops, zero ENOBUFS. Note
that this firmware reports device loopback properly: candump sees each frame **twice**
(TX echo + looped-back RX), so counters read 2× the cangen count.

### Decided against: disabling the BOOT0 pin (2026-08-21)

MKS boards share PB8 between CAN-RX and nBOOT0, and their power-on delay circuit is too
long, so a brief 5V dip drops the adapter into the ROM bootloader — merely wiggling the
USB plug is enough. The fix would be setting `nBOOT_SEL` in option bytes (Firmware
Programmer button in HUD ECU Hacker, or STM32CubeProgrammer). **Not doing it**, for two
reasons:

- The failure is not a brick. The firmware stays intact in flash; the adapter just
  enumerates as `0483:df11` instead of `1d50:606f`, so `can0` never appears. A clean
  power cycle fixes it.
- The threat model was vibration on a moving bike silently killing a log mid-ride. This
  adapter is a **bench analysis tool only** — the on-bike logger is the Feather. So the
  cost of the failure is one replug, occasionally.

Against that, the edit carries a real risk: `RDP` lives in the same option-byte editor,
and Level 2 is irreversible — it permanently disables SWD and the system bootloader.
Risk without a matching payoff. If this is ever revisited, the board does have SWD pads
and an RP2040 running `debugprobe` (CMSIS-DAP + OpenOCD) is enough to recover, but test
that path *before* disabling the pin, not after.

Note for future firmware updates: the pin is irrelevant to the normal flow, since
`dfu-util -e` is a software DFU detach that never involves BOOT0. The `0483:df11` DFU
device is already in usbipd's persisted (bound) list, so the next update needs only
`attach`, no administrator PowerShell.

The rest of this document is the diagnosis that led here, kept for reference.

## Summary

A CANable 2.0 Pro running candleLight-family firmware is forwarded into WSL2 with
usbipd-win. It enumerates correctly, `gs_usb` binds, and `can0` comes up at 500 kbps.
**Exactly three frames then complete per channel open, and nothing completes
afterwards.** The count is three whether frames are sent one per process or all from one
socket, in loopback mode or normal mode, with any inter-frame gap from 0 to 1000 ms.

**Root cause: the adapter's firmware stops servicing its bulk IN endpoint.** After
exactly three deliveries in which the endpoint had to wait for data (NAK, then data
arrives) per channel start, the firmware stops handing queued frames to the USB
peripheral. The frames stay in its internal FIFO (~10 deep — hence the LEDs signalling
buffer overflow) until a channel stop command flushes them through a different code
path. The entire host stack — Windows xHCI, VBoxUSB.sys, usbipd-win, vhci-hcd, gs_usb —
was polling correctly the whole time.

The earlier conclusion in this document ("the adapter is not at fault and neither is
the driver") was wrong, and the evidence that seemed to support it — late echoes after
teardown — is in fact the firmware bug's signature: teardown sends a channel-stop,
which is exactly what un-wedges the device and releases the buffered frames.

## Environment

| | |
|---|---|
| Host | Windows, WSL2, `networkingMode=mirrored` set explicitly in `.wslconfig` |
| Kernel | `6.18.33.2-microsoft-standard-WSL2` |
| Distro | Ubuntu 24.04.4 LTS |
| usbipd-win | `5.3.0-54`, a `master` branch build (`aa3db8b`), not a tagged release |
| usbip client | usbip-utils 2.0, from `linux-tools` built for `6.8.0-138-generic` |
| iproute2 | 6.1.0 |
| can-utils | 2023.03-1 |

## Hardware

MKS CANable 2.0 Pro — an isolated USB-CAN adapter, STM32G431C8T6, CAN-FD capable.
Firmware as shipped, at the time of this diagnosis — since replaced, see
[Resolution](#resolution-2026-08-21) above. It identified as:

- USB: `1d50:606f`, manufacturer `canable.io`, product `canable2 gs_usb`, bcdDevice 0.00
- gs_usb `DEVICE_CONFIG`: `sw_version=2`, `hw_version=1`

That is an old build of the canable2/budgetcan gs_usb firmware as shipped by MKS on
their clones, not current upstream.

Nothing is connected to CAN-H/CAN-L; all testing was loopback mode or normal mode into
an unterminated bus. On-board 120 Ω terminator fitted.

## The diagnosis, step by step

Each step below reproduced the stall and eliminated one layer of the stack.

**1. usbmon on the Linux side.** A capture during the stall shows `gs_usb` behaving
correctly: 30 bulk IN URBs pending at all times, TX URBs on the OUT endpoint all
completing with status 0, and IN completions simply ceasing after 3 — no error
statuses, no unlinks, nothing silently retired. vhci and gs_usb are doing their jobs.

**2. tcpdump of the usbip TCP stream** (loopback, port 3240; in mirrored networking
mode it rides `loopback0`, not `lo`). The Linux client sent `CMD_SUBMIT` for every IN
URB, including resubmissions. The Windows server returned `RET_SUBMIT` for the OUT
transfers throughout, but stopped returning IN completions after the third fresh frame.
**The loss is on the Windows side of the TCP link; vhci is exonerated.**

**3. usbipd-win source review** (the exact `master` commit in use, `aa3db8b`). The
server turns each IN submit into an overlapped `SUPUSB_IOCTL_SEND_URB` ioctl to
VBoxUSB.sys, in seqnum order. Replies per endpoint are serialized through a FIFO, so a
single never-completing ioctl would silently block all later replies — a plausible
suspect, eliminated in step 5.

**4. Direct libusb reproduction, bypassing gs_usb entirely** (python3-libusb1 against
the raw device; same control transfers the kernel driver makes: bittiming, then mode
start with loopback+hw-timestamp). This reproduced the stall identically and allowed
controlled experiments:

| Experiment | Result |
|---|---|
| 30 concurrently pending IN reads | stall after 3 fresh frames |
| **1** pending IN read, resubmitted after each completion | stall after 3 |
| 32- and 64-byte read buffers (multiples of wMaxPacketSize) | stall after 3 |
| Timestamps disabled (20-byte frames instead of 24) | stall after 3 |
| Inter-frame gap 0 / 5 / 50 / 1000 ms | stall after 3 |
| Cancel the pending IN before every OUT, resubmit after | stall after 3 |
| **Reads submitted only after all frames sent** (data already buffered in the device) | **all ~10 FIFO'd frames complete, in one burst, no stall** |

The asymmetry in the last row is the fingerprint: deliveries where data is ready
complete without limit; deliveries where the endpoint had to wait die after the third.

**5. The decisive experiment.** Wedge the pipe (3 of 8 frames delivered), then — with
the starving host-side read left completely untouched — send a bare channel-stop
(`GS_USB_BREQ_MODE`, mode 0) over the control endpoint:

```
phase1: fresh=3 of 8            (wedged, 5 frames stuck in the device)
phase2: after mode STOP alone:  5 completions, instantly
phase3: after mode START again: 3 more frames, then wedged again
```

The pending read completed the moment the device decided to hand over data. Therefore
the host was issuing IN tokens all along and the device was NAKing with a full FIFO.
No host-side component can be responsible; repeated endpoint aborts (which usbipd-win
issues on unlink) had already been observed *not* to un-wedge it. Only the device's own
channel-stop path flushes the queue. It is a firmware bug. (A data-toggle desync is
also excluded: a receiver ACKs a wrong-toggle packet, so a toggle mismatch cannot
permanently wedge a pipe.)

Every element of the original mystery now has an explanation:

- **"Exactly three"** — a deterministic limit in the firmware's IN-endpoint arming
  logic, plausibly its double-buffered endpoint state machine (2 PMA buffers + 1 armed
  = 3 in flight before a desync deadlock).
- **Late echoes after teardown** — `ip link down` sends channel-stop, which flushes
  the FIFO; the frames land in fresh URBs at the next open and gs_usb logs
  `Unexpected unused echo id 0..9`.
- **Error counters frozen at zero** — state reports ride the same wedged IN endpoint.
- **Both LEDs solid** — genuine device-side buffer overflow, which is exactly what it
  looked like.
- **ENOBUFS after ten frames** — all ten `GS_MAX_TX_URBS` echo contexts occupied,
  echoes never delivered.

## What was ruled out along the way

| Hypothesis | How it was killed |
|---|---|
| Test method artifacts | Same stall with cansend-per-frame, cangen, and raw libusb. |
| Broken loopback mode | Normal mode stalls identically. |
| vhci-hcd / WSL kernel | TCP capture shows the completions never left Windows. |
| usbipd-win reply pipeline | Stall reproduces with a single outstanding IN read; nothing to head-of-line block. |
| VBoxUSB.sys queue limits | Source review: each URB is an independent IRP passed to USBD; stall count unaffected by pipeline depth (1 vs 30). |
| Windows xHCI stops polling | Decisive experiment above: a device-only control command released the data into an untouched pending read. |
| Concurrent CDC-ACM device on the usbip link | Detached, no change (and its own stream ran 1400 s clean). |
| `networkingMode=mirrored` | Never needed testing — the TCP capture already placed the fault above the network layer. |

## Fix

**Flash current firmware.** The shipped build (`sw_version=2`, an old MKS build of the
canable2/budgetcan gs_usb firmware) is superseded upstream, where the USB stack and the
device→host queue handling have both been reworked since.

Options, in order of convenience:

1. **canable.io web updater** — <https://canable.io/updater/> in Chrome/Edge on
   Windows, flashes over WebDFU. Choose the CANable 2.0 candleLight/gs_usb build.
   The device must be in DFU mode (BOOT button, or DFU detach) and, if it is still
   bound to usbipd, `usbipd unbind` first so Windows sees it.
2. **dfu-util** — the running firmware exposes a DFU runtime interface (interface 1 of
   the composite device), so `dfu-util -d 1d50:606f -e` detaches into the STM32 ROM
   bootloader (`0483:df11`), then flash a `.bin`/`.dfu` of the current firmware.
3. The STM32G431 ROM bootloader cannot be bricked by a bad application image — worst
   case, hold BOOT and reflash.

**What was done:** option 2, `dfu-util` over usbip — but with ElmüSoft Candlelight 2.5
rather than an upstream candleLight build, because upstream `candleLight_fw` does not
support the STM32G431 this board uses. The command sequence and the post-flash acceptance
result are in [Resolution](#resolution-2026-08-21) at the top.

Neither of the two leads below was needed in the end. They are kept as fallbacks:

- slcan firmware over CDC-ACM (`slcand`), the transport already proven reliable on this
  machine — at the cost of the native netdev and throughput.
- a native Windows sanity test (python-can + gs_usb/candle backend after
  `usbipd unbind`), before and after reflashing, to show whether the old firmware also
  failed natively and to confirm the fix independently of usbip.

## Acceptance test

Fixed means `cangen can0 -n 200 -g 10` completes with the interface TX counter advancing
by 200 and `candump` seeing 200 frames, on a single channel open, with no `ENOBUFS`.

## Tooling in this repository

- [firmware/tools/usb-attach.sh](../firmware/tools/usb-attach.sh) — attach project devices
  from WSL, no admin needed
- [firmware/tools/can-up.sh](../firmware/tools/can-up.sh) — bring `can0` up; note that
  SocketCAN control modes are **sticky**, so this script always spells out the `off`
  states. A `--normal` that only omits `loopback on` silently leaves loopback in force,
  which invalidated one round of testing here. `--one-shot` is a flag rather than a mode —
  it combines with the others — but is spelled out the same way for the same reason.
- [firmware/tools/can-loopback-test.sh](../firmware/tools/can-loopback-test.sh) — the
  reproduction above, as one command

## Diagnostic techniques worth remembering

- `usbmon` works fine inside WSL2 (`modprobe usbmon`, `/sys/kernel/debug/usb/usbmon/1u`).
- In mirrored networking mode, WSL↔Windows loopback TCP (like usbip's port 3240) is
  captured on interface `loopback0`, not `lo`.
- The usbip wire protocol is trivially parseable: 48-byte headers, `CMD_SUBMIT`=1,
  `RET_SUBMIT`=3, seqnums map completions to submissions.
- usbipd-win can log at trace level and write a Windows-side PcapNg of all URB traffic:
  `usbipd server Logging:LogLevel:Default=Trace 'usbipd:PcapNg:Path=C:\...\trace.pcapng'`
  (stop the service first; admin console). Not needed in the end, but the right next
  step if Windows-side visibility is ever required.
- python3-libusb1 against the raw endpoints, with the kernel driver auto-detached, is
  the fastest way to isolate device behavior from driver behavior — gs_usb's control
  protocol is small enough to drive by hand.
