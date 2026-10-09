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
# Which game to build and launch. `games/<name>/` holds its generated shim and
# import indices and its hand-written game.h; the app UID comes out of that
# file so the two cannot disagree about which application to run.
GAME="${GAME:-asphalt2}"
UID3=$(sed -n 's/.*GAME_APP_UID3[[:space:]]*\(0x[0-9A-Fa-f]*\).*/\1/p' "$P/games/$GAME/game.h")
[ -n "$UID3" ] || { echo "no GAME_APP_UID3 in games/$GAME/game.h"; exit 1; }
# The log, box and dump are named after the title's stem now, so two games on
# one machine do not truncate each other's records. Same source as the paths
# the loader builds: GAME_STEM_CHARS.
STEM=$(sed -n "s/.*GAME_STEM_CHARS[[:space:]]*//p" "$P/games/$GAME/game.h" | tr -cd "0-9A-Za-z_")
[ -n "$STEM" ] || { echo "no GAME_STEM_CHARS in games/$GAME/game.h"; exit 1; }

export LIBGL_ALWAYS_SOFTWARE=1 GALLIUM_DRIVER=llvmpipe QT_QPA_PLATFORM=xcb DISPLAY=:99
pgrep -x Xvfb >/dev/null || { nohup Xvfb :99 -screen 0 1280x900x24 >"$S/xvfb.log" 2>&1 & sleep 3; }
# Round 158: built as the release builds it -- with the icon, whose MIF the
# caption resource names. Without it the S60 3.0 app list hands aknicon a
# bare drive root and the launch leaves -28 (E870); a package always has it.
(cd "$P" && python3 -c "
import os, sys, build_release as r, build_gate6, mkmbm, picture
out, game = sys.argv[1], sys.argv[2]
os.makedirs(out, exist_ok=True)
st = r.stem(game); tree = os.path.join(r.GAMES_ROOT, picture.folder(game))
icon = os.path.join(out, 'gate6.mbm')
mkmbm.build(os.path.join(tree, st + '.aif'), icon, list(r.ICON_BITMAPS))
build_gate6.build(out, caption=picture.setting(game, 'GAME_CAPTION'), icon=icon,
                  vendor=picture.setting(game, 'GAME_VENDOR'), game=game)
" "$S/out" "$GAME") || exit 1
# Under the name that title installs as -- two games cannot both be gate6.exe,
# on a phone or here. See GAME_APP_NAME.
APPNAME=$(sed -n 's/.*GAME_APP_NAME[[:space:]]*"\([^"]*\)".*/\1/p' "$P/games/$GAME/game.h")
APPNAME=${APPNAME:-gate6}
cp "$S/out/$APPNAME.exe" $D/sys/bin/$APPNAME.exe
cp "$S/out/$APPNAME.rsc" $D/resource/apps/$APPNAME.rsc
cp "$S/out/${APPNAME}_reg.rsc" $D/private/10003a3f/import/apps/${APPNAME}_reg.rsc
# Round 158: the icon too, as a package installs it. Without it the S60 3.0
# app list names the icon file as the bare drive root, aknicon leaves -28, and
# the launch dies in the cleanup (E870); 3.2 never asked for it.
[ -f "$S/out/$APPNAME.mif" ] && cp "$S/out/$APPNAME.mif" $D/resource/apps/$APPNAME.mif
# KEEPOLD=1 leaves whatever is on the drive alone, which is how the sweep of
# the old rotated log names is tested: the port has to delete them itself.
[ -n "$KEEPOLD" ] || rm -f $C/g6box-$STEM*.log $D/g6box-$STEM*.dat $C/g6box-$STEM*.dat
# Round 161: every run reports the handles EKA2L1 lets through and a phone
# answers with KERN-EXEC 0 (EKA2L1_STRICTHANDLE=1, log only). Colin's build
# 002 passed thirty bench runs and died at launch on the N95 on exactly that.
# EKA2L1_STRICTHANDLE=2 in the caller's environment panics as a device does.
export EKA2L1_STRICTHANDLE="${EKA2L1_STRICTHANDLE:-1}"
# Round 162: and requests complete as EKA2's kernel completes them, the status
# word only (EKA2L1_KERNREQ=1, src/emu/utils/include/utils/reqsts.h). The
# emulator's own completion also cleared ERequestPending in the word after,
# which hid an EKA1 one-word TRequestStatus spilling into the next field: Colin
# build 003 took no keys on the N95 and passed every lenient bench run.
# EKA2L1_KERNREQ=0 in the caller's environment brings the old behaviour back.
export EKA2L1_KERNREQ="${EKA2L1_KERNREQ:-1}"
(cd /home/user/EKA2L1/build/bin && timeout -k 5 -s KILL "${TMO:-120}" ./eka2l1_qt --device "${DEVICE:-RM-409}" --run $UID3 >"$S/g6.log" 2>&1)
# The emulator does not always go on SIGTERM, and a run left behind holds its
# memory and a few per cent of a core. Enough of them and a later launch cannot
# allocate the image -- which is where the G6MEM panics were coming from -- and
# every timing in the session is off. Always match exactly: `pkill -f` would
# also match the shell that started it.
pkill -x eka2l1_qt 2>/dev/null; sleep 1; pkill -9 -x eka2l1_qt 2>/dev/null
# One log since build 178, and the old rotated names while any linger.
FIRSTLOG=$(ls -S "$C"/g6box-$STEM.log "$C"/g6box[0-9].log 2>/dev/null | head -1)
cp -f "$FIRSTLOG" "$S/emu-latest.log" 2>/dev/null
LAUNCHES=$(ls "$C"/g6box-$STEM.log "$C"/g6box[0-9].log 2>/dev/null | wc -l)

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
BADH=$(grep -a -c "BAD HANDLE" "$S/g6.log" 2>/dev/null)
[ "${BADH:-0}" -gt 0 ] && echo "** bad handles: $BADH -- a phone panics KERN-EXEC 0 at the first (grep 'BAD HANDLE' $S/g6.log) **"
echo
python3 "$P/rules.py"
