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
(cd /home/user/EKA2L1/build/bin && timeout -k 5 -s KILL "${TMO:-120}" ./eka2l1_qt --device RM-409 --run 0xE0001006 >"$S/g6.log" 2>&1)
# The emulator does not always go on SIGTERM, and a run left behind holds its
# memory and a few per cent of a core. Enough of them and a later launch cannot
# allocate the image -- which is where the G6MEM panics were coming from -- and
# every timing in the session is off. Always match exactly: `pkill -f` would
# also match the shell that started it.
pkill -x eka2l1_qt 2>/dev/null; sleep 1; pkill -9 -x eka2l1_qt 2>/dev/null
FIRSTLOG=$(ls -S "$C"/g6box[0-9].log 2>/dev/null | head -1)
cp -f "$FIRSTLOG" "$S/emu-latest.log" 2>/dev/null
LAUNCHES=$(ls "$C"/g6box[0-9].log 2>/dev/null | wc -l)

REC=$(python3 -c "import os,sys;p=sys.argv[1] if len(sys.argv)>1 and sys.argv[1] else '';print(os.path.getsize(p)//8 if p and os.path.exists(p) else 0)" "$FIRSTLOG")
FAULT=$(grep -oE 'Access violation reading address 0x[0-9A-Fa-f]+' "$S/g6.log" | tail -1 | grep -oE '0x[0-9A-Fa-f]+')
[ -z "$FAULT" ] && FAULT="--"

# Next number in the E series, then the row, in place of the marker.
N=$(grep -oE '^\| E([0-9]+) ' "$R" | grep -oE '[0-9]+' | sort -n | tail -1)
N=$((N + 1))
# Two runs finishing together both read the same last row number and both write
# it, which is where the duplicate E82 and E89 rows came from. One at a time.
exec 9>"$R.lock"; flock 9
N=$(grep -oE '^\| E([0-9]+) ' "$R" | grep -oE '[0-9]+' | sort -n | tail -1); N=$((N + 1))
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
flock -u 9
echo "records: $REC   launches: ${LAUNCHES:-?}   ends: $FAULT   -> logged as E$N in ROUNDS.md (finish its last column)"
echo
python3 "$P/rules.py"
