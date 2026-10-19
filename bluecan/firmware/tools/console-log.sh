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
# wake. usbipd's auto-attach puts it back into WSL each time, and the reader here
# reopens the port whenever it reappears. The port is read from Linux on purpose:
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

# 239a:8123 is the Feather with TinyUSB, 303a:1001 the chip's own USB, as the Lite
# and --like-lite have.
if [[ -z "$BUSID" ]]; then
    BUSID=$("$USBIPD" list 2>/dev/null | tr -d '\r' |
        awk '/^[0-9]+-[0-9]+/ && /239a:8123|303a:1001/ {print $1; exit}')
fi
if [[ -z "$BUSID" ]]; then
    echo "The board is not on USB, probably asleep. Wake it, or give its port:" >&2
    echo "  $0 --busid <BUSID>     (usb-attach.sh --list shows it while awake)" >&2
    exit 1
fi

"$USBIPD" attach --wsl --busid "$BUSID" --auto-attach --unplugged >/dev/null 2>&1 &
ATTACH=$!
trap 'kill $ATTACH 2>/dev/null' EXIT

echo "--- writing $LOG, board on USB $BUSID (ctrl-c to stop)" >&2

python3 -u - <<'EOF' | tee -a "$LOG"
import glob, time, datetime, serial

def say(text):
    print(datetime.datetime.now().strftime('%H:%M:%S.%f')[:-3], text, flush=True)

while True:
    ports = sorted(glob.glob('/dev/ttyACM*'))
    if not ports:
        time.sleep(0.1)
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
    try:
        while True:
            buf += s.read(4096)
            while b'\n' in buf:
                line, buf = buf.split(b'\n', 1)
                say(line.decode('utf-8', 'replace').rstrip('\r'))
    except (OSError, serial.SerialException):
        pass
    try:
        s.close()
    except Exception:
        pass
    say('--- %s gone' % ports[0])
EOF
