#!/usr/bin/env bash
# Flash a Treadmill Buddy with release firmware from GitHub.
#
#   scripts/flash-latest.sh                   # latest release, 2.0"/2.8" Display Pack
#   scripts/flash-latest.sh --pack 114        # latest release, 1.14" Display Pack
#   scripts/flash-latest.sh v0.3.0            # a specific release
#   scripts/flash-latest.sh --uf2 FILE.uf2    # a local build instead, e.g. .pio/build/pico_28/firmware.uf2
#
# Downloading a release needs the GitHub CLI (`gh`).
#
# Getting the Pico into its bootloader:
#   * Already running Treadmill Buddy: the script reboots it into the
#     bootloader over USB serial. Nothing to press.
#   * Otherwise (new board, CircuitPython, or it doesn't respond): unplug it,
#     hold BOOTSEL while plugging it back in, then let go. The script waits.
#
# It flashes with picotool when it can find one (on PATH, or the copy
# PlatformIO installs), else by copying the UF2 to the RPI-RP2 drive. Every
# USB step has a time limit, so a stuck device can't hang the script.
#
# TREADMILL_DRIVE overrides where the bootloader drive mounts (default
# /Volumes/RPI-RP2 on macOS, /media/$USER/RPI-RP2 on Linux).
set -euo pipefail

REPO="MaverickUK/TreadmillBuddy_Native"
PACK=28
TAG=""
LOCAL_UF2=""
WAIT_FOR_BOOTLOADER_S=120
WAIT_FOR_BOOT_S=20

while [[ $# -gt 0 ]]; do
    case "$1" in
        --pack) PACK="${2:-}"; shift 2 ;;
        --uf2) LOCAL_UF2="${2:-}"; shift 2 ;;
        -h|--help) sed -n '2,23p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
        v*) TAG="$1"; shift ;;
        *) echo "Unknown argument: $1 (see --help)" >&2; exit 1 ;;
    esac
done
[[ "$PACK" == 28 || "$PACK" == 114 ]] || { echo "--pack must be 28 or 114" >&2; exit 1; }

if [[ -n "${TREADMILL_DRIVE:-}" ]]; then
    DRIVE="$TREADMILL_DRIVE"
elif [[ "$(uname)" == "Darwin" ]]; then
    DRIVE="/Volumes/RPI-RP2"
else
    DRIVE="/media/${USER}/RPI-RP2"
fi

# Run a command, killing it after $1 seconds (macOS has no `timeout`).
limit() {
    local secs="$1"; shift
    perl -e 'alarm shift; exec @ARGV' "$secs" "$@"
}

find_picotool() {
    if command -v picotool >/dev/null; then
        command -v picotool
        return
    fi
    ls "$HOME"/.platformio/packages/tool-picotool-rp2040-earlephilhower/picotool 2>/dev/null | head -1
}
PICOTOOL="$(find_picotool || true)"

in_bootloader() {
    [[ -d "$DRIVE" ]] && return 0
    [[ -n "$PICOTOOL" ]] && limit 5 "$PICOTOOL" info >/dev/null 2>&1
}

# The board's serial port while Treadmill Buddy runs (if there's exactly one candidate).
serial_port() {
    local ports
    if [[ "$(uname)" == "Darwin" ]]; then
        ports=$(ls /dev/cu.usbmodem* 2>/dev/null || true)
    else
        ports=$(ls /dev/ttyACM* 2>/dev/null || true)
    fi
    [[ $(echo "$ports" | grep -c .) -eq 1 ]] && echo "$ports"
}

running_as_treadmill_buddy() {
    if [[ "$(uname)" == "Darwin" ]]; then
        limit 10 system_profiler SPUSBDataType 2>/dev/null | grep -q "TREADMILLBUDDY"
    else
        grep -qs "TREADMILLBUDDY" /sys/bus/usb/devices/*/product
    fi
}

# 1. The firmware.
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT
if [[ -n "$LOCAL_UF2" ]]; then
    [[ -f "$LOCAL_UF2" ]] || { echo "No such file: $LOCAL_UF2" >&2; exit 1; }
    UF2="$LOCAL_UF2"
    echo "Flashing local $UF2."
else
    command -v gh >/dev/null || { echo "Needs the GitHub CLI: https://cli.github.com (brew install gh)" >&2; exit 1; }
    echo "Downloading ${TAG:-the latest release} (${PACK} Display Pack) from $REPO..."
    gh release download ${TAG:+"$TAG"} --repo "$REPO" --pattern "treadmillbuddy-display${PACK}-*.uf2" --dir "$WORK"
    UF2="$(ls "$WORK"/treadmillbuddy-display"${PACK}"-*.uf2 | head -1)"
    [[ -f "$UF2" ]] || { echo "No display${PACK} firmware UF2 in that release." >&2; exit 1; }
    echo "Got $(basename "$UF2")."
fi

# 2. Get the board into its bootloader.
if ! in_bootloader; then
    port="$(serial_port || true)"
    if [[ -n "$port" ]] && running_as_treadmill_buddy; then
        # Opening the port at 1200 baud asks the firmware to reboot into the bootloader.
        echo "Rebooting the board into its bootloader ($port)..."
        if [[ "$(uname)" == "Darwin" ]]; then limit 5 stty -f "$port" 1200 || true
        else limit 5 stty -F "$port" 1200 || true; fi
        for ((i = 0; i < 10; i++)); do
            in_bootloader && break
            sleep 1
        done
    fi
fi
if ! in_bootloader; then
    echo
    echo "Waiting for the Pico's bootloader."
    echo "Unplug the Pico, hold BOOTSEL while plugging it back in, then let go."
    for ((i = 0; i < WAIT_FOR_BOOTLOADER_S; i++)); do
        in_bootloader && break
        sleep 1
    done
    in_bootloader || { echo "No bootloader after ${WAIT_FOR_BOOTLOADER_S} s; nothing flashed." >&2; exit 1; }
fi

# 3. Flash it.
echo "Flashing..."
if [[ -n "$PICOTOOL" ]]; then
    # -x: run the new firmware afterwards
    limit 90 "$PICOTOOL" load -v -x "$UF2" | tr '\r' '\n' | grep -E "OK|rror|rebooted" || {
        echo "picotool failed or timed out." >&2
        exit 1
    }
else
    # The board flashes itself and reboots once the copy completes, so the
    # drive disappears (macOS may say it wasn't ejected properly; that's fine).
    cp "$UF2" "$DRIVE/" 2>/dev/null || true
    sync
fi

# 4. Check it came back as Treadmill Buddy.
for ((i = 0; i < WAIT_FOR_BOOT_S; i++)); do
    if running_as_treadmill_buddy; then
        echo "Done: the board is running $(basename "$UF2" .uf2)."
        exit 0
    fi
    sleep 1
done
echo "Flashed, but the board hasn't reappeared as TREADMILLBUDDY within ${WAIT_FOR_BOOT_S} s." >&2
exit 1
