#!/usr/bin/env bash
# Keep the board's serial console in a file, across resets, sleeps and wakes.
#
#   ./tools/console-log.sh                  # writes build/console.log, and to the screen
#   ./tools/console-log.sh --busid 1-4      # the board's USB port, if it is asleep now
#   ./tools/console-log.sh other.log        # somewhere else
#
# Then, in any other terminal:  tail -f bluecan/firmware/build/console.log
#
# A board that sleeps disappears from USB and comes back as a new device on every
# wake. usbipd's own auto-attach did not get it back after a wake from light sleep: it
# went on listing the board as attached, and the port gave nothing. So the reader does
# it itself: when the board says it is going to sleep, or the port goes, it detaches
# the board in usbipd, waits for Windows to see it again, attaches it, and reopens the
# port. The port is read from Linux on purpose:
# opening it from Windows reset the board every time on 2026-10-19 (rst:0x15,
# USB_UART_CHIP_RESET), whatever DTR and RTS were set to, while Linux with DTR set
# and RTS clear, as bench.py does, does not.
#
# It holds the port: stop it (ctrl-c) before uploading, or the upload cannot open it.
#
# Each line gets the computer's time, to the millisecond; the board's own lines carry
# none. Lines starting --- are this script's: the port appearing and going.
set -euo pipefail
cd "$(dirname "$0")/.."

USBIPD="/mnt/c/Program Files/usbipd-win/usbipd.exe"
LOG="build/console.log"
BUSID=""
while [[ $# -gt 0 ]]; do
    case "$1" in
        --busid) BUSID="$2"; shift 2 ;;
        -*)      echo "unknown option: $1" >&2; exit 2 ;;
        *)       LOG="$1"; shift ;;
    esac
done
mkdir -p "$(dirname "$LOG")"

# 303a:1001 is the chip's own USB serial port, which every Bluecan profile runs on;
# 239a:8123 the Feather with TinyUSB, as the bench sketches have it.
if [[ -z "$BUSID" ]]; then
    BUSID=$("$USBIPD" list 2>/dev/null | tr -d '\r' |
        awk '/^[0-9]+-[0-9]+/ && /239a:8123|303a:1001/ {print $1; exit}')
fi
if [[ -z "$BUSID" ]]; then
    echo "The board is not on USB, probably asleep. Wake it, or give its port:" >&2
    echo "  $0 --busid <BUSID>     (usb-attach.sh --list shows it while awake)" >&2
    exit 1
fi

echo "--- writing $LOG, board on USB $BUSID (ctrl-c to stop)" >&2

USBIPD="$USBIPD" BUSID="$BUSID" python3 -u - <<'EOF' | tee -a "$LOG"
import glob, os, subprocess, time, datetime, serial

USBIPD = os.environ['USBIPD']
BUSID = os.environ['BUSID']

def say(text):
    print(datetime.datetime.now().strftime('%H:%M:%S.%f')[:-3], text, flush=True)

def usbipd(*args):
    return subprocess.run([USBIPD, *args], capture_output=True, text=True).stdout

def state():
    """The board's line in `usbipd list`: 'Attached', 'Shared', or None while Windows
    does not see it -- asleep."""
    for line in usbipd('list').splitlines():
        if line.startswith(BUSID + ' '):
            return 'Attached' if line.rstrip().endswith('Attached') else 'Shared'
    return None

def attach():
    """Detach whatever usbipd holds, wait for Windows to see the board, attach it,
    and wait for its port."""
    usbipd('detach', '--busid', BUSID)
    while True:
        if state() == 'Shared':
            usbipd('attach', '--wsl', '--busid', BUSID)
            for _ in range(50):
                if glob.glob('/dev/ttyACM*'):
                    return
                time.sleep(0.1)
        time.sleep(1)

if not glob.glob('/dev/ttyACM*'):
    attach()

while True:
    ports = sorted(glob.glob('/dev/ttyACM*'))
    if not ports:
        attach()
        continue
    s = serial.Serial()
    s.port = ports[0]
    s.baudrate = 115200
    s.dtr = True
    s.rts = False
    s.timeout = 0.2
    try:
        s.open()
    except (OSError, serial.SerialException):
        time.sleep(0.1)
        continue
    say('--- %s open' % ports[0])
    buf = b''
    asleep = False
    try:
        while not asleep:
            buf += s.read(4096)
            while b'\n' in buf:
                line, buf = buf.split(b'\n', 1)
                text = line.decode('utf-8', 'replace').rstrip('\r')
                say(text)
                # Standby restarts into the sleep: what is left to read is the
                # bootloader, then nothing until the wake.
                if 'Standby: no frame for' in text:
                    asleep = True
        time.sleep(2)
        while True:
            chunk = s.read(4096)
            if not chunk:
                break
            for line in chunk.split(b'\n'):
                if line.strip():
                    say(line.decode('utf-8', 'replace').rstrip('\r'))
    except (OSError, serial.SerialException):
        pass
    try:
        s.close()
    except Exception:
        pass
    say('--- %s gone' % ports[0])
    attach()
    say('--- %s back' % BUSID)
EOF
