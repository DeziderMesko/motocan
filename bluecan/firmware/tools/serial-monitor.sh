#!/usr/bin/env bash
# Stream the Feather's serial output into WSL, without usbipd USB passthrough.
#
# WSL2 has no USB stack of its own, but it does have Windows interop, and .NET's
# System.IO.Ports works fine from PowerShell. So we let Windows own the COM port
# and just pipe it across. See ../../wsl-setup.md.
#
# Usage:  ./serial-monitor.sh [COMn] [baud]
set -euo pipefail

PS="/mnt/c/WINDOWS/System32/WindowsPowerShell/v1.0/powershell.exe"
BAUD="${2:-115200}"
PORT="${1:-}"

# The board enumerates as VID 239A / PID 8123 when running a sketch with USB CDC.
# In UF2 bootloader mode it is PID 0123 and exposes mass storage, not a COM port.
if [[ -z "$PORT" ]]; then
    PORT=$("$PS" -NoProfile -Command \
        "Get-CimInstance Win32_PnPEntity | Where-Object { \$_.PNPDeviceID -match 'VID_239A' -and \$_.Name -match 'COM\d+' } | ForEach-Object { if (\$_.Name -match '(COM\d+)') { \$Matches[1] } } | Select-Object -First 1" \
        2>/dev/null | tr -d '\r' | tr -d '[:space:]')
fi

if [[ -z "$PORT" ]]; then
    echo "No Adafruit (VID 239A) serial device found." >&2
    echo "Is the board plugged in and running a sketch? In UF2 bootloader mode it" >&2
    echo "shows up as the FTHRS3BOOT drive instead of a COM port." >&2
    "$PS" -NoProfile -Command "[System.IO.Ports.SerialPort]::GetPortNames()" 2>/dev/null | tr -d '\r' | sed 's/^/  seen: /' >&2
    exit 1
fi

echo "--- $PORT @ $BAUD (ctrl-c to stop) ---" >&2

# DTR/RTS are left alone on purpose: on the ESP32-S3's native USB CDC, toggling
# them is what triggers the reset-into-bootloader sequence. We want to observe a
# running board, not reset it.
"$PS" -NoProfile -Command "
\$p = New-Object System.IO.Ports.SerialPort '$PORT', $BAUD, 'None', 8, 'One'
\$p.DtrEnable = \$false
\$p.RtsEnable = \$false
\$p.ReadTimeout = 500
\$p.Open()
try {
    while (\$true) {
        try { \$line = \$p.ReadLine(); Write-Output \$line }
        catch [TimeoutException] { }
    }
} finally { \$p.Close() }
" 2>&1 | tr -d '\r'
