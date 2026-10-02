#!/bin/bash
# holdtest.sh -- press and hold the mode key at a real emulator, and see what
# the port does with it.
#
# The picker cannot be tested by running the game and reading a log, because
# the whole thing is about what a held key does over time. So: launch, let the
# game settle, hold host Backspace (which EKA2L1 binds to
# `std_key_backspace`, the same 0x01 the port watches for) for a couple of
# seconds, let go, and read the modes out of the log afterwards.
set -u
P="$(cd "$(dirname "$0")" && pwd)"
S="${EMUSCRATCH:-/tmp/claude-0/-home-user-EKA2L1/ec0d1fd1-56fb-5d4c-9116-ef9f3a4e0d5a/scratchpad}"
C=/root/.local/share/EKA2L1/data/drives/c
HOLD="${HOLD:-2.2}"
SETTLE="${SETTLE:-35}"

export LIBGL_ALWAYS_SOFTWARE=1 GALLIUM_DRIVER=llvmpipe QT_QPA_PLATFORM=xcb DISPLAY=:99
pgrep -x Xvfb >/dev/null || { nohup Xvfb :99 -screen 0 1280x900x24 >"$S/xvfb.log" 2>&1 & sleep 3; }
pgrep -x eka2l1_qt >/dev/null && { echo "another emulator is running -- refusing"; exit 1; }

rm -f "$C"/g6box*.log "$C"/gate6.cfg
(cd /home/user/EKA2L1/build/bin && timeout -k 5 -s KILL 90 \
    ./eka2l1_qt --device RM-409 --run 0xE0001006 >"$S/hold.log" 2>&1) &
EMU=$!

sleep "$SETTLE"
WIN=$(xdotool search --onlyvisible --name EKA2L1 2>/dev/null | head -1)
if [ -z "$WIN" ]; then echo "no emulator window found"; else
    xdotool windowactivate --sync "$WIN" 2>/dev/null
    echo "holding backspace for ${HOLD}s at window $WIN"
    xdotool keydown --window "$WIN" BackSpace
    sleep "$HOLD"
    xdotool keyup --window "$WIN" BackSpace
fi
sleep 6
pkill -x eka2l1_qt 2>/dev/null; sleep 1; pkill -9 -x eka2l1_qt 2>/dev/null
wait $EMU 2>/dev/null

LOG=$(ls -S "$C"/g6box[0-9].log 2>/dev/null | head -1)
cp -f "$LOG" "$S/hold-latest.log" 2>/dev/null
echo "log: $LOG"
ls -la "$C"/gate6.cfg 2>/dev/null || echo "gate6.cfg: NOT WRITTEN"
