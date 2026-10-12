#!/bin/bash
# solo_ap.sh -- Colin McRae single-player, menu to stage, then the autopilot:
# one emulator instead of the duo, for working on the autopilot (E1245).
#
#   solo_ap.sh &  GAME=colin TMO=900 ./emurun.sh "..."
#
# The route is duo_loadrace.sh's (E1145): RALLY, the mode, the difficulty,
# the driver profile, then the service area's RACE into RALLY FINLAND STAGE 1
# (Mikkeli, 5.0 km, the duo's stage). E976's Right x3 from the main menu is
# stale: the menu opens on RALLY now (E1246). Shots every 4 s to 160 s; the
# autopilot from START (default 104) for AP_SECS.
export DISPLAY=:99
S="${EMUSCRATCH:-/tmp/claude-0/-home-user-EKA2L1/ec0d1fd1-56fb-5d4c-9116-ef9f3a4e0d5a/scratchpad}"
O="$S/solo"; t0=$(date +%s)
at() { while [ $(( $(date +%s) - t0 )) -lt $1 ]; do sleep 0.5; done; }
until W=$(xdotool search --onlyvisible --pid "$(pgrep -x eka2l1_qt | head -1)" 2>/dev/null | head -1) && [ -n "$W" ]; do
    sleep 1; [ $(( $(date +%s) - t0 )) -gt 60 ] && { echo "solo_ap: no window"; exit 1; }
done
k() { timeout 5 xdotool windowactivate --sync "$W" 2>/dev/null; timeout 5 xdotool key --window "$W" $1; }
shot() { timeout 10 import -window "$W" ${O}_$1.png 2>/dev/null; }
( for t in $(seq 8 4 160); do at $t; shot $(printf %03d $t)s; done ) &
at 30; k Return; at 34; k Return; at 38; k Return
at 42; k Up; at 43; k Up; at 44; k Return; at 48; k Return
for t in 54 58 62 66 70 74 78 82; do at $t; k Return; done
at 88; k Right; at 89; k Right; at 90; k Right; at 92; k Return; at 96; k Return; at 100; k Return
at "${START:-104}"; python3 "$(dirname "$0")/autopilot.py" "$W" "${AP_SECS:-600}" "${O}_ap.log"
wait
