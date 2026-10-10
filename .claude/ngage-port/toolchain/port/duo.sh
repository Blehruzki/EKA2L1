#!/bin/bash
# duo.sh -- two emulator instances on one machine, Bluetooth over IP between
# them: A hosts, B joins. Builds and installs once into both data trees, runs
# a key script for each, and writes the run down as emurun.sh does.
#
#   duo.sh "the one change"
#
# A is the usual ~/.local/share/EKA2L1; B is a copy of it under
# $S/home2 (make it once with cp -a). Each gets direct-IP discovery
# (btnet-discovery-mode 1), its own midman port and port offset, and the
# other as its one friend; A's own config is put back afterwards, so a later
# single run is unchanged. DUO_A and DUO_B name the key scripts (default
# duo_host.sh and duo_join.sh beside this file); each gets the window id
# and the screenshot prefix.
P="$(cd "$(dirname "$0")" && pwd)"
R="$P/../../ROUNDS.md"
S="${EMUSCRATCH:-/tmp/claude-0/-home-user-EKA2L1/ec0d1fd1-56fb-5d4c-9116-ef9f3a4e0d5a/scratchpad}"
CHANGE="${1:-(not stated)}"
GAME="${GAME:-colin}"
HA=/root/.local/share/EKA2L1
HB="$S/home2/.local/share/EKA2L1"
[ -d "$HB/data/drives" ] || { echo "no second tree at $HB (cp -a $HA $S/home2/.local/share/)"; exit 1; }
UID3=$(sed -n 's/.*GAME_APP_UID3[[:space:]]*\(0x[0-9A-Fa-f]*\).*/\1/p' "$P/games/$GAME/game.h")
STEM=$(sed -n "s/.*GAME_STEM_CHARS[[:space:]]*//p" "$P/games/$GAME/game.h" | tr -cd "0-9A-Za-z_")
APPNAME=$(sed -n 's/.*GAME_APP_NAME[[:space:]]*"\([^"]*\)".*/\1/p' "$P/games/$GAME/game.h"); APPNAME=${APPNAME:-gate6}

export LIBGL_ALWAYS_SOFTWARE=1 GALLIUM_DRIVER=llvmpipe QT_QPA_PLATFORM=xcb DISPLAY=:99
# B gets a display of its own: on one display the windows overlap (a covered
# window captures black, no compositing) and the two key scripts fight over
# the focus (E1118).
pgrep -f "Xvfb :99" >/dev/null || { nohup Xvfb :99 -screen 0 1280x900x24 >"$S/xvfb.log" 2>&1 & sleep 3; }
pgrep -f "Xvfb :98" >/dev/null || { nohup Xvfb :98 -screen 0 1280x900x24 >"$S/xvfb98.log" 2>&1 & sleep 3; }
pgrep -x eka2l1_qt >/dev/null && { echo "an emulator is already running"; exit 1; }

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

install() {   # install <tree>
    local D="$1/data/drives/e" C="$1/data/drives/c"
    cp "$S/out/$APPNAME.exe" "$D/sys/bin/$APPNAME.exe"
    cp "$S/out/$APPNAME.rsc" "$D/resource/apps/$APPNAME.rsc"
    cp "$S/out/${APPNAME}_reg.rsc" "$D/private/10003a3f/import/apps/${APPNAME}_reg.rsc"
    [ -f "$S/out/$APPNAME.mif" ] && cp "$S/out/$APPNAME.mif" "$D/resource/apps/$APPNAME.mif"
    rm -f "$C"/g6box-$STEM*.log "$D"/g6box-$STEM*.dat "$C"/g6box-$STEM*.dat
}
netconf() {   # netconf <config.yml> <own port> <offset> <friend port>
    python3 - "$@" <<'PY'
import re, sys
path, port, offset, friend = sys.argv[1:5]
s = open(path).read()
s = re.sub(r'(?m)^internet-bluetooth-port: .*$', 'internet-bluetooth-port: %s' % port, s)
s = re.sub(r'(?m)^btnet-port-offset: .*$', 'btnet-port-offset: %s' % offset, s)
s = re.sub(r'(?m)^btnet-discovery-mode: .*$', 'btnet-discovery-mode: 1', s)
s = re.sub(r'(?m)^enable-upnp: .*$', 'enable-upnp: false', s)
i = s.index('internet-bluetooth-friends:')
s = s[:i] + 'internet-bluetooth-friends:\n  - address: 127.0.0.1\n    port: %s\n' % friend
open(path, 'w').write(s)
PY
}
install "$HA"; install "$HB"
cp "$HA/config.yml" "$S/duo_configA.yml"
netconf "$HA/config.yml" 35689 15000 35690
cp "$HB/config.yml" "$S/duo_configB.yml"
netconf "$HB/config.yml" 35690 16000 35689

export EKA2L1_STRICTHANDLE="${EKA2L1_STRICTHANDLE:-1}" EKA2L1_KERNREQ="${EKA2L1_KERNREQ:-1}"
rm -f "$S"/duoA_*.png "$S"/duoB_*.png
# DUO_ENV_A / DUO_ENV_B: VAR=value words for one instance only (a trace path each).
(cd /home/user/EKA2L1/build/bin && env $DUO_ENV_A timeout -k 5 -s KILL "${TMO:-200}" ./eka2l1_qt --device "${DEVICE:-RM-409}" --run $UID3 >"$S/duoA.log" 2>&1) &
sleep 2
(cd /home/user/EKA2L1/build/bin && env $DUO_ENV_B DISPLAY=:98 HOME="$S/home2" timeout -k 5 -s KILL "${TMO:-200}" ./eka2l1_qt --device "${DEVICE:-RM-409}" --run $UID3 >"$S/duoB.log" 2>&1) &
sleep 20
PA=$(pgrep -x eka2l1_qt | head -1); PB=$(pgrep -x eka2l1_qt | sed -n 2p)
WA=$(xdotool search --onlyvisible --pid "$PA" 2>/dev/null | head -1)
WB=$(DISPLAY=:98 xdotool search --onlyvisible --pid "$PB" 2>/dev/null | head -1)
echo "A pid $PA win $WA   B pid $PB win $WB"
bash "${DUO_A:-$P/duo_host.sh}" "$WA" "$S/duoA" & KA=$!
DISPLAY=:98 bash "${DUO_B:-$P/duo_join.sh}" "$WB" "$S/duoB" & KB=$!
wait $KA $KB                # not a bare wait: an Xvfb this script started is a child too
pkill -x eka2l1_qt 2>/dev/null; sleep 1; pkill -9 -x eka2l1_qt 2>/dev/null
cp "$S/duo_configA.yml" "$HA/config.yml"
cp "$S/duo_configB.yml" "$HB/config.yml"

recs() { python3 -c "import os,sys;p=sys.argv[1];print(os.path.getsize(p)//8 if os.path.exists(p) else 0)" "$1"; }
RA=$(recs "$HA/data/drives/c/g6box-$STEM.log"); RB=$(recs "$HB/data/drives/c/g6box-$STEM.log")
cp -f "$HA/data/drives/c/g6box-$STEM.log" "$S/duoA-g6box.log" 2>/dev/null
cp -f "$HB/data/drives/c/g6box-$STEM.log" "$S/duoB-g6box.log" 2>/dev/null
exec 9>"$R.lock"; flock 9
N=$(grep -oE '^\| E([0-9]+) ' "$R" | grep -oE '[0-9]+' | sort -n | tail -1); N=$((N + 1))
python3 - "$R" "E$N" "duo: $CHANGE" "A $RA, B $RB" <<'PY'
import sys
path, num, change, rec = sys.argv[1:5]
s = open(path).read()
marker = '<!-- EMURUN -->'
row = '| %s | %s | %s | `--` | TODO |\n' % (num, change, rec)
i = s.index(marker)
j = s.rindex('\n', 0, i)
j = s.rindex('\n', 0, j) + 1
open(path, 'w').write(s[:j] + row + s[j:])
PY
flock -u 9
echo "records: A $RA, B $RB   -> logged as E$N in ROUNDS.md (finish its last column)"
echo
python3 "$P/rules.py"
