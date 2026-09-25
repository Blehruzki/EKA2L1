#!/bin/bash
# emurun.sh -- build gate6, run it once in the emulator, and write the run down.
#
# The last column of every row starts as TODO and checkrec.py refuses a commit
# while one is left, so a run cannot quietly go unrecorded. That is the whole
# point of this script: the logging is not a thing to remember.
#
#   emurun.sh "the one change"      # the change goes in the row as given
#
P="$(cd "$(dirname "$0")" && pwd)"
R="$P/../../ROUNDS.md"
S="${EMUSCRATCH:-/tmp/claude-0/-home-user-EKA2L1/ec0d1fd1-56fb-5d4c-9116-ef9f3a4e0d5a/scratchpad}"
D=/root/.local/share/EKA2L1/data/drives/e
C=/root/.local/share/EKA2L1/data/drives/c
CHANGE="${1:-(not stated)}"

export LIBGL_ALWAYS_SOFTWARE=1 GALLIUM_DRIVER=llvmpipe QT_QPA_PLATFORM=xcb DISPLAY=:99
pgrep -x Xvfb >/dev/null || { nohup Xvfb :99 -screen 0 1280x900x24 >"$S/xvfb.log" 2>&1 & sleep 3; }
(cd "$P" && python3 build_gate6.py "$S/out") || exit 1
cp "$S/out/gate6.exe" $D/sys/bin/gate6.exe
cp "$S/out/gate6.rsc" $D/resource/apps/gate6.rsc
cp "$S/out/gate6_reg.rsc" $D/private/10003a3f/import/apps/gate6_reg.rsc
rm -f $C/g6box*.log $D/g6box*.dat $C/g6box*.dat
(cd /home/user/EKA2L1/build/bin && timeout "${TMO:-45}" ./eka2l1_qt --device RM-409 --run 0xE0001006 >"$S/g6.log" 2>&1)
pkill -x eka2l1_qt 2>/dev/null
cp -f "$C"/g6box1.log "$S/emu-latest.log" 2>/dev/null
LAUNCHES=$(ls "$C"/g6box[0-9].log 2>/dev/null | wc -l)

REC=$(python3 -c "import os;p='$C/g6box1.log';print(os.path.getsize(p)//8 if os.path.exists(p) else 0)")
FAULT=$(grep -oE 'Access violation reading address 0x[0-9A-Fa-f]+' "$S/g6.log" | tail -1 | grep -oE '0x[0-9A-Fa-f]+')
[ -z "$FAULT" ] && FAULT="--"

# Next number in the E series, then the row, in place of the marker.
N=$(grep -oE '^\| E([0-9]+) ' "$R" | grep -oE '[0-9]+' | sort -n | tail -1)
N=$((N + 1))
python3 - "$R" "E$N" "$CHANGE" "$REC" "$FAULT" <<'PY'
import sys
path, num, change, rec, fault = sys.argv[1:6]
s = open(path).read()
marker = '<!-- EMURUN -->'
row = '| %s | %s | %s | `%s` | TODO |\n' % (num, change, rec, fault)
i = s.index(marker)
j = s.rindex('\n', 0, i)          # the blank line before the marker
j = s.rindex('\n', 0, j) + 1      # end of the last table row
open(path, 'w').write(s[:j] + row + s[j:])
PY
echo "records: $REC   launches: ${LAUNCHES:-?}   ends: $FAULT   -> logged as E$N in ROUNDS.md (finish its last column)"
